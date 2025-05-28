package dev.adeptproductions.recorder.audio

import android.content.Context
import android.media.MediaPlayer
import android.util.Log
import java.io.File

class AudioPlayer(private val context: Context) {
    private var mediaPlayer: MediaPlayer? = null

    /**
     * Play an audio file.
     *
     * @param filePath Path to the audio file.
     * @param loop Whether the audio should loop.
     * @param onComplete Callback when playback ends (only if not looping).
     */
    fun play(filePath: String, loop: Boolean = false, onComplete: (() -> Unit)? = null) {
        stop()

        val audioFile = File(filePath)
        if (!audioFile.exists() || audioFile.length() == 0L) {
            Log.e("AudioPlayer", "❌ File not found or empty: $filePath")
            return
        }

        mediaPlayer = MediaPlayer().apply {
            try {
                setDataSource(filePath)
                isLooping = loop
                setOnPreparedListener { start() }
                setOnCompletionListener {
                    if (!loop) {
                        onComplete?.invoke()
                        stop()
                    }
                }
                prepareAsync()
            } catch (e: Exception) {
                Log.e("AudioPlayer", "❌ Playback error: ${e.message}")
                release()
            }
        }
    }

    /** Stop playback and release resources. */
    fun stop() {
        mediaPlayer?.apply {
            if (isPlaying) stop()
            release()
        }
        mediaPlayer = null
    }

    /** Returns true if currently playing. */
    fun isPlaying(): Boolean = mediaPlayer?.isPlaying == true
}
