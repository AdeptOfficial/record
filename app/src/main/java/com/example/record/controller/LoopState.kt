package com.example.record.controller

sealed class LoopState {
    object Idle : LoopState()
    object Recording : LoopState()
    object Playing : LoopState()
    object Overdubbing : LoopState()
    object Stopped : LoopState()
}
