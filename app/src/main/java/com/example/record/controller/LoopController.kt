package com.example.record.controller

import android.media.AudioDeviceInfo
import com.example.record.audio.AudioEngine
import com.example.record.ui.controls.LooperGesture
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope

class LoopController : ViewModel() {
    private val _loopState = MutableStateFlow<LoopState>(LoopState.Idle)
    val loopState: StateFlow<LoopState> = _loopState.asStateFlow()

    private val _inputLevel = MutableStateFlow(0f)
    val inputLevel: StateFlow<Float> = _inputLevel.asStateFlow()

    private val audioEngine by lazy { AudioEngine() }

    init {
        viewModelScope.launch {
            loopState.collect { state ->
                audioEngine.setLoopState(state)
            }
        }

        viewModelScope.launch {
            while (true) {
                _inputLevel.value = audioEngine.getInputLevel()
                delay(100)
            }
        }
    }

    fun onGesture(gesture: LooperGesture) {
        when (gesture) {
            LooperGesture.Tap -> onTap()
            LooperGesture.Hold -> onHold()
            LooperGesture.DoubleTap -> onDoubleTap()
            LooperGesture.DoubleTapHold -> onDoubleTapHold()
        }
    }

    private fun onTap() {
        when (_loopState.value) {
            LoopState.Idle -> {
                audioEngine.start()
                _loopState.value = LoopState.Recording
            }
            LoopState.Recording -> {
                _loopState.value = LoopState.Playing
            }
            LoopState.Playing -> {
                _loopState.value = LoopState.Overdubbing
            }
            LoopState.Overdubbing -> {
                _loopState.value = LoopState.Playing
            }
            LoopState.Stopped -> {
                _loopState.value = LoopState.Playing
            }
        }
    }

    private fun onHold() {
        when (_loopState.value) {
            LoopState.Playing, LoopState.Overdubbing -> {
                audioEngine.undo()
            }
            LoopState.Stopped -> {
                deleteAll()
            }
            else -> { }
        }
    }

    private fun onDoubleTap() {
        when (_loopState.value) {
            LoopState.Playing, LoopState.Overdubbing -> {
                _loopState.value = LoopState.Stopped
            }
            else -> { }
        }
    }

    private fun onDoubleTapHold() {
        when (_loopState.value) {
            LoopState.Playing, LoopState.Overdubbing -> {
                deleteAll()
            }
            else -> { }
        }
    }

    private fun deleteAll() {
        audioEngine.stop()
        _loopState.value = LoopState.Idle
    }

    fun selectDevice(device: AudioDeviceInfo) {
        audioEngine.setDeviceId(device.id)
    }

    fun exportLoop(filePath: String): Boolean {
        return audioEngine.exportToFile(filePath)
    }

    override fun onCleared() {
        super.onCleared()
        audioEngine.stop()
    }
}
