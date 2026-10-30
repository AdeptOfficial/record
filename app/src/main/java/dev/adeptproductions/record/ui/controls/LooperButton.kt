package dev.adeptproductions.record.ui.controls

import android.os.SystemClock
import android.view.HapticFeedbackConstants
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.adeptproductions.record.R
import dev.adeptproductions.record.audio.LoopState
import dev.adeptproductions.record.audio.LooperCommand
import dev.adeptproductions.record.controller.GestureClassifier
import dev.adeptproductions.record.ui.theme.stateColors
import kotlinx.coroutines.withTimeoutOrNull

const val LOOPER_BUTTON_TAG = "looper_button"

/**
 * The single looper control. Commands carry the touch-down time, so the engine places
 * loop points where the finger landed rather than when the gesture was recognised.
 */
@Composable
fun LooperButton(
    state: LoopState,
    progress: Float,
    enabled: Boolean,
    onCommand: (LooperCommand, Long) -> Unit,
    diameter: Dp,
    modifier: Modifier = Modifier,
) {
    val view = LocalView.current
    val currentOnCommand by rememberUpdatedState(onCommand)
    val classifier = remember { GestureClassifier() }
    var pressed by remember { mutableStateOf(false) }
    val colors = stateColors(state)
    val trackColor = MaterialTheme.colorScheme.surfaceVariant
    val ringColor = colors.fill

    fun send(command: LooperCommand, timeMs: Long) {
        view.performHapticFeedback(
            if (command == LooperCommand.Hold || command == LooperCommand.DoubleTapHold) {
                HapticFeedbackConstants.LONG_PRESS
            } else {
                HapticFeedbackConstants.VIRTUAL_KEY
            }
        )
        currentOnCommand(command, timeMs)
    }

    val label = stringResource(R.string.looper_button)
    val stateText = stringResource(stateLabel(state))
    val clickLabel = stringResource(tapActionLabel(state))
    val undoLabel = stringResource(R.string.action_undo)
    val stopLabel = stringResource(R.string.action_stop)
    val clearLabel = stringResource(R.string.action_delete)

    Box(
        contentAlignment = Alignment.Center,
        modifier = modifier
            .size(diameter)
            .testTag(LOOPER_BUTTON_TAG)
            .semantics(mergeDescendants = true) {
                role = Role.Button
                contentDescription = label
                stateDescription = stateText
                if (!enabled) disabled()
                onClick(label = clickLabel) {
                    if (enabled) currentOnCommand(LooperCommand.Tap, SystemClock.uptimeMillis())
                    enabled
                }
                if (enabled) {
                    customActions = accessibilityActions(state, undoLabel, stopLabel, clearLabel) { command ->
                        currentOnCommand(command, SystemClock.uptimeMillis())
                    }
                }
            }
            .pointerInput(enabled) {
                if (!enabled) return@pointerInput
                awaitEachGesture {
                    val down = awaitFirstDown()
                    down.consume()
                    pressed = true
                    send(classifier.onDown(down.uptimeMillis), down.uptimeMillis)
                    val releasedAt = withTimeoutOrNull(GestureClassifier.HOLD_MS) {
                        waitForUpOrCancellation()?.uptimeMillis ?: SystemClock.uptimeMillis()
                    }
                    if (releasedAt == null) {
                        send(classifier.onHold(), SystemClock.uptimeMillis())
                        val up = waitForUpOrCancellation()
                        classifier.onUp(up?.uptimeMillis ?: SystemClock.uptimeMillis())
                    } else {
                        classifier.onUp(releasedAt)
                    }
                    pressed = false
                }
            },
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val stroke = 10.dp.toPx()
            val inset = stroke / 2
            val arcSize = Size(size.width - stroke, size.height - stroke)
            drawArc(trackColor, 0f, 360f, false, Offset(inset, inset), arcSize, style = Stroke(stroke))
            if (state == LoopState.Playing || state == LoopState.Overdubbing || state == LoopState.Stopped) {
                drawArc(
                    ringColor, -90f, 360f * progress.coerceIn(0f, 1f), false, Offset(inset, inset), arcSize,
                    style = Stroke(stroke, cap = StrokeCap.Round),
                )
            }
            val fill = if (enabled) colors.fill else colors.fill.copy(alpha = 0.4f)
            drawCircle(if (pressed) fill.copy(alpha = fill.alpha * 0.8f) else fill, radius = size.minDimension / 2 - stroke * 2)
        }
        // Scale the label with the button so the longest state name fits on one line.
        Text(
            text = stateText.uppercase(),
            color = colors.content,
            fontSize = (diameter.value * 0.085f).sp,
            fontWeight = FontWeight.Bold,
            textAlign = TextAlign.Center,
            maxLines = 1,
            softWrap = false,
        )
    }
}

private fun stateLabel(state: LoopState) = when (state) {
    LoopState.Idle -> R.string.state_idle
    LoopState.Recording -> R.string.state_recording
    LoopState.Playing -> R.string.state_playing
    LoopState.Overdubbing -> R.string.state_overdubbing
    LoopState.Stopped -> R.string.state_stopped
}

private fun tapActionLabel(state: LoopState) = when (state) {
    LoopState.Idle -> R.string.action_record
    LoopState.Recording -> R.string.action_close_loop
    LoopState.Playing -> R.string.action_overdub
    LoopState.Overdubbing -> R.string.action_finish_overdub
    LoopState.Stopped -> R.string.action_resume
}

private fun accessibilityActions(
    state: LoopState,
    undo: String,
    stop: String,
    clear: String,
    send: (LooperCommand) -> Unit,
): List<CustomAccessibilityAction> {
    fun action(label: String, command: LooperCommand) = CustomAccessibilityAction(label) { send(command); true }
    return when (state) {
        LoopState.Idle -> emptyList()
        LoopState.Recording -> listOf(action(stop, LooperCommand.Stop), action(clear, LooperCommand.Clear))
        LoopState.Playing, LoopState.Overdubbing -> listOf(
            action(undo, LooperCommand.Undo),
            action(stop, LooperCommand.Stop),
            action(clear, LooperCommand.Clear),
        )
        LoopState.Stopped -> listOf(action(undo, LooperCommand.Undo), action(clear, LooperCommand.Clear))
    }
}
