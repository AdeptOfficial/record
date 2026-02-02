package com.example.record.ui

import android.content.Context
import android.os.Environment
import android.widget.Toast
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material.Button
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.record.controller.LoopController
import com.example.record.controller.LoopState
import com.example.record.ui.controls.LooperButton
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@Composable
fun MainScreen() {
    val context = LocalContext.current
    val loopController = remember { LoopController() }
    val loopState = loopController.loopState.collectAsState()

    Box(
        modifier = Modifier.fillMaxSize(),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center
        ) {
            LooperButton(
                loopState = loopState.value,
                onGesture = { gesture -> loopController.onGesture(gesture) }
            )

            Spacer(modifier = Modifier.height(32.dp))

            Text(
                text = getHintText(loopState.value),
                fontSize = 14.sp
            )

            if (loopState.value == LoopState.Stopped) {
                Spacer(modifier = Modifier.height(24.dp))
                Button(
                    onClick = {
                        saveLoop(context, loopController)
                    }
                ) {
                    Text("Save Loop")
                }
            }
        }
    }
}

private fun getHintText(state: LoopState): String {
    return when (state) {
        LoopState.Idle -> "Tap to start recording"
        LoopState.Recording -> "Tap to set loop length"
        LoopState.Playing -> "Tap: overdub | Hold: undo | 2x tap: stop"
        LoopState.Overdubbing -> "Tap: play | Hold: undo | 2x tap: stop"
        LoopState.Stopped -> "Tap: resume | Hold: delete"
    }
}

private fun saveLoop(context: Context, loopController: LoopController) {
    val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
    val musicDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MUSIC)
    val file = File(musicDir, "loop_$timestamp.wav")

    val success = loopController.exportLoop(file.absolutePath)
    if (success) {
        Toast.makeText(context, "Saved to ${file.name}", Toast.LENGTH_SHORT).show()
    } else {
        Toast.makeText(context, "Failed to save", Toast.LENGTH_SHORT).show()
    }
}
