package dev.adeptproductions.recorder

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
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
import dev.adeptproductions.recorder.audio.AudioRecorder
import dev.adeptproductions.recorder.ui.theme.RecordTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            RecordTheme {
                Scaffold(modifier = Modifier.fillMaxSize()) { innerPadding ->
                    SoundRecorderUI(modifier = Modifier.padding(innerPadding))
                }
            }
        }
    }
}

@Composable
fun SoundRecorderUI(modifier: Modifier = Modifier) {
    val context = LocalContext.current

    var isRecording by remember { mutableStateOf(false) }
    var hasRecording by remember { mutableStateOf(false) }

    val audioPlayer = remember { AudioPlayer(context) }
    val audioRecorder = remember { AudioRecorder(context) }

    val outputFilePath = remember {
        "${context.cacheDir.absolutePath}/recording.3gp"
    }

    val permissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestPermission()
    ) { isGranted ->
        if (isGranted) {
            isRecording = true
            audioRecorder.startRecording(outputFilePath)
        } else {
            Toast.makeText(context, "Microphone permission denied", Toast.LENGTH_SHORT).show()
        }
    }

    Box(
        modifier = modifier.fillMaxSize(),
        contentAlignment = Alignment.Center
    ) {
        Button(
            onClick = {
                when {
                    isRecording -> {
                        audioRecorder.stopRecording()
                        isRecording = false
                        hasRecording = true
                        Toast.makeText(context, "Recording stopped. Playing back...", Toast.LENGTH_SHORT).show()
                        audioPlayer.play(outputFilePath)
                    }

                    hasRecording -> {
                        audioPlayer.play(outputFilePath)
                    }

                    else -> {
                        val permissionStatus = ContextCompat.checkSelfPermission(
                            context,
                            Manifest.permission.RECORD_AUDIO
                        )
                        if (permissionStatus == PackageManager.PERMISSION_GRANTED) {
                            isRecording = true
                            audioRecorder.startRecording(outputFilePath)
                        } else {
                            permissionLauncher.launch(Manifest.permission.RECORD_AUDIO)
                        }
                    }
                }
            },
            shape = CircleShape,
            modifier = Modifier.size(100.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = when {
                    isRecording -> Color.Red
                    hasRecording -> Color.Green
                    else -> Color.Gray
                }
            )
        ) {
            Icon(
                imageVector = if (isRecording) Icons.Default.Stop else Icons.Default.Mic,
                contentDescription = if (isRecording) "Stop Recording" else "Start Recording",
                tint = Color.White
            )
        }
    }
}
