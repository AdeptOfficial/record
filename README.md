# Record

A one-button audio looper for Android. Record a phrase, loop it, stack overdubs,
undo them, and save the result as a WAV file. Audio runs on a low-latency native
engine (Oboe), and every touch is timestamped, so loop points land where your
finger did.

**Status:** in development (1.1.0). Not published.

## Using it

1. **Plug in headphones.** Through the speaker, the microphone records the loop as
   well. The app shows a reminder when no headphones or external output is connected.
2. **Allow the microphone** when asked. Without it the button stays disabled.

### Gestures

Every press acts the moment you touch the button, so nothing waits for the double-tap
window. If the press turns into a double tap or a hold, that gesture's action from the
table replaces the tap's (while recording, a hold keeps the loop the tap closed).

| State (colour) | Tap | Hold (½ s) | Double-tap | Double-tap and hold |
|---|---|---|---|---|
| Ready (grey) | Start recording | – | – | – |
| Recording (red) | Set the loop length and play | Same as a tap | Set the loop length and stop | Delete everything |
| Playing (green) | Start an overdub | Undo the newest layer | Stop | Delete everything |
| Overdub (amber) | Back to playing, keeping the overdub | Undo this overdub | Stop, keeping the overdub | Delete everything |
| Stopped (blue) | Resume where it stopped | Delete everything | – | Delete everything |

- Hold again to undo more layers. Up to 16 layers can be undone on loops of about
  20 seconds or less, and two on a two-minute loop. When room runs out, starting a
  new overdub folds the oldest layer into the loop, where it stays.
- A loop can be up to two minutes long. Recording switches to playback at the limit.
- Overdubs are shifted by the measured round-trip latency, so they line up with
  what you heard while playing.
- The ring around the button shows the position in the loop.

### Other controls

- **Microphone:** choose the built-in mic, a headset, a USB interface or line input.
  If the chosen input disappears, the app falls back to the default microphone.
- **Hear input:** plays the microphone through your headphones. It adds the device's
  audio latency, so leave it off for acoustic instruments.
- **Save loop:** stop the loop first. The loop is saved to `Music/Record` as a 16-bit
  mono WAV at the device's sample rate. Android 6 to 9 ask for storage access.
- **TalkBack:** the button's actions (record, overdub, undo, stop, delete) are
  available from the accessibility actions menu.

### Behaviour to know

- Rotating the phone or switching dark mode does not interrupt the audio.
- When the app leaves the screen, audio stops at that moment and the microphone is
  released. A recording in progress becomes the loop; an overdub is kept. The loop
  stays in memory until the app is closed.
- Loops are not stored between sessions. Save any loop you want to keep.

## Building

Requirements:

- JDK 17 or newer (tested with 25)
- Android SDK platform 37, NDK 30.0.16248370 and CMake 4.1.2. Gradle names them;
  install them with Android Studio's SDK Manager or the `android sdk` command-line tool.
- An Android Studio release that supports Android Gradle Plugin 9.5, if you use the IDE

```bash
./gradlew assembleDebug          # app/build/outputs/apk/debug/app-debug.apk
./gradlew installDebug           # install on a connected device
./gradlew testDebugUnitTest lintDebug
./gradlew assembleProfile        # release code with R8 shrinking, signed with the debug key
```

Release signing is not configured; a release build needs the owner's keystore.

## Tests

- **Looper core (host, no Android SDK):** `scripts/native-tests.sh` builds the engine
  core with plain CMake and runs 24 behaviour tests plus a short multi-threaded soak,
  in three builds: optimised (also checks the audio thread never allocates),
  AddressSanitizer with UBSan, and ThreadSanitizer. For a long soak run
  `build/native-none/soak_test <seconds> [seed]`.
- **Gesture classifier (JVM):** `./gradlew testDebugUnitTest`.
- **Device soak:** `scripts/device-soak.sh <minutes> [seed]` runs `LooperSoakTest` on
  a phone or emulator. It drives the button with real touches, rotations,
  backgrounding and saves, checks the engine's state after each action, and fails on
  leaked file descriptors, threads or memory. Progress is logged under `RecordSoak`.

## Architecture

- `app/src/main/cpp/core/LooperCore`: the looper itself, free of Android and Oboe.
  It allocates every buffer up front and never allocates, locks or blocks while
  processing audio. Commands carry the time of the touch and take effect at the
  matching audio frame. Overdub layers live in one preallocated block; when they run
  out, the oldest layer is merged into the loop a few blocks at a time while the new
  overdub's audio waits in the input history.
- `app/src/main/cpp/OboeLooper`: keeps a synchronised Oboe input and output pair
  running, reopens it when a device disconnects, measures latency, and pauses loop
  changes while a save reads the loop.
- `app/src/main/cpp/jni_bridge.cpp` and `audio/LooperEngine.kt`: the JNI boundary.
- `controller/LoopController.kt`: a ViewModel tied to the app process lifecycle, so
  configuration changes keep the audio running and backgrounding stops it.
- `controller/GestureClassifier.kt` and `ui/`: touch handling and the Compose UI.

## Permissions

- `RECORD_AUDIO`: capturing loops.
- `WRITE_EXTERNAL_STORAGE` (Android 9 and older only): saving to Music.

## Known limitations

- No background looping: there is no foreground service.
- Targets API 36, Google Play's minimum for 2026. Move to 37 after testing on Android 17.
- The launcher icon is a neutral placeholder until the client's artwork is available.
- Verified on an Android 16 emulator, whose microphone is silent. Latency, audio
  quality and Bluetooth/USB routing still need a check on real phones.

## License

MIT. See [LICENSE](LICENSE).

## // codesign

<img src="https://raw.githubusercontent.com/AdeptOfficial/AdeptOfficial/main/assets/codesign-typing.svg" alt="// code-signed by AdepT_X · stay sad and dead <3">

<p align="right"><img src="https://raw.githubusercontent.com/AdeptOfficial/AdeptOfficial/main/assets/codesign.svg" alt="// code-signed by AdepT_X · stay sad and dead <3"></p>
