package dev.adeptproductions.record.ui.controls

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import dev.adeptproductions.record.R
import kotlin.math.log10

private const val FLOOR_DB = -60f

/** Input peak on a -60..0 dBFS scale: green, amber from -12 dB, red from -3 dB. */
@Composable
fun LevelMeter(peak: Float, modifier: Modifier = Modifier) {
    val db = if (peak > 0f) 20f * log10(peak) else FLOOR_DB
    val fraction = ((db - FLOOR_DB) / -FLOOR_DB).coerceIn(0f, 1f)
    val color = when {
        db >= -3f -> Color(0xFFE53935)
        db >= -12f -> Color(0xFFF9A825)
        else -> Color(0xFF43A047)
    }
    val track = MaterialTheme.colorScheme.surfaceVariant
    val description = stringResource(R.string.input_level)
    Canvas(
        modifier
            .fillMaxWidth()
            .height(10.dp)
            .semantics {
                contentDescription = description
                progressBarRangeInfo = ProgressBarRangeInfo(fraction, 0f..1f)
            }
    ) {
        val radius = CornerRadius(size.height / 2)
        drawRoundRect(track, cornerRadius = radius)
        if (fraction > 0f) drawRoundRect(color, size = Size(size.width * fraction, size.height), cornerRadius = radius)
    }
}
