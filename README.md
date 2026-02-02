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

### Getting Started

1. **Plug in headphones** - This is required to avoid audio feedback (the mic picking up the speaker)
2. **Grant microphone permission** when prompted on first launch

### Recording Your First Loop

1. **Tap** the button to start recording (button turns red)
2. Play your beat, riff, or vocal
3. **Tap** again to set the loop length - playback starts immediately (button turns green)
4. Your loop now plays continuously

### Adding Layers (Overdubbing)

1. While playing, **tap** to enter overdub mode (button turns yellow)
2. Record additional sounds on top of your loop
3. **Tap** again to return to normal playback
4. Repeat to stack as many layers as you want

### Removing Layers (Undo)

1. While playing or overdubbing, **hold** the button for 500ms
2. The last recorded layer is removed
3. Keep holding and releasing to undo multiple layers

### Stopping and Resuming

1. **Double-tap** to stop playback (button turns blue)
2. **Tap** to resume playing from where you stopped
3. **Hold** while stopped to delete everything and start fresh

### Saving Your Loop

1. Stop playback with a double-tap
2. Tap the **"Save Loop"** button that appears
3. Your loop is saved as a WAV file in the Music folder

### Quick Reference

| Action | How | When |
|--------|-----|------|
| Start recording | Tap | When idle (gray) |
| Set loop & play | Tap | While recording (red) |
| Overdub | Tap | While playing (green) |
| Stop overdub | Tap | While overdubbing (yellow) |
| Undo layer | Hold 500ms | While playing/overdubbing |
| Stop | Double-tap | While playing/overdubbing |
| Resume | Tap | While stopped (blue) |
| Delete all | Hold 500ms | While stopped |
| Delete all (quick) | Double-tap + hold | While playing/overdubbing |

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
