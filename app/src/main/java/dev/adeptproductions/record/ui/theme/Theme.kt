package dev.adeptproductions.record.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import dev.adeptproductions.record.audio.LoopState

private val DarkColors = darkColorScheme(
    primary = Color(0xFF8AB4F8),
    onPrimary = Color(0xFF0B1E3A),
    background = Color(0xFF111316),
    onBackground = Color(0xFFE6E8EB),
    surface = Color(0xFF111316),
    onSurface = Color(0xFFE6E8EB),
    surfaceVariant = Color(0xFF2A2E33),
    onSurfaceVariant = Color(0xFFC1C6CC),
    errorContainer = Color(0xFF5C1A1A),
    onErrorContainer = Color(0xFFFFDAD6),
)

private val LightColors = lightColorScheme(
    primary = Color(0xFF1A5BB5),
    onPrimary = Color.White,
    background = Color(0xFFF8F9FA),
    onBackground = Color(0xFF1B1C1E),
    surface = Color(0xFFF8F9FA),
    onSurface = Color(0xFF1B1C1E),
    surfaceVariant = Color(0xFFE1E3E6),
    onSurfaceVariant = Color(0xFF44474B),
)

/** Button fill and text for each state; every pair meets WCAG AA (4.5:1) for its label. */
data class StateColors(val fill: Color, val content: Color)

fun stateColors(state: LoopState): StateColors = when (state) {
    LoopState.Idle -> StateColors(Color(0xFF5F6368), Color.White)
    LoopState.Recording -> StateColors(Color(0xFFC62828), Color.White)
    LoopState.Playing -> StateColors(Color(0xFF2E7D32), Color.White)
    LoopState.Overdubbing -> StateColors(Color(0xFFF9A825), Color(0xFF1B1C1E))
    LoopState.Stopped -> StateColors(Color(0xFF1565C0), Color.White)
}

@Composable
fun RecordTheme(darkTheme: Boolean = isSystemInDarkTheme(), content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = if (darkTheme) DarkColors else LightColors, content = content)
}
