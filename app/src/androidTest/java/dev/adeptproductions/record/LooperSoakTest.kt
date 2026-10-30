package dev.adeptproductions.record

import android.Manifest
import android.accessibilityservice.AccessibilityService
import android.app.UiAutomation
import android.content.ContentUris
import android.content.Intent
import android.graphics.PointF
import android.os.Debug
import android.os.SystemClock
import android.provider.MediaStore
import android.util.Log
import android.view.InputDevice
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import androidx.compose.ui.platform.ViewRootForTest
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.getAllSemanticsNodes
import androidx.compose.ui.semantics.getOrNull
import androidx.lifecycle.ViewModelProvider
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import dev.adeptproductions.record.audio.EngineStatus
import dev.adeptproductions.record.audio.LoopState
import dev.adeptproductions.record.audio.SaveResult
import dev.adeptproductions.record.controller.LoopController
import dev.adeptproductions.record.ui.controls.LOOPER_BUTTON_TAG
import java.io.File
import kotlin.random.Random
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Drives the real app with injected touches for a long time and checks that the
 * engine stays consistent and nothing leaks. Duration and seed come from
 * instrumentation arguments:
 *
 *   adb shell am instrument -w -e soakMinutes 120 -e soakSeed 1 \
 *     -e class dev.adeptproductions.record.LooperSoakTest \
 *     dev.adeptproductions.record.test/androidx.test.runner.AndroidJUnitRunner
 *
 * Progress is logged under the RecordSoak tag.
 */
@RunWith(AndroidJUnit4::class)
class LooperSoakTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val automation: UiAutomation = instrumentation.uiAutomation
    private val arguments = InstrumentationRegistry.getArguments()
    private val minutes = arguments.getString("soakMinutes")?.toDoubleOrNull() ?: 2.0
    private val seed = arguments.getString("soakSeed")?.toLongOrNull() ?: System.nanoTime()
    private val random = Random(seed)
    private val failures = mutableListOf<String>()
    private val counts = sortedMapOf<String, Int>()
    private var rotated = false

    @Test
    fun soak() {
        automation.grantRuntimePermission(context.packageName, Manifest.permission.RECORD_AUDIO)
        log("start: $minutes min, seed $seed")
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            val controller = onActivity(scenario) { ViewModelProvider(it)[LoopController::class.java] }
            waitFor(10_000) { controller.state.value.status.running }
            assertTrue("engine did not start: ${controller.state.value}", controller.state.value.status.running)

            val start = SystemClock.uptimeMillis()
            val deadline = start + (minutes * 60_000).toLong()
            var nextReport = start + 60_000
            var warmSample: ResourceSample? = null
            var center = buttonCenter(scenario)
            while (SystemClock.uptimeMillis() < deadline && failures.size < 50) {
                val roll = random.nextInt(100)
                when {
                    roll < 34 -> act("tap") { tap(center) }
                    roll < 44 -> act("doubleTap") { tap(center); pause(80, 220); tap(center) }
                    roll < 52 -> act("hold") { press(center, 700) }
                    roll < 55 -> act("doubleTapHold") { tap(center); pause(80, 200); press(center, 700) }
                    roll < 77 -> act("wait") { pause(200, 6_000) }
                    roll < 82 -> act("rotate") { rotate(scenario, controller); center = buttonCenter(scenario) }
                    roll < 86 -> act("background") { background(controller); center = buttonCenter(scenario) }
                    roll < 92 -> act("save") { save(controller) }
                    roll < 97 -> act("monitor") { instrumentation.runOnMainSync { controller.setMonitoring(random.nextBoolean()) } }
                    else -> act("reselectInput") { instrumentation.runOnMainSync { controller.selectInputDevice(0) } }
                }
                checkStatus(controller.state.value.status, foreground = true)
                val now = SystemClock.uptimeMillis()
                if (now >= nextReport) {
                    val sample = ResourceSample.take()
                    if (warmSample == null && now - start >= 120_000) warmSample = sample
                    report(now - start, controller.state.value.status, sample)
                    nextReport = now + 60_000
                }
            }
            automation.setRotation(UiAutomation.ROTATION_UNFREEZE)

            val final = ResourceSample.take()
            report(SystemClock.uptimeMillis() - start, controller.state.value.status, final)
            warmSample?.let { warm ->
                // The loop arena is allocated once at start-up; nothing should grow after warm-up.
                if (final.fds - warm.fds > 20) fail("file descriptors grew ${warm.fds} -> ${final.fds}")
                if (final.threads - warm.threads > 10) fail("threads grew ${warm.threads} -> ${final.threads}")
                if (final.nativeHeapMb - warm.nativeHeapMb > 32) {
                    fail("native heap grew ${warm.nativeHeapMb} -> ${final.nativeHeapMb} MB")
                }
                if (final.javaHeapMb - warm.javaHeapMb > 32) fail("Java heap grew ${warm.javaHeapMb} -> ${final.javaHeapMb} MB")
            }
        }
        log("actions: $counts")
        log(if (failures.isEmpty()) "RESULT PASS" else "RESULT FAIL (${failures.size}): ${failures.take(10)}")
        assertTrue("soak failures: ${failures.take(10)}", failures.isEmpty())
    }

    private inline fun act(name: String, block: () -> Unit) {
        counts[name] = (counts[name] ?: 0) + 1
        block()
    }

    private fun checkStatus(s: EngineStatus, foreground: Boolean) {
        if (foreground && !s.running) fail("engine not running in the foreground (lastError ${s.lastError})")
        if (s.loopFrames < 0 || s.loopFrames > s.maxLoopFrames) fail("loop length ${s.loopFrames} out of range")
        if (s.hasLoop && s.positionFrames !in 0 until s.loopFrames) fail("position ${s.positionFrames} outside loop ${s.loopFrames}")
        if (s.state == LoopState.Idle && s.hasLoop) fail("idle with a loop")
        if (s.state in setOf(LoopState.Playing, LoopState.Overdubbing, LoopState.Stopped) && !s.hasLoop) {
            fail("${s.state} without a loop")
        }
        if (s.layers !in 0..20) fail("layer count ${s.layers}")  // 16 slots plus overdubs waiting for one
    }

    private fun rotate(scenario: ActivityScenario<MainActivity>, controller: LoopController) {
        val before = controller.state.value.status
        rotated = !rotated
        automation.setRotation(if (rotated) UiAutomation.ROTATION_FREEZE_90 else UiAutomation.ROTATION_FREEZE_0)
        SystemClock.sleep(1_500)
        val after = onActivity(scenario) { ViewModelProvider(it)[LoopController::class.java] }
        if (after !== controller) fail("rotation created a second looper")
        val now = controller.state.value.status
        if (!now.running) fail("rotation stopped the audio")
        if (before.hasLoop && now.loopFrames != before.loopFrames && now.state != LoopState.Idle) {
            // A pending gesture may legitimately change the loop; a lost loop may not.
            log("loop changed across rotation: ${before.loopFrames} -> ${now.loopFrames}")
        }
    }

    private fun background(controller: LoopController) {
        val loopBefore = controller.state.value.status.loopFrames
        automation.performGlobalAction(AccessibilityService.GLOBAL_ACTION_HOME)
        if (!waitFor(5_000) { !controller.state.value.status.running }) fail("audio kept running in the background")
        val parked = controller.state.value.status
        if (parked.state == LoopState.Recording || parked.state == LoopState.Playing || parked.state == LoopState.Overdubbing) {
            fail("background left the looper ${parked.state}")
        }
        if (loopBefore > 0 && parked.loopFrames == 0L) fail("background lost the loop")
        SystemClock.sleep(random.nextLong(500, 3_000))
        context.startActivity(
            Intent(context, MainActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT)
        )
        if (!waitFor(10_000) { controller.state.value.status.running }) fail("audio did not restart in the foreground")
        SystemClock.sleep(500)
    }

    private fun save(controller: LoopController) {
        val before = controller.state.value.status
        val result = runBlocking { controller.saveLoopNow() }
        when (result) {
            is SaveResult.Saved -> {
                val expected = 44 + before.loopFrames * 2
                val size = savedSize(result)
                if (before.state != LoopState.Stopped) log("saved while ${before.state}")
                if (size != expected) fail("saved ${result.displayName}: $size bytes, expected $expected")
                context.contentResolver.delete(result.uri, null, null)
            }
            SaveResult.NoLoop -> if (before.hasLoop) fail("save said no loop with a ${before.loopFrames}-frame loop")
            SaveResult.Busy -> if (before.state == LoopState.Stopped) fail("save busy while stopped")
            SaveResult.NeedsPermission, SaveResult.Failed -> fail("save failed: $result")
        }
    }

    private fun savedSize(saved: SaveResult.Saved): Long {
        val id = ContentUris.parseId(saved.uri)
        context.contentResolver.query(
            MediaStore.Audio.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY),
            arrayOf(MediaStore.Audio.Media.SIZE), "${MediaStore.Audio.Media._ID}=?", arrayOf(id.toString()), null,
        )?.use { if (it.moveToFirst()) return it.getLong(0) }
        return -1
    }

    private fun tap(at: PointF) = press(at, random.nextLong(40, 120))

    private fun press(at: PointF, durationMs: Long) {
        val down = SystemClock.uptimeMillis()
        inject(down, down, MotionEvent.ACTION_DOWN, at)
        SystemClock.sleep(durationMs)
        inject(down, SystemClock.uptimeMillis(), MotionEvent.ACTION_UP, at)
    }

    private fun inject(downTime: Long, eventTime: Long, action: Int, at: PointF) {
        val event = MotionEvent.obtain(downTime, eventTime, action, at.x, at.y, 0)
        event.source = InputDevice.SOURCE_TOUCHSCREEN
        automation.injectInputEvent(event, true)
        event.recycle()
    }

    private fun pause(minMs: Long, maxMs: Long) = SystemClock.sleep(random.nextLong(minMs, maxMs))

    private fun buttonCenter(scenario: ActivityScenario<MainActivity>): PointF {
        repeat(50) {
            val center = onActivity(scenario) { activity ->
                val root = findComposeRoot(activity.findViewById(android.R.id.content))
                val node = root?.semanticsOwner?.getAllSemanticsNodes(mergingEnabled = true)
                    ?.firstOrNull { it.config.getOrNull(SemanticsProperties.TestTag) == LOOPER_BUTTON_TAG }
                if (root == null || node == null) {
                    null
                } else {
                    val origin = IntArray(2)
                    root.view.getLocationOnScreen(origin)
                    val bounds = node.boundsInRoot
                    PointF(origin[0] + bounds.center.x, origin[1] + bounds.center.y)
                }
            }
            if (center != null) return center
            SystemClock.sleep(100)
        }
        error("looper button not found")
    }

    private fun findComposeRoot(view: View): ViewRootForTest? {
        if (view is ViewRootForTest) return view
        if (view is ViewGroup) {
            for (i in 0 until view.childCount) findComposeRoot(view.getChildAt(i))?.let { return it }
        }
        return null
    }

    private fun <T> onActivity(scenario: ActivityScenario<MainActivity>, block: (MainActivity) -> T): T {
        var result: Result<T>? = null
        scenario.onActivity { result = runCatching { block(it) } }
        return result!!.getOrThrow()
    }

    private fun waitFor(timeoutMs: Long, condition: () -> Boolean): Boolean {
        val end = SystemClock.uptimeMillis() + timeoutMs
        while (SystemClock.uptimeMillis() < end) {
            if (condition()) return true
            SystemClock.sleep(50)
        }
        return condition()
    }

    private fun report(elapsedMs: Long, s: EngineStatus, r: ResourceSample) {
        log(
            "t=${elapsedMs / 60_000}m state=${s.state} loop=${"%.2f".format(s.loopSeconds)}s layers=${s.layers} " +
                "xruns=${s.xruns} streamErrors=${s.errorCount} roundTrip=${"%.1f".format(s.roundTripMs)}ms " +
                "nativeHeap=${r.nativeHeapMb}MB javaHeap=${r.javaHeapMb}MB pss=${r.pssMb}MB " +
                "threads=${r.threads} fds=${r.fds} failures=${failures.size} actions=${counts.values.sum()}"
        )
    }

    private fun fail(message: String) {
        failures += message
        log("FAILURE: $message")
    }

    private fun log(message: String) {
        Log.i(TAG, message)
    }

    private data class ResourceSample(
        val nativeHeapMb: Long,
        val javaHeapMb: Long,
        val pssMb: Long,
        val threads: Int,
        val fds: Int,
    ) {
        companion object {
            fun take(): ResourceSample {
                Runtime.getRuntime().gc()
                val runtime = Runtime.getRuntime()
                return ResourceSample(
                    nativeHeapMb = Debug.getNativeHeapAllocatedSize() / MB,
                    javaHeapMb = (runtime.totalMemory() - runtime.freeMemory()) / MB,
                    pssMb = Debug.getPss() / 1024,
                    threads = File("/proc/self/task").list()?.size ?: -1,
                    fds = File("/proc/self/fd").list()?.size ?: -1,
                )
            }
        }
    }

    private companion object {
        const val TAG = "RecordSoak"
        const val MB = 1024L * 1024L
    }
}
