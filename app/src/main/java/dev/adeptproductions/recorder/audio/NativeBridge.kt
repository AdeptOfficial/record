package dev.adeptproductions.recorder.audio

import android.content.Context

object NativeBridge {
    init {
        System.loadLibrary("native-lib") // 🧠 Matches the .so name defined in CMake
    }

    external fun testNativeCall()
    external fun startSuperpoweredRecording(filePath: String, sampleRate: Int)
    external fun stopSuperpoweredRecording()
}
