package dev.adeptproductions.record.controller

import dev.adeptproductions.record.audio.LooperCommand

/**
 * Turns presses on the looper button into commands as they happen. Every press
 * acts on touch-down; when it turns out to be the second tap of a double tap, or a
 * hold, the engine rolls the first action back. Nothing waits for the double-tap
 * window, so loop points land where the finger did.
 */
class GestureClassifier(private val doubleTapWindowMs: Long = DOUBLE_TAP_WINDOW_MS) {
    private var lastTapUpMs: Long? = null
    private var secondPress = false
    private var held = false

    fun onDown(timeMs: Long): LooperCommand {
        val lastUp = lastTapUpMs
        secondPress = lastUp != null && timeMs - lastUp in 0..doubleTapWindowMs
        held = false
        lastTapUpMs = null
        return if (secondPress) LooperCommand.DoubleTap else LooperCommand.Tap
    }

    fun onHold(): LooperCommand {
        held = true
        return if (secondPress) LooperCommand.DoubleTapHold else LooperCommand.Hold
    }

    /** Only a short first press can start a double tap. */
    fun onUp(timeMs: Long) {
        lastTapUpMs = if (!held && !secondPress) timeMs else null
    }

    companion object {
        const val DOUBLE_TAP_WINDOW_MS = 300L
        const val HOLD_MS = 500L
    }
}
