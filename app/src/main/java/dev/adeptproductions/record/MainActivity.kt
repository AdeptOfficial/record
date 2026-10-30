package dev.adeptproductions.record

import android.os.Bundle
import android.os.SystemClock
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import dev.adeptproductions.record.controller.LoopController
import dev.adeptproductions.record.ui.MainScreen
import dev.adeptproductions.record.ui.theme.RecordTheme

class MainActivity : ComponentActivity() {
    private val controller: LoopController by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        setContent {
            RecordTheme {
                MainScreen(controller)
            }
        }
    }

    override fun onStop() {
        super.onStop()
        if (!isChangingConfigurations) controller.onScreenHidden(SystemClock.uptimeMillis())
    }
}
