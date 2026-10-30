# Diary

## 2026-10-30: Looper engine rebuilt and soak-tested (1.1.0)

Rebuilt the engine as a real-time-safe core on Oboe full duplex (timestamped touches,
latency-compensated overdubs, queued layer merges, reconnects); fixed the undo crash,
late taps, the engine orphaned by rotation, and saving. Release 76f00c5.
Verified: 24 core tests in 3 builds, 3 × 60 min host soaks (~848 h audio), a 100 min
emulator soak (5,846 gestures), all with 0 failures; screenshots in docs/screenshots.
Not verified on real phones (latency, audio quality, Bluetooth/USB routing).
Next: test on a phone with headphones; client launcher art; release keystore.
