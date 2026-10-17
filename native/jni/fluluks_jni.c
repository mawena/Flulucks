/* Pont JNI du moteur FluLuks (version minimale ; les autres entrées arrivent en T6). */
#include <jni.h>

#define FL_JNI_VERSION "fluluks 0.1.0"

JNIEXPORT jstring JNICALL
Java_com_mawena_fluluks_engine_NativeEngine_version(JNIEnv *env, jclass clazz) {
    (void)clazz;
    /* Chaîne ASCII pure : NewStringUTF est sûr ici. */
    return (*env)->NewStringUTF(env, FL_JNI_VERSION);
}
