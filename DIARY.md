# Diary

## 2026-10-30: Looper engine rebuilt and soak-tested (1.1.0)

- **What changed:**
  - **Engine:** replaced with a platform-free, real-time-safe core: touch-timestamped commands, latency-compensated overdubs, preallocated layers with background merges.
  - **Audio I/O:** Oboe full-duplex streams that reconnect after device changes.
  - **App:** a ViewModel on the process lifecycle, saving through MediaStore, and a landscape layout.
  - **Identity and build:** AGP 9.5 / Kotlin 2.4, targetSdk 36, app ID `dev.adeptproductions.record`, MIT licence.
- **Bugs fixed:** crash on repeated undo, playhead jump, short overdubs repeating, doubled monitoring, 300 ms late taps, rotation orphaning a second engine, saving on Android 6–10, missing pre-API-26 icon, non-executable `gradlew`. An independent review's eight findings are fixed as well.
- **Verified:**
  - 24 core tests in optimised, ASan/UBSan and TSan builds, all passing.
  - Host soaks of 60 min per build: about 848 h of simulated audio with 0 failures.
  - Android 16 emulator soak (silent mic): 100 min, 5,846 gestures, 0 failures, 0 xruns, 0 crashes.
  - Screenshots in `docs/screenshots/`.
- **Not verified:** real phones (latency, audio quality, Bluetooth/USB routing).
- **Next:** test on a real phone with headphones; get the client's launcher art; set up a release keystore before any store upload.
