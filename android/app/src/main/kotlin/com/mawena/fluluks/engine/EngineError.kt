package com.mawena.fluluks.engine

/**
 * Erreur du moteur : `code` est un fl_err (< 0). Le message par défaut est celui de `fl_strerror`
 * (français). Ne contient jamais de phrase de passe.
 */
class EngineError(val code: Int, message: String = defaultMessage(code)) : Exception(message) {
    companion object {
        const val FL_EIO = -1
        const val FL_EINVAL = -2
        const val FL_ENOMEM = -3
        const val FL_ENOTSUP = -4
        const val FL_EBADPASS = -5
        const val FL_ECORRUPT = -6
        const val FL_EROFS = -7
        const val FL_ENOENT = -8
        const val FL_EEXIST = -9
        const val FL_ENOTDIR = -10
        const val FL_EISDIR = -11
        const val FL_ENOTEMPTY = -12
        const val FL_ENOSPC = -13
        const val FL_ENODEV = -14
        const val FL_ETIMEDOUT = -15
        const val FL_ESTALL = -16
        const val FL_EBUSY = -17

        /** Message de fl_strerror ; repli neutre si la bibliothèque native est absente (tests JVM). */
        fun defaultMessage(code: Int): String =
            try {
                String(NativeEngine.strerror(code), Charsets.UTF_8)
            } catch (e: LinkageError) { // UnsatisfiedLinkError, ExceptionInInitializerError, NoClassDefFoundError
                "erreur $code"
            }

        /** Lève EngineError si `rc` < 0 ; sinon le rend. */
        fun check(rc: Long): Long = if (rc < 0) throw EngineError(rc.toInt()) else rc

        fun check(rc: Int): Int = if (rc < 0) throw EngineError(rc) else rc
    }
}
