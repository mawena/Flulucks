package com.mawena.fluluks.engine

/**
 * Pont JNI vers le moteur natif (libfluluks_jni.so).
 *
 * Poignées : `Long` > 0 ; toute valeur < 0 est un code fl_err (voir [EngineError]). Une poignée
 * périmée, fermée ou d'un autre type donne `FL_EINVAL`, jamais un plantage. Tout est sûr entre fils :
 * une fermeture attend la fin des opérations en cours sur l'objet fermé.
 *
 * Chaînes : les chemins et le JSON passent en octets UTF-8 (`ByteArray`), jamais en « modified UTF-8 ».
 * Le JSON d'erreur est `{"err":<code>}` ; les modèles de [EngineModels.kt] le transforment en [EngineError].
 */
object NativeEngine {
    init {
        System.loadLibrary("fluluks_jni")
    }

    /** Version du moteur, en ASCII : « fluluks 0.1.0 ». */
    @JvmStatic external fun version(): String

    /** Message français (UTF-8) d'un code fl_err. */
    @JvmStatic external fun strerror(code: Int): ByteArray

    /** Image disque, en lecture seule. Poignée disque > 0, ou code fl_err < 0. */
    @JvmStatic external fun openImage(path: ByteArray): Long

    /**
     * Disque USB (interface déjà réclamée) : usb_bdev en lecture seule (LUN 0) + cache 64 Kio × 256.
     * Le descripteur reste à l'appelant (UsbDeviceConnection), qui ne le ferme qu'après [closeDisk].
     */
    @JvmStatic external fun openUsb(fd: Int, iface: Int, epIn: Int, epOut: Int): Long

    /** Démonte de force les volumes du disque puis ferme toute la pile. 0 ou fl_err. */
    @JvmStatic external fun closeDisk(disk: Long): Int

    /** JSON UTF-8 de la table de partitions ([ScanResult.parse]). */
    @JvmStatic external fun scan(disk: Long): ByteArray

    /**
     * Ouvre la partition `partIndex` (déverrouillage LUKS si besoin, ext4 en lecture seule).
     * `pass` est mis à zéro dans tous les cas, succès ou erreur. Poignée volume ou fl_err.
     */
    @JvmStatic external fun mount(disk: Long, partIndex: Int, pass: ByteArray?): Long

    /** Sans force : FL_EBUSY si un fichier est ouvert. Avec force : ferme d'abord les fichiers. */
    @JvmStatic external fun unmount(vol: Long, force: Boolean): Int

    /** JSON de l'espace du volume ([VolumeStat.parse]). */
    @JvmStatic external fun statfs(vol: Long): ByteArray

    /** JSON d'une entrée ([FsStat.parse]) ; `path` absolu en UTF-8. */
    @JvmStatic external fun stat(vol: Long, path: ByteArray): ByteArray

    /** JSON du contenu d'un dossier, trié par nom ([DirEntry.parseList]). */
    @JvmStatic external fun list(vol: Long, path: ByteArray): ByteArray

    /** Ouvre un fichier ordinaire en lecture. Poignée fichier ou fl_err. */
    @JvmStatic external fun openFile(vol: Long, path: ByteArray): Long

    /** Lit au plus `len` octets (4 Mio par appel au plus) dans `buf[0..]`. Octets lus, 0 = fin, < 0 = fl_err. */
    @JvmStatic external fun pread(file: Long, offset: Long, buf: ByteArray, len: Int): Int

    /** 0 ou fl_err. */
    @JvmStatic external fun closeFile(file: Long): Int
}
