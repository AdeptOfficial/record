package dev.adeptproductions.record.audio

import android.Manifest
import android.content.ContentValues
import android.content.Context
import android.content.pm.PackageManager
import android.media.MediaScannerConnection
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.ParcelFileDescriptor
import android.provider.MediaStore
import androidx.annotation.RequiresApi
import androidx.core.content.ContextCompat
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

sealed interface SaveResult {
    data class Saved(val displayName: String, val uri: Uri) : SaveResult
    data object NoLoop : SaveResult
    data object Busy : SaveResult
    data object NeedsPermission : SaveResult
    data object Failed : SaveResult
}

/**
 * Saves the mixed loop to Music/Record as a 16-bit WAV. Android 10+ goes through
 * MediaStore and needs no permission; Android 6–9 needs WRITE_EXTERNAL_STORAGE.
 * Blocking: call from a background thread.
 */
class LoopSaver(private val context: Context) {

    fun save(engine: LooperEngine): SaveResult {
        val name = "loop_" + SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date()) + ".wav"
        return if (Build.VERSION.SDK_INT >= 29) saveToMediaStore(engine, name) else saveToFile(engine, name)
    }

    @RequiresApi(29)
    private fun saveToMediaStore(engine: LooperEngine, name: String): SaveResult {
        val resolver = context.contentResolver
        val values = ContentValues().apply {
            put(MediaStore.Audio.Media.DISPLAY_NAME, name)
            put(MediaStore.Audio.Media.MIME_TYPE, MIME_TYPE)
            put(MediaStore.Audio.Media.RELATIVE_PATH, "${Environment.DIRECTORY_MUSIC}/$FOLDER")
            put(MediaStore.Audio.Media.IS_PENDING, 1)
        }
        val collection = MediaStore.Audio.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
        val uri = resolver.insert(collection, values) ?: return SaveResult.Failed
        val failure = try {
            val descriptor = resolver.openFileDescriptor(uri, "w")
            if (descriptor == null) SaveResult.Failed else descriptor.use { failureOf(engine.exportWav(it.fd)) }
        } catch (e: Exception) {
            SaveResult.Failed
        }
        if (failure != null) {
            resolver.delete(uri, null, null)
            return failure
        }
        values.clear()
        values.put(MediaStore.Audio.Media.IS_PENDING, 0)
        resolver.update(uri, values, null, null)
        return SaveResult.Saved(name, uri)
    }

    @Suppress("DEPRECATION") // public directories are the API on Android 9 and older
    private fun saveToFile(engine: LooperEngine, name: String): SaveResult {
        if (needsPermission(context)) return SaveResult.NeedsPermission
        val dir = File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MUSIC), FOLDER)
        if (!dir.isDirectory && !dir.mkdirs()) return SaveResult.Failed
        val file = File(dir, name)
        val failure = try {
            ParcelFileDescriptor.open(
                file,
                ParcelFileDescriptor.MODE_WRITE_ONLY or ParcelFileDescriptor.MODE_CREATE or
                    ParcelFileDescriptor.MODE_TRUNCATE,
            ).use { failureOf(engine.exportWav(it.fd)) }
        } catch (e: Exception) {
            SaveResult.Failed
        }
        if (failure != null) {
            file.delete()
            return failure
        }
        MediaScannerConnection.scanFile(context, arrayOf(file.path), arrayOf(MIME_TYPE), null)
        return SaveResult.Saved(name, Uri.fromFile(file))
    }

    private fun failureOf(export: LooperEngine.ExportResult): SaveResult? = when (export) {
        LooperEngine.ExportResult.Ok -> null
        LooperEngine.ExportResult.NoLoop -> SaveResult.NoLoop
        LooperEngine.ExportResult.Busy -> SaveResult.Busy
        LooperEngine.ExportResult.IoError, LooperEngine.ExportResult.Timeout -> SaveResult.Failed
    }

    companion object {
        const val FOLDER = "Record"
        private const val MIME_TYPE = "audio/x-wav"

        fun needsPermission(context: Context): Boolean =
            Build.VERSION.SDK_INT < 29 && ContextCompat.checkSelfPermission(
                context, Manifest.permission.WRITE_EXTERNAL_STORAGE
            ) != PackageManager.PERMISSION_GRANTED
    }
}
