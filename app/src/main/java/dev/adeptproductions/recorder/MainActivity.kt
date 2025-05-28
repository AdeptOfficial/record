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
import java.io.File

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
    val audioPlayer = remember { AudioPlayer(context) }
    val audioRecorder = remember { AudioRecorder(context) }

    var isRecording by remember { mutableStateOf(false) }
    var hasRecording by remember { mutableStateOf(false) }

    val outputFilePath = remember {
        "${context.getExternalFilesDir(null)?.absolutePath}/recording.3gp"
    }

    val permissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestPermission()
    ) { isGranted ->
        if (isGranted) {
            isRecording = true
            audioRecorder.startRecording(outputFilePath)
        } else {
            Toast.makeText(context, "Mic permission denied", Toast.LENGTH_SHORT).show()
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

                        val file = File(outputFilePath)
                        if (file.exists() && file.length() > 0) {
                            Toast.makeText(context, "✅ Saved: ${file.absolutePath}", Toast.LENGTH_SHORT).show()
                            audioPlayer.play(outputFilePath)
                        } else {
                            Toast.makeText(context, "❌ File not saved or empty", Toast.LENGTH_LONG).show()
                        }
                    }

                    hasRecording -> {
                        audioPlayer.play(outputFilePath) {
                            Toast.makeText(context, "Playback finished", Toast.LENGTH_SHORT).show()
                        }
                    }
                    else -> {
                        val status = ContextCompat.checkSelfPermission(
                            context, Manifest.permission.RECORD_AUDIO
                        )
                        if (status == PackageManager.PERMISSION_GRANTED) {
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
                contentDescription = null,
                tint = Color.White
            )
        }
    }
}
