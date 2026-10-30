package dev.adeptproductions.record.ui

import android.Manifest
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import dev.adeptproductions.record.R
import dev.adeptproductions.record.audio.EngineStatus
import dev.adeptproductions.record.audio.InputDevice
import dev.adeptproductions.record.audio.LoopSaver
import dev.adeptproductions.record.audio.LoopState
import dev.adeptproductions.record.audio.SaveResult
import dev.adeptproductions.record.controller.LoopController
import dev.adeptproductions.record.ui.controls.DeviceSelector
import dev.adeptproductions.record.ui.controls.LevelMeter
import dev.adeptproductions.record.ui.controls.LooperButton
import java.util.Locale

@Composable
fun MainScreen(controller: LoopController = viewModel()) {
    val state by controller.state.collectAsStateWithLifecycle()
    val devices by controller.inputDevices.collectAsStateWithLifecycle()
    val externalOutput by controller.externalOutput.collectAsStateWithLifecycle()
    val context = LocalContext.current
    val resources = LocalResources.current
    val snackbar = remember { SnackbarHostState() }

    val micLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) {
        controller.onMicPermissionResult(it)
    }
    val storageLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) controller.saveLoop()
    }
    LaunchedEffect(Unit) {
        if (!state.micPermission) micLauncher.launch(Manifest.permission.RECORD_AUDIO)
    }
    LaunchedEffect(Unit) {
        controller.saveResults.collect { result ->
            val message = when (result) {
                is SaveResult.Saved -> resources.getString(R.string.saved_as, result.displayName)
                SaveResult.NoLoop -> resources.getString(R.string.save_no_loop)
                SaveResult.Busy -> resources.getString(R.string.save_busy)
                SaveResult.NeedsPermission -> resources.getString(R.string.save_needs_permission)
                SaveResult.Failed -> resources.getString(R.string.save_failed)
            }
            snackbar.showSnackbar(message)
        }
    }

    val status = state.status
    val canPlay = state.micPermission && state.engineError == null && status.running

    Scaffold(
        snackbarHost = { SnackbarHost(snackbar) },
        containerColor = MaterialTheme.colorScheme.background,
    ) { padding ->
        BoxWithConstraints(Modifier.fillMaxSize().padding(padding)) {
            val landscape = maxWidth > maxHeight && maxWidth >= 560.dp
            val header = @Composable {
                Text(
                    text = stringResource(R.string.app_name),
                    style = MaterialTheme.typography.headlineSmall,
                    modifier = Modifier.fillMaxWidth().semantics { heading() },
                )
                Notices(
                    micPermission = state.micPermission,
                    engineError = state.engineError,
                    externalOutput = externalOutput,
                    onRequestMic = { micLauncher.launch(Manifest.permission.RECORD_AUDIO) },
                    onOpenSettings = {
                        context.startActivity(
                            Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", context.packageName, null))
                        )
                    },
                    onRetry = controller::retry,
                )
            }
            val looper = @Composable { diameter: Dp ->
                LooperButton(
                    state = status.state,
                    progress = status.progress,
                    enabled = canPlay,
                    onCommand = controller::onCommand,
                    diameter = diameter,
                )
                Text(
                    text = loopSummary(status),
                    style = MaterialTheme.typography.titleMedium,
                    textAlign = TextAlign.Center,
                )
            }
            val details = @Composable {
                Text(
                    text = stringResource(hint(status.state)),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    textAlign = TextAlign.Center,
                    modifier = Modifier.widthIn(max = 420.dp),
                )
                Controls(
                    level = state.inputLevel,
                    monitoring = state.monitoring,
                    onMonitoring = controller::setMonitoring,
                    devices = devices,
                    selectedDevice = status.inputDeviceId,
                    onSelectDevice = controller::selectInputDevice,
                    maxWidth = 420.dp,
                )
                Button(
                    onClick = {
                        if (LoopSaver.needsPermission(context)) {
                            storageLauncher.launch(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                        } else {
                            controller.saveLoop()
                        }
                    },
                    enabled = status.hasLoop && status.state == LoopState.Stopped && !state.saving,
                ) {
                    Text(stringResource(if (state.saving) R.string.saving else R.string.save_loop))
                }
                if (status.hasLoop && status.state != LoopState.Stopped) {
                    Text(
                        text = stringResource(R.string.save_hint),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }

            if (landscape) {
                // Button on the left, everything else beside it: nothing hides below the fold.
                val diameter = minOf(260.dp, maxHeight * 0.7f, maxWidth * 0.4f).coerceAtLeast(160.dp)
                Row(
                    horizontalArrangement = Arrangement.spacedBy(24.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.fillMaxSize().padding(horizontal = 24.dp, vertical = 8.dp),
                ) {
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(12.dp, Alignment.CenterVertically),
                        modifier = Modifier.weight(1f).fillMaxHeight(),
                    ) { looper(diameter) }
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(12.dp),
                        modifier = Modifier.weight(1f).fillMaxHeight().verticalScroll(rememberScrollState()),
                    ) {
                        header()
                        details()
                    }
                }
            } else {
                val diameter = minOf(260.dp, maxWidth * 0.7f, maxHeight * 0.45f).coerceAtLeast(160.dp)
                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(16.dp),
                    modifier = Modifier
                        .fillMaxSize()
                        .verticalScroll(rememberScrollState())
                        .padding(horizontal = 24.dp, vertical = 16.dp),
                ) {
                    header()
                    looper(diameter)
                    details()
                    Spacer(Modifier.height(8.dp))
                }
            }
        }
    }
}

@Composable
private fun Notices(
    micPermission: Boolean,
    engineError: Int?,
    externalOutput: Boolean,
    onRequestMic: () -> Unit,
    onOpenSettings: () -> Unit,
    onRetry: () -> Unit,
) {
    when {
        !micPermission -> Notice(stringResource(R.string.mic_needed)) {
            TextButton(onClick = onRequestMic) { Text(stringResource(R.string.allow_mic)) }
            TextButton(onClick = onOpenSettings) { Text(stringResource(R.string.open_settings)) }
        }
        engineError != null -> Notice(stringResource(R.string.audio_error, engineError)) {
            TextButton(onClick = onRetry) { Text(stringResource(R.string.retry)) }
        }
    }
    if (!externalOutput) Notice(stringResource(R.string.headphones_warning))
}

@Composable
private fun Notice(text: String, actions: @Composable () -> Unit = {}) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
        modifier = Modifier.fillMaxWidth().widthIn(max = 420.dp),
    ) {
        Column(Modifier.padding(16.dp)) {
            Text(text, style = MaterialTheme.typography.bodyMedium)
            Row(horizontalArrangement = Arrangement.End, modifier = Modifier.fillMaxWidth()) { actions() }
        }
    }
}

@Composable
private fun Controls(
    level: Float,
    monitoring: Boolean,
    onMonitoring: (Boolean) -> Unit,
    devices: List<InputDevice>,
    selectedDevice: Int,
    onSelectDevice: (Int) -> Unit,
    maxWidth: Dp,
) {
    Column(verticalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.widthIn(max = maxWidth)) {
        LevelMeter(level)
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
            DeviceSelector(devices, selectedDevice, onSelectDevice, Modifier.weight(1f))
            Text(
                text = stringResource(R.string.monitor),
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.padding(horizontal = 8.dp),
            )
            Switch(checked = monitoring, onCheckedChange = onMonitoring)
        }
    }
}

@Composable
private fun loopSummary(status: EngineStatus): String = when {
    status.state == LoopState.Recording -> stringResource(
        R.string.recording_time, seconds(status.recordedSeconds), seconds(status.maxLoopSeconds)
    )
    status.hasLoop -> pluralStringResource(R.plurals.loop_summary, status.layers, seconds(status.loopSeconds), status.layers)
    else -> stringResource(R.string.no_loop)
}

private fun seconds(value: Double) = String.format(Locale.getDefault(), "%.1f", value)

private fun hint(state: LoopState) = when (state) {
    LoopState.Idle -> R.string.hint_idle
    LoopState.Recording -> R.string.hint_recording
    LoopState.Playing -> R.string.hint_playing
    LoopState.Overdubbing -> R.string.hint_overdubbing
    LoopState.Stopped -> R.string.hint_stopped
}
