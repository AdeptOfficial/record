package dev.adeptproductions.recorder.audio

import android.content.Context
import android.media.MediaPlayer
import java.io.File

class AudioPlayer(private val context: Context) {
    private var mediaPlayer: MediaPlayer? = null

    fun play(filePath: String, onComplete: (() -> Unit)? = null) {
        stop() // Stop any previous playback

        if (!File(filePath).exists()) return

        mediaPlayer = MediaPlayer().apply {
            setDataSource(filePath)
            setOnCompletionListener {
                onComplete?.invoke()
                stop()
            }
            prepare()
            start()
        }
    }

    fun stop() {
        mediaPlayer?.apply {
            if (isPlaying) stop()
            release()
        }
        mediaPlayer = null
    }

    fun isPlaying(): Boolean = mediaPlayer?.isPlaying == true
}
