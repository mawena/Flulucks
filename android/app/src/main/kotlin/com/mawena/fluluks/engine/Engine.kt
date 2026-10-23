package com.mawena.fluluks.engine

/**
 * Surface du moteur utilisée par le gestionnaire de volumes. [NativeEngine] l'implémente ; les tests
 * JVM fournissent un faux moteur. Même convention que NativeEngine : poignée > 0, valeur < 0 = fl_err.
 * Par défaut, tout s'ouvre en lecture seule.
 */
interface Engine {
    fun version(): String
    fun strerror(code: Int): ByteArray
    fun openImage(path: ByteArray, readOnly: Boolean = true): Long
    fun openUsb(fd: Int, iface: Int, epIn: Int, epOut: Int): Long
    fun closeDisk(disk: Long): Int
    fun scan(disk: Long): ByteArray
    fun mount(disk: Long, partIndex: Int, pass: ByteArray?, writable: Boolean = false): Long
    fun unmount(vol: Long, force: Boolean): Int
    fun statfs(vol: Long): ByteArray
    fun stat(vol: Long, path: ByteArray): ByteArray
    fun list(vol: Long, path: ByteArray): ByteArray
    fun openFile(vol: Long, path: ByteArray, flags: Int = OpenFlags.RDONLY): Long
    fun pread(file: Long, offset: Long, buf: ByteArray, len: Int): Int
    fun pwrite(file: Long, offset: Long, buf: ByteArray, len: Int): Int
    fun truncate(file: Long, size: Long): Int
    fun closeFile(file: Long): Int
    fun mkdir(vol: Long, path: ByteArray): Int
    fun unlink(vol: Long, path: ByteArray): Int
    fun rmdir(vol: Long, path: ByteArray): Int
    fun rename(vol: Long, from: ByteArray, to: ByteArray): Int
    fun sync(vol: Long): Int

    /**
     * Formate tout le disque (phase 8) : `opts` est le JSON UTF-8 de fj_format (fs, encrypted, label,
     * kdf_mem_kib, uid, gid). Refus FL_EBUSY si un volume du disque est ouvert, FL_EROFS si le disque est
     * protégé. [progress] reçoit un pourcentage croissant depuis le fil appelant. 0 ou fl_err.
     */
    fun format(disk: Long, opts: ByteArray, pass: ByteArray?, progress: FormatProgress?): Int = EngineError.FL_ENOTSUP

    /*
     * Archives (phase 9) : lecture de ZIP, 7z, RAR, TAR, GZ, XZ et écriture de ZIP. La source (descripteur
     * ou poignée de fichier) appartient à l'archive dès l'appel, même en cas d'échec ; une archive sur un
     * fichier du moteur est abandonnée si le fichier, le volume ou le disque est fermé.
     */

    /** Poignée d'archive en lecture ; FL_ENOTSUP si le fichier n'est pas une archive reconnue. */
    fun arcOpenFd(fd: Int): Long = EngineError.FL_ENOTSUP.toLong()

    /** [size] : taille du fichier (stat). */
    fun arcOpenFile(file: Long, size: Long): Long = EngineError.FL_ENOTSUP.toLong()

    /**
     * JSON UTF-8 de l'entrée suivante ({"name": base64 des octets bruts du nom, "type", "size", "mtime",
     * "encrypted"}), `{"err":<code>}` en cas d'erreur, null à la fin de l'archive.
     */
    fun arcNext(arc: Long): ByteArray? = """{"err":${EngineError.FL_ENOTSUP}}""".toByteArray()

    /** Données de l'entrée courante : octets lus (4 Mio au plus), 0 à la fin de l'entrée, ou fl_err. */
    fun arcRead(arc: Long, buf: ByteArray, len: Int): Int = EngineError.FL_ENOTSUP
    fun arcClose(arc: Long): Int = EngineError.FL_ENOTSUP
    fun arcwOpenFd(fd: Int): Long = EngineError.FL_ENOTSUP.toLong()
    fun arcwOpenFile(file: Long): Long = EngineError.FL_ENOTSUP.toLong()

    /** `json` : {"name": chemin UTF-8 relatif, "type": "f"|"d", "size", "mtime", "store"}. */
    fun arcwAdd(arc: Long, json: ByteArray): Int = EngineError.FL_ENOTSUP
    fun arcwWrite(arc: Long, buf: ByteArray, len: Int): Int = EngineError.FL_ENOTSUP

    /** [commit] : termine le ZIP ; sinon abandon. Ferme la destination dans les deux cas. */
    fun arcwClose(arc: Long, commit: Boolean): Int = EngineError.FL_ENOTSUP
}

/** Progression du formatage, appelée par le moteur natif (méthode `onProgress(I)V`). */
fun interface FormatProgress {
    fun onProgress(pct: Int)
}

/** Drapeaux de [Engine.openFile], ceux de fluluks/fs.h (FL_O_*), combinables. */
object OpenFlags {
    const val RDONLY = 0
    const val RDWR = 1
    const val CREAT = 2
    const val TRUNC = 4
}
