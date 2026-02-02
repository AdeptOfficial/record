# Ditto Looper

A real-time audio looper app for Android, inspired by the TC Electronic Ditto Looper pedal. Built with low-latency native audio using Oboe.

## Features

- **Real-time looping** - Record and playback with minimal latency
- **Layer stacking** - Overdub multiple layers on top of your loop
- **Undo support** - Remove the last layer with a hold gesture
- **Single-button interface** - All controls through tap, hold, and double-tap gestures
- **WAV export** - Save your loops to the Music folder

## Gesture Controls

| State | Tap | Hold (500ms) | Double-tap |
|-------|-----|--------------|------------|
| **Idle** | Start recording | - | - |
| **Recording** | Set loop length & play | - | - |
| **Playing** | Overdub | Undo last layer | Stop |
| **Overdubbing** | Back to play | Undo layer | Stop |
| **Stopped** | Resume play | Delete all | - |

**Double-tap + Hold** while playing/overdubbing = Delete everything

## Usage

1. **Plug in headphones** (required to avoid feedback)
2. Tap to start recording
3. Tap again to set the loop length - playback starts immediately
4. Tap to overdub more layers
5. Hold to undo a layer
6. Double-tap to stop
7. Tap "Save Loop" to export as WAV

## Building

### Requirements

- Android Studio Arctic Fox or newer
- Android NDK (for native audio)
- Android device or emulator (API 21+)

### Build from command line

```bash
# Debug build
./gradlew assembleDebug

# Install on connected device
./gradlew installDebug
```

APK output: `app/build/outputs/apk/debug/app-debug.apk`

## Architecture

- **Native C++ audio engine** using [Oboe](https://github.com/google/oboe) for low-latency I/O
- **Jetpack Compose** UI
- **Layer-based looping** with undo/redo stack
- **JNI bridge** between Kotlin and native code

## Permissions

- `RECORD_AUDIO` - Microphone access for recording
- `WRITE_EXTERNAL_STORAGE` - Saving WAV files (Android 9 and below)
- `READ_MEDIA_AUDIO` - Media access (Android 13+)

## License

MIT
