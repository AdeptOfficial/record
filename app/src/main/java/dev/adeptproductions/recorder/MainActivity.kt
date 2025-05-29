package dev.adeptproductions.recorder

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.util.Log
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import dev.adeptproductions.recorder.audio.AudioPlayer
import dev.adeptproductions.recorder.audio.NativeBridge
import dev.adeptproductions.recorder.ui.theme.RecordTheme
import java.io.File

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        NativeBridge.testNativeCall()
        setContent {
            RecordTheme {
                Scaffold(modifier = Modifier.fillMaxSize()) { padding ->
                    SoundRecorderUI(modifier = Modifier.padding(padding))
                }
            }
        }
    }
}

@Composable
fun SoundRecorderUI(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val activity = context as? ComponentActivity
    val audioPlayer = remember { AudioPlayer(context) }

    var isRecording by remember { mutableStateOf(false) }
    var hasRecording by remember { mutableStateOf(false) }
    var isLooping by remember { mutableStateOf(false) }
    var isBusy by remember { mutableStateOf(false) }

    val outputFilePath = remember {
        "${context.getExternalFilesDir(null)?.absolutePath}/recording_loop.wav"
    }
    Log.i("MainActivity", "🧭 Recording to path: $outputFilePath")

    // ✅ Define recording logic before permission launcher
    fun startRecording() {
        if (isBusy) return
        isBusy = true
        Thread {
            NativeBridge.startSuperpoweredRecording(outputFilePath, 44100)
            Thread.sleep(100)
            activity?.runOnUiThread {
                isRecording = true
                isBusy = false
            }
        }.start()
    }

    fun stopRecording() {
        if (isBusy) return
        isBusy = true
        Thread {
            NativeBridge.stopSuperpoweredRecording()
            Thread.sleep(500) // Allow time to flush file

            val file = File(outputFilePath)
            val fileSize = file.length()
            val valid = file.exists() && fileSize > 0

            Log.i("MainActivity", "📁 File size: $fileSize bytes")

            activity?.runOnUiThread {
                isRecording = false
                hasRecording = valid
                isBusy = false
                if (valid) {
                    isLooping = true
                    audioPlayer.play(outputFilePath, loop = true)
                    Toast.makeText(context, "✅ Saved: ${file.absolutePath}", Toast.LENGTH_SHORT).show()
                } else {
                    Toast.makeText(context, "❌ File not saved or empty", Toast.LENGTH_LONG).show()
                }
            }
        }.start()
    }

    // ✅ Permission launcher after startRecording is defined
    val permissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) startRecording()
        else Toast.makeText(context, "Microphone permission denied", Toast.LENGTH_SHORT).show()
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Button(
            onClick = {
                if (isBusy) return@Button
                when {
                    isRecording -> stopRecording()
                    hasRecording && !audioPlayer.isPlaying() -> {
                        isLooping = true
                        audioPlayer.play(outputFilePath, loop = true)
                        Toast.makeText(context, "🔁 Looping playback", Toast.LENGTH_SHORT).show()
                    }
                    hasRecording && audioPlayer.isPlaying() -> {
                        isLooping = false
                        audioPlayer.stop()
                        Toast.makeText(context, "⏹️ Loop stopped", Toast.LENGTH_SHORT).show()
                    }
                    else -> {
                        val granted = ContextCompat.checkSelfPermission(
                            context,
                            Manifest.permission.RECORD_AUDIO
                        ) == PackageManager.PERMISSION_GRANTED
                        if (granted) startRecording()
                        else permissionLauncher.launch(Manifest.permission.RECORD_AUDIO)
                    }
                }
            },
            enabled = !isBusy,
            shape = CircleShape,
            modifier = Modifier.size(100.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = when {
                    isRecording -> Color.Red
                    isLooping -> Color.Blue
                    else -> Color.Gray
                }
            )
        ) {
            Icon(
                imageVector = if (isRecording || isLooping) Icons.Default.Stop else Icons.Default.Mic,
                contentDescription = null,
                tint = Color.White
            )
        }

        Spacer(modifier = Modifier.height(16.dp))

        Text(
            text = when {
                isBusy -> "Please wait..."
                isRecording -> "Recording..."
                isLooping -> "Playing in loop"
                hasRecording -> "Ready to play"
                else -> "Press to start"
            }
        )
    }
}
