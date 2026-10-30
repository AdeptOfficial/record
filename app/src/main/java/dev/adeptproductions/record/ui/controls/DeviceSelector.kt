package dev.adeptproductions.record.ui.controls

import android.media.AudioDeviceInfo
import androidx.compose.foundation.layout.Box
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import dev.adeptproductions.record.R
import dev.adeptproductions.record.audio.InputDevice

/** Microphone picker. Id 0 is the system default input. */
@Composable
fun DeviceSelector(
    devices: List<InputDevice>,
    selectedId: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    var expanded by remember { mutableStateOf(false) }
    val defaultLabel = stringResource(R.string.input_default)
    val selected = devices.firstOrNull { it.id == selectedId }
    Box(modifier) {
        OutlinedButton(onClick = { expanded = true }) {
            Text(
                text = selected?.let { deviceLabel(it) } ?: defaultLabel,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            DropdownMenuItem(
                text = { Text(defaultLabel) },
                onClick = {
                    expanded = false
                    onSelect(0)
                },
            )
            devices.forEach { device ->
                DropdownMenuItem(
                    text = { Text(deviceLabel(device)) },
                    onClick = {
                        expanded = false
                        onSelect(device.id)
                    },
                )
            }
        }
    }
}

@Composable
private fun deviceLabel(device: InputDevice): String {
    val kind = stringResource(
        when (device.type) {
            AudioDeviceInfo.TYPE_BUILTIN_MIC -> R.string.input_builtin
            AudioDeviceInfo.TYPE_WIRED_HEADSET -> R.string.input_headset
            AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> R.string.input_bluetooth
            AudioDeviceInfo.TYPE_LINE_ANALOG, AudioDeviceInfo.TYPE_LINE_DIGITAL, AudioDeviceInfo.TYPE_AUX_LINE ->
                R.string.input_line
            else -> R.string.input_usb
        }
    )
    return if (device.name.isBlank()) kind else "$kind · ${device.name}"
}
