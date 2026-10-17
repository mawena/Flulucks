package com.mawena.fluluks.engine

/** Pont JNI vers le moteur natif (libfluluks_jni.so). */
object NativeEngine {
    init {
        System.loadLibrary("fluluks_jni")
    }

    /** Version du moteur, en ASCII : « fluluks 0.1.0 ». */
    @JvmStatic
    external fun version(): String
}
