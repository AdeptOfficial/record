package com.example.record.ui.controls

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.record.controller.LoopState
import kotlinx.coroutines.delay
import kotlinx.coroutines.withTimeoutOrNull

sealed class LooperGesture {
    object Tap : LooperGesture()
    object Hold : LooperGesture()
    object DoubleTap : LooperGesture()
    object DoubleTapHold : LooperGesture()
}

private const val TAP_TIMEOUT_MS = 300L
private const val HOLD_TIMEOUT_MS = 500L
private const val DOUBLE_TAP_WINDOW_MS = 300L

@Composable
fun LooperButton(
    loopState: LoopState,
    onGesture: (LooperGesture) -> Unit,
    modifier: Modifier = Modifier
) {
    val backgroundColor = when (loopState) {
        LoopState.Idle -> Color.Gray
        LoopState.Recording -> Color.Red
        LoopState.Playing -> Color.Green
        LoopState.Overdubbing -> Color.Yellow
        LoopState.Stopped -> Color.Blue
    }

    val buttonText = when (loopState) {
        LoopState.Idle -> "TAP TO\nRECORD"
        LoopState.Recording -> "RECORDING"
        LoopState.Playing -> "PLAYING"
        LoopState.Overdubbing -> "OVERDUB"
        LoopState.Stopped -> "STOPPED"
    }

    var isPressed by remember { mutableStateOf(false) }
    var pendingTapTime by remember { mutableLongStateOf(0L) }

    // Handle delayed single tap detection
    LaunchedEffect(pendingTapTime) {
        if (pendingTapTime > 0) {
            delay(DOUBLE_TAP_WINDOW_MS)
            if (pendingTapTime > 0) {
                onGesture(LooperGesture.Tap)
                pendingTapTime = 0L
            }
        }
    }

    Box(
        contentAlignment = Alignment.Center,
        modifier = modifier
            .size(200.dp)
            .clip(CircleShape)
            .background(if (isPressed) backgroundColor.copy(alpha = 0.7f) else backgroundColor)
            .pointerInput(Unit) {
                var lastTapTime = 0L

                awaitEachGesture {
                    val down = awaitFirstDown()
                    isPressed = true
                    val downTime = System.currentTimeMillis()
                    val timeSinceLastTap = downTime - lastTapTime
                    val isSecondTap = timeSinceLastTap < DOUBLE_TAP_WINDOW_MS

                    // Cancel any pending single tap if this is a second tap
                    if (isSecondTap) {
                        pendingTapTime = 0L
                    }

                    // Wait for release with timeout for hold detection
                    val up = withTimeoutOrNull(HOLD_TIMEOUT_MS) {
                        waitForUpOrCancellation()
                    }

                    isPressed = false

                    if (up == null) {
                        // Timeout - this is a hold
                        if (isSecondTap) {
                            onGesture(LooperGesture.DoubleTapHold)
                        } else {
                            onGesture(LooperGesture.Hold)
                        }
                        // Wait for actual release
                        waitForUpOrCancellation()
                        lastTapTime = 0L
                    } else {
                        // Released before timeout - this is a tap
                        val pressDuration = System.currentTimeMillis() - downTime
                        if (pressDuration < TAP_TIMEOUT_MS) {
                            if (isSecondTap) {
                                onGesture(LooperGesture.DoubleTap)
                                lastTapTime = 0L
                            } else {
                                lastTapTime = downTime
                                pendingTapTime = downTime
                            }
                        }
                    }
                }
            }
    ) {
        Text(
            text = buttonText,
            fontSize = 24.sp,
            color = Color.White
        )
    }
}
