package com.example.record.audio

import com.example.record.controller.LoopState

class AudioEngine {

    private var nativeEngineHandle: Long = 0

    fun start() {
        nativeEngineHandle = native_create()
        native_start(nativeEngineHandle)
    }

    fun stop() {
        if (nativeEngineHandle != 0L) {
            native_stop(nativeEngineHandle)
            nativeEngineHandle = 0
        }
    }

    fun setLoopState(state: LoopState) {
        if (nativeEngineHandle != 0L) {
            native_set_loop_state(nativeEngineHandle, toNativeState(state))
        }
    }

    fun setDeviceId(deviceId: Int) {
        if (nativeEngineHandle != 0L) {
            native_set_device_id(nativeEngineHandle, deviceId)
        }
    }

    fun undo() {
        if (nativeEngineHandle != 0L) {
            native_undo(nativeEngineHandle)
        }
    }

    fun redo() {
        if (nativeEngineHandle != 0L) {
            native_redo(nativeEngineHandle)
        }
    }

    fun getInputLevel(): Float {
        return if (nativeEngineHandle != 0L) {
            native_get_input_level(nativeEngineHandle)
        } else {
            0f
        }
    }

    fun exportToFile(filePath: String): Boolean {
        return if (nativeEngineHandle != 0L) {
            native_export_to_file(nativeEngineHandle, filePath)
        } else {
            false
        }
    }

    private external fun native_create(): Long
    private external fun native_start(engineHandle: Long)
    private external fun native_stop(engineHandle: Long)
    private external fun native_set_loop_state(engineHandle: Long, state: Int)
    private external fun native_set_device_id(engineHandle: Long, deviceId: Int)
    private external fun native_undo(engineHandle: Long)
    private external fun native_redo(engineHandle: Long)
    private external fun native_get_input_level(engineHandle: Long): Float
    private external fun native_export_to_file(engineHandle: Long, filePath: String): Boolean

    private fun toNativeState(state: LoopState): Int = when (state) {
        LoopState.Idle -> 0
        LoopState.Recording -> 1
        LoopState.Playing -> 2
        LoopState.Overdubbing -> 3
        LoopState.Stopped -> 4
    }


    companion object {
        init {
            System.loadLibrary("audio_engine")
        }
    }
}
