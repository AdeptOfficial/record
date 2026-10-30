package dev.adeptproductions.record.controller

import android.Manifest
import android.app.Application
import android.content.pm.PackageManager
import android.media.AudioManager
import android.os.SystemClock
import android.util.Log
import androidx.core.content.ContextCompat
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import androidx.lifecycle.viewModelScope
import dev.adeptproductions.record.audio.AudioDevices
import dev.adeptproductions.record.audio.EngineStatus
import dev.adeptproductions.record.audio.InputDevice
import dev.adeptproductions.record.audio.LoopSaver
import dev.adeptproductions.record.audio.LooperCommand
import dev.adeptproductions.record.audio.LooperEngine
import dev.adeptproductions.record.audio.SaveResult
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

data class LooperUiState(
    val status: EngineStatus = EngineStatus(),
    val micPermission: Boolean = false,
    /** `oboe::Result` code when the audio streams could not be opened. */
    val engineError: Int? = null,
    val monitoring: Boolean = false,
    val saving: Boolean = false,
    /** Input peak with a short decay, 0..1, for the level meter. */
    val inputLevel: Float = 0f,
)

/**
 * Owns the looper for the whole app process. Audio runs while the app is in the
 * foreground and stops (keeping the loop) when it is backgrounded; configuration
 * changes such as rotation do not interrupt it.
 */
class LoopController(application: Application) : AndroidViewModel(application), DefaultLifecycleObserver {
    private val audioManager = application.getSystemService(AudioManager::class.java)
    private val engine = LooperEngine(nativeSampleRate(audioManager), MAX_LOOP_SECONDS)
    private val devices = AudioDevices(audioManager)

    private val _state = MutableStateFlow(LooperUiState(micPermission = hasMicPermission()))
    val state: StateFlow<LooperUiState> = _state.asStateFlow()
    val inputDevices: StateFlow<List<InputDevice>> = devices.inputs
    val externalOutput: StateFlow<Boolean> = devices.externalOutput

    private val _saveResults = Channel<SaveResult>(Channel.BUFFERED)
    val saveResults: Flow<SaveResult> = _saveResults.receiveAsFlow()

    @Volatile private var foreground = false
    private var pollJob: Job? = null

    init {
        devices.start()
        // The process lifecycle ignores configuration changes, unlike an activity's.
        ProcessLifecycleOwner.get().lifecycle.addObserver(this)
    }

    override fun onStart(owner: LifecycleOwner) {
        foreground = true
        _state.update { it.copy(micPermission = hasMicPermission()) }
        startEngine()
    }

    override fun onStop(owner: LifecycleOwner) {
        foreground = false
        pollJob?.cancel()
        pollJob = null
        engine.stop()  // releases the mic; recording and overdubs are kept, playback stops
        _state.update { it.copy(status = engine.status(), inputLevel = 0f) }
    }

    /**
     * The activity left the screen for real (not a configuration change). Stops the loop
     * at that moment; the process lifecycle only reports the app stopped ~700 ms later,
     * which would otherwise add that much audio to a recording in progress.
     */
    fun onScreenHidden(uptimeMillis: Long) {
        engine.post(LooperCommand.Stop, uptimeMillis * 1_000_000)
    }

    fun onMicPermissionResult(granted: Boolean) {
        _state.update { it.copy(micPermission = granted) }
        startEngine()
    }

    fun retry() = startEngine()

    /** [eventUptimeMillis] is the time of the touch, from `SystemClock.uptimeMillis()`. */
    fun onCommand(command: LooperCommand, eventUptimeMillis: Long) {
        engine.post(command, eventUptimeMillis * 1_000_000)
    }

    fun setMonitoring(enabled: Boolean) {
        engine.setMonitoring(enabled)
        _state.update { it.copy(monitoring = enabled) }
    }

    fun selectInputDevice(deviceId: Int) {
        val result = engine.setInputDevice(deviceId)
        _state.update { it.copy(engineError = result.takeIf { code -> code != 0 }) }
    }

    fun saveLoop() {
        viewModelScope.launch { _saveResults.send(saveLoopNow()) }
    }

    suspend fun saveLoopNow(): SaveResult {
        if (_state.value.saving) return SaveResult.Busy
        _state.update { it.copy(saving = true) }
        try {
            return withContext(Dispatchers.IO) { LoopSaver(getApplication()).save(engine) }
        } finally {
            _state.update { it.copy(saving = false) }
        }
    }

    private fun startEngine() {
        if (!foreground || !_state.value.micPermission) return
        val result = engine.start()
        _state.update { it.copy(engineError = result.takeIf { code -> code != 0 }) }
        if (pollJob == null) pollJob = viewModelScope.launch(Dispatchers.Default) { poll() }
    }

    private suspend fun poll() {
        var level = 0f
        var lastMaintainMs = 0L
        var lastLogMs = 0L
        while (currentCoroutineContext().isActive) {
            val now = SystemClock.uptimeMillis()
            if (now - lastMaintainMs >= MAINTAIN_INTERVAL_MS) {
                engine.maintain()
                lastMaintainMs = now
            }
            val status = engine.status()
            if (!foreground) break  // onStop has published the final status
            level = maxOf(status.inputPeak, level * LEVEL_DECAY)
            _state.update {
                it.copy(
                    status = status,
                    inputLevel = level,
                    // A stream that died and could not be reopened shows up here.
                    engineError = if (status.running) null else it.engineError ?: status.lastError.takeIf { e -> e != 0 },
                )
            }
            if (now - lastLogMs >= STATS_INTERVAL_MS && Log.isLoggable(STATS_TAG, Log.DEBUG)) {
                Log.d(STATS_TAG, "state=${status.state} loop=${status.loopFrames} layers=${status.layers} " +
                    "running=${status.running} xruns=${status.xruns} errors=${status.errorCount} " +
                    "roundTripMs=${"%.1f".format(status.roundTripMs)}")
                lastLogMs = now
            }
            delay(POLL_INTERVAL_MS)
        }
    }

    private fun hasMicPermission() = ContextCompat.checkSelfPermission(
        getApplication(), Manifest.permission.RECORD_AUDIO
    ) == PackageManager.PERMISSION_GRANTED

    override fun onCleared() {
        ProcessLifecycleOwner.get().lifecycle.removeObserver(this)
        devices.stop()
        pollJob?.cancel()
        engine.close()
    }

    companion object {
        const val MAX_LOOP_SECONDS = 120.0
        const val STATS_TAG = "RecordStats"  // adb shell setprop log.tag.RecordStats DEBUG
        private const val POLL_INTERVAL_MS = 16L
        private const val MAINTAIN_INTERVAL_MS = 1_000L
        private const val STATS_INTERVAL_MS = 10_000L
        private const val LEVEL_DECAY = 0.85f

        private fun nativeSampleRate(audioManager: AudioManager): Int =
            audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)?.toIntOrNull() ?: 48_000
    }
}
