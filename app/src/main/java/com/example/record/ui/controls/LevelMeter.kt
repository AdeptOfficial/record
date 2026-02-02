package com.example.record.ui.controls

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material.LinearProgressIndicator
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun LevelMeter(level: Float) {
    LinearProgressIndicator(
        progress = level,
        modifier = Modifier
            .fillMaxWidth()
            .height(16.dp)
    )
}
