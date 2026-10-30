package dev.adeptproductions.record.audio

/** JNI entry points of librecord_engine.so (jni_bridge.cpp). Use [LooperEngine] instead. */
internal object NativeBridge {
    init {
        System.loadLibrary("record_engine")
    }

    @JvmStatic external fun nativeCreate(sampleRate: Int, maxLoopSeconds: Double): Long
    @JvmStatic external fun nativeDestroy(handle: Long)
    @JvmStatic external fun nativeStart(handle: Long): Int
    @JvmStatic external fun nativeStop(handle: Long)
    @JvmStatic external fun nativeSetInputDevice(handle: Long, deviceId: Int): Int
    @JvmStatic external fun nativeSetMonitoring(handle: Long, enabled: Boolean)
    @JvmStatic external fun nativePost(handle: Long, command: Int, eventTimeNs: Long): Boolean
    @JvmStatic external fun nativeStatus(handle: Long, out: LongArray)
    @JvmStatic external fun nativeMaintain(handle: Long): Double
    @JvmStatic external fun nativeExportWav(handle: Long, fd: Int): Int
}
