package dev.adeptproductions.recorder.audio

import android.content.Context
import android.media.MediaRecorder
import android.os.Build
import android.util.Log
import java.io.File

class AudioRecorder(private val context: Context) {

    private var recorder: MediaRecorder? = null
    private var isRecording = false

    fun startRecording(
        filePath: String,
        onError: ((Throwable) -> Unit)? = null
    ) {
        val outputFile = File(filePath)
        if (outputFile.exists()) outputFile.delete()

        val mediaRecorder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            MediaRecorder(context)
        } else {
            @Suppress("DEPRECATION")
            MediaRecorder()
        }

        runCatching {
            mediaRecorder.apply {
                setAudioSource(MediaRecorder.AudioSource.MIC)
                setOutputFormat(MediaRecorder.OutputFormat.MPEG_4)
                setAudioEncoder(MediaRecorder.AudioEncoder.AAC)
                setAudioEncodingBitRate(128_000)
                setAudioSamplingRate(44100)
                setOutputFile(outputFile.absolutePath)

                prepare()
                start()
            }

            recorder = mediaRecorder
            isRecording = true
            Log.i("AudioRecorder", "Recording started: $filePath")
        }.onFailure {
            Log.e("AudioRecorder", "Recording failed: ${it.localizedMessage}")
            mediaRecorder.release()
            recorder = null
            isRecording = false
            onError?.invoke(it)
        }
    }

    fun stopRecording() {
        recorder?.let {
            runCatching {
                it.stop()
            }.onFailure {
                Log.e("AudioRecorder", "Stop failed: ${it.localizedMessage}")
            }.also {
                recorder?.release()
                recorder = null
                isRecording = false
                Log.i("AudioRecorder", "Recording stopped.")
            }
        }
    }

    fun isRecording(): Boolean = isRecording
}
