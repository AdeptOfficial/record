package dev.adeptproductions.record.audio

/** Looper states. Codes match `looper::State` in LooperCore.h. */
enum class LoopState(val code: Int) {
    Idle(0),
    Recording(1),
    Playing(2),
    Overdubbing(3),
    Stopped(4);

    companion object {
        fun fromCode(code: Int): LoopState = entries.firstOrNull { it.code == code } ?: Idle
    }
}

/** Commands for the engine. Codes match `looper::Command` in LooperCore.h. */
enum class LooperCommand(val code: Int) {
    /** First press of any gesture; acts at once. */
    Tap(0),

    /** Second press within the double-tap window; rolls the first tap back, then stops. */
    DoubleTap(1),

    /** First press held; rolls its tap back, then undoes or deletes. */
    Hold(2),

    /** Second press held; deletes everything. */
    DoubleTapHold(3),

    // Direct actions for accessibility services.
    Undo(4),
    Stop(5),
    Clear(6),
}
