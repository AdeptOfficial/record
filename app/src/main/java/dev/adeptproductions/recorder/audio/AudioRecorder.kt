package dev.adeptproductions.recorder.audio

import android.content.Context
import android.media.MediaRecorder
import android.os.Build
import java.io.File

class AudioRecorder(private val context: Context) {
    private var recorder: MediaRecorder? = null

    fun startRecording(filePath: String) {
        val file = File(filePath)
        if (file.exists()) file.delete()

        recorder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            MediaRecorder(context)
        } else {
            @Suppress("DEPRECATION")
            MediaRecorder()
        }.apply {
            setAudioSource(MediaRecorder.AudioSource.MIC)
            setOutputFormat(MediaRecorder.OutputFormat.THREE_GPP)
            setAudioEncoder(MediaRecorder.AudioEncoder.AMR_NB)
            setOutputFile(filePath)
            prepare()
            start()
        }
    }

    fun stopRecording() {
        recorder?.let {
            try {
                it.stop()
            } catch (e: Exception) {
                e.printStackTrace()
            } finally {
                it.release()
                recorder = null
            }
        }
    }

    fun isRecording(): Boolean = recorder != null
}
