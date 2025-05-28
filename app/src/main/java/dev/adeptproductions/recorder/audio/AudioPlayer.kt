package dev.adeptproductions.recorder.audio

import android.content.Context
import android.media.MediaPlayer
import java.io.File

class AudioPlayer(private val context: Context) {

    private var mediaPlayer: MediaPlayer? = null

    fun play(filePath: String, onCompletion: (() -> Unit)? = null) {
        stop() // Stop if already playing

        val file = File(filePath)
        if (!file.exists()) {
            return
        }

        mediaPlayer = MediaPlayer().apply {
            setDataSource(file.absolutePath)
            setOnPreparedListener { start() }
            setOnCompletionListener {
                onCompletion?.invoke()
                release()
            }
            prepareAsync()
        }
    }

    fun stop() {
        mediaPlayer?.apply {
            if (isPlaying) stop()
            release()
        }
        mediaPlayer = null
    }

    fun isPlaying(): Boolean {
        return mediaPlayer?.isPlaying == true
    }
}
