package dev.adeptproductions.record.audio

import java.util.concurrent.locks.ReentrantReadWriteLock
import kotlin.concurrent.read
import kotlin.concurrent.write

/**
 * Owns the native looper. Calls may come from any thread; [close] waits for calls in
 * progress (an export, a status poll) before freeing the engine.
 */
class LooperEngine(sampleRate: Int, maxLoopSeconds: Double) : AutoCloseable {
    private val lock = ReentrantReadWriteLock()
    private var handle: Long = NativeBridge.nativeCreate(sampleRate, maxLoopSeconds)

    /** Opens the audio streams. Returns 0 or a negative `oboe::Result` code. */
    fun start(): Int = withHandle(RESULT_CLOSED) { NativeBridge.nativeStart(it) }

    /** Closes the streams and parks the loop in memory (Stopped). */
    fun stop() = withHandle(Unit) { NativeBridge.nativeStop(it) }

    fun setInputDevice(deviceId: Int): Int =
        withHandle(RESULT_CLOSED) { NativeBridge.nativeSetInputDevice(it, deviceId) }

    fun setMonitoring(enabled: Boolean) = withHandle(Unit) { NativeBridge.nativeSetMonitoring(it, enabled) }

    /** Sends a command stamped with the CLOCK_MONOTONIC time of the touch that caused it. */
    fun post(command: LooperCommand, eventTimeNs: Long): Boolean =
        withHandle(false) { NativeBridge.nativePost(it, command.code, eventTimeNs) }

    fun status(): EngineStatus = withHandle(EngineStatus()) {
        val values = LongArray(EngineStatus.SIZE)
        NativeBridge.nativeStatus(it, values)
        EngineStatus.fromArray(values)
    }

    /**
     * Call about once a second while running: re-measures the round-trip latency used to
     * line up overdubs and restarts streams that stopped silently. Milliseconds, or -1.
     */
    fun maintain(): Double = withHandle(-1.0) { NativeBridge.nativeMaintain(it) }

    /** Writes the mixed loop as a WAV file to [fd]. Blocks; call off the main thread. */
    fun exportWav(fd: Int): ExportResult =
        withHandle(ExportResult.IoError) { ExportResult.fromCode(NativeBridge.nativeExportWav(it, fd)) }

    override fun close() = lock.write {
        if (handle != 0L) {
            NativeBridge.nativeDestroy(handle)
            handle = 0L
        }
    }

    private inline fun <T> withHandle(closed: T, block: (Long) -> T): T =
        lock.read { if (handle == 0L) closed else block(handle) }

    enum class ExportResult(val code: Int) {
        Ok(0), NoLoop(1), Busy(2), IoError(3), Timeout(4);

        companion object {
            fun fromCode(code: Int) = entries.firstOrNull { it.code == code } ?: IoError
        }
    }

    private companion object {
        const val RESULT_CLOSED = -869  // oboe::Result::ErrorClosed
    }
}

/** One reading of the engine. Field order matches `status_index` in jni_bridge.cpp. */
data class EngineStatus(
    val state: LoopState = LoopState.Idle,
    val loopFrames: Long = 0,
    val positionFrames: Long = 0,
    val recordedFrames: Long = 0,
    val layers: Int = 0,
    val sampleRate: Int = 48_000,
    val inputPeak: Float = 0f,
    val roundTripFrames: Int = 0,
    val maxLoopFrames: Long = 0,
    val running: Boolean = false,
    val xruns: Int = 0,
    val errorCount: Int = 0,
    val lastError: Int = 0,
    val inputDeviceId: Int = 0,
) {
    val hasLoop: Boolean get() = loopFrames > 0
    val loopSeconds: Double get() = loopFrames.toDouble() / sampleRate
    val recordedSeconds: Double get() = recordedFrames.toDouble() / sampleRate
    val maxLoopSeconds: Double get() = maxLoopFrames.toDouble() / sampleRate
    val roundTripMs: Double get() = roundTripFrames * 1000.0 / sampleRate
    val progress: Float get() = if (loopFrames > 0) positionFrames.toFloat() / loopFrames else 0f

    companion object {
        const val SIZE = 14

        fun fromArray(v: LongArray) = EngineStatus(
            state = LoopState.fromCode(v[0].toInt()),
            loopFrames = v[1],
            positionFrames = v[2],
            recordedFrames = v[3],
            layers = v[4].toInt(),
            sampleRate = v[5].toInt().takeIf { it > 0 } ?: 48_000,
            inputPeak = java.lang.Float.intBitsToFloat(v[6].toInt()),
            roundTripFrames = v[7].toInt(),
            maxLoopFrames = v[8],
            running = v[9] != 0L,
            xruns = v[10].toInt(),
            errorCount = v[11].toInt(),
            lastError = v[12].toInt(),
            inputDeviceId = v[13].toInt(),
        )
    }
}
