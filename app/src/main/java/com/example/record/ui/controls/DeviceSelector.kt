package com.example.record.ui.controls

import android.media.AudioDeviceInfo
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material.DropdownMenu
import androidx.compose.material.DropdownMenuItem
import androidx.compose.material.OutlinedButton
import androidx.compose.material.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier

@Composable
fun DeviceSelector(
    devices: List<AudioDeviceInfo>,
    onDeviceSelected: (AudioDeviceInfo) -> Unit
) {
    var expanded by remember { mutableStateOf(false) }
    var selectedDevice by remember { mutableStateOf<AudioDeviceInfo?>(null) }

    Box {
        OutlinedButton(
            onClick = { expanded = true },
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(selectedDevice?.productName?.toString() ?: "Select a device")
        }

        DropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false },
            modifier = Modifier.fillMaxWidth()
        ) {
            devices.forEach { device ->
                DropdownMenuItem(onClick = {
                    selectedDevice = device
                    onDeviceSelected(device)
                    expanded = false
                }) {
                    Text(device.productName.toString())
                }
            }
        }
    }
}
