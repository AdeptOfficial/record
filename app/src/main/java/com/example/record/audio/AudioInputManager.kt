package com.example.record.audio

import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioManager

class AudioInputManager(private val context: Context) {

    // Note: Requires RECORD_AUDIO permission
    fun getAvailableInputDevices(): List<AudioDeviceInfo> {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        val devices = audioManager.getDevices(AudioManager.GET_DEVICES_INPUTS)
        return devices.toList()
    }
}
