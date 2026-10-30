package dev.adeptproductions.record.controller

import dev.adeptproductions.record.audio.LooperCommand
import org.junit.Assert.assertEquals
import org.junit.Test

class GestureClassifierTest {
    private val classifier = GestureClassifier()

    private fun tap(downMs: Long, upMs: Long = downMs + 60): LooperCommand =
        classifier.onDown(downMs).also { classifier.onUp(upMs) }

    @Test
    fun everyPressActsOnTouchDown() {
        assertEquals(LooperCommand.Tap, classifier.onDown(1_000))
    }

    @Test
    fun secondPressInsideTheWindowIsADoubleTap() {
        tap(1_000)
        assertEquals(LooperCommand.DoubleTap, tap(1_200))
    }

    @Test
    fun windowCountsFromTheRelease() {
        tap(1_000, upMs = 1_450)  // long, but not a hold
        assertEquals(LooperCommand.DoubleTap, tap(1_700))
    }

    @Test
    fun secondPressAfterTheWindowIsANewTap() {
        tap(1_000)
        assertEquals(LooperCommand.Tap, tap(1_400))
    }

    @Test
    fun aThirdQuickTapStartsAgain() {
        tap(1_000)
        tap(1_150)
        assertEquals(LooperCommand.Tap, tap(1_300))
    }

    @Test
    fun heldFirstPressIsAHold() {
        assertEquals(LooperCommand.Tap, classifier.onDown(1_000))
        assertEquals(LooperCommand.Hold, classifier.onHold())
        classifier.onUp(1_800)
        assertEquals(LooperCommand.Tap, tap(1_900))  // a hold never starts a double tap
    }

    @Test
    fun heldSecondPressIsADoubleTapHold() {
        tap(1_000)
        assertEquals(LooperCommand.DoubleTap, classifier.onDown(1_200))
        assertEquals(LooperCommand.DoubleTapHold, classifier.onHold())
        classifier.onUp(1_900)
        assertEquals(LooperCommand.Tap, tap(2_000))
    }
}
