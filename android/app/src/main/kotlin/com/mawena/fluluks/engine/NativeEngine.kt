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
object NativeEngine : Engine {
    init {
        System.loadLibrary("fluluks_jni")
    }

    /** Version du moteur, en ASCII : « fluluks 0.1.0 ». */
    external override fun version(): String

    /** Message français (UTF-8) d'un code fl_err. */
    external override fun strerror(code: Int): ByteArray

    /** Image disque (lecture seule par défaut ; écriture pour les tests). Poignée disque > 0, ou fl_err. */
    external override fun openImage(path: ByteArray, readOnly: Boolean): Long

    /**
     * Disque USB (interface déjà réclamée) : usb_bdev en lecture-écriture (LUN 0 ; MODE SENSE dit si le
     * disque est protégé) + cache 64 Kio × 256.
     * Le descripteur reste à l'appelant (UsbDeviceConnection), qui ne le ferme qu'après [closeDisk].
     */
    external override fun openUsb(fd: Int, iface: Int, epIn: Int, epOut: Int): Long

    /** Démonte de force les volumes du disque puis ferme toute la pile. 0 ou fl_err. */
    external override fun closeDisk(disk: Long): Int

    /** JSON UTF-8 de la table de partitions ([ScanResult.parse]). */
    external override fun scan(disk: Long): ByteArray

    /**
     * Ouvre la partition `partIndex` (déverrouillage LUKS si besoin), ext4 en écriture si `writable` et
     * si les règles du moteur le permettent (sinon lecture seule, raison dans [statfs]).
     * `pass` est mis à zéro dans tous les cas, succès ou erreur. Poignée volume ou fl_err.
     */
    external override fun mount(disk: Long, partIndex: Int, pass: ByteArray?, writable: Boolean): Long

    /**
     * Sans force : FL_EBUSY si un fichier est ouvert. Avec force : ferme d'abord les fichiers. Un volume en
     * écriture est synchronisé puis démonté ; disque mort : tout est libéré et l'erreur est rendue.
     */
    external override fun unmount(vol: Long, force: Boolean): Int

    /** JSON de l'espace du volume ([VolumeStat.parse]). */
    external override fun statfs(vol: Long): ByteArray

    /** JSON d'une entrée ([FsStat.parse]) ; `path` absolu en UTF-8. */
    external override fun stat(vol: Long, path: ByteArray): ByteArray

    /** JSON du contenu d'un dossier, trié par nom ([DirEntry.parseList]). */
    external override fun list(vol: Long, path: ByteArray): ByteArray

    /** Ouvre un fichier ordinaire ; `flags` : [OpenFlags]. Poignée fichier ou fl_err. */
    external override fun openFile(vol: Long, path: ByteArray, flags: Int): Long

    /** Lit au plus `len` octets (4 Mio par appel au plus) dans `buf[0..]`. Octets lus, 0 = fin, < 0 = fl_err. */
    external override fun pread(file: Long, offset: Long, buf: ByteArray, len: Int): Int

    /** Écrit `buf[0..len)` (4 Mio au plus) à `offset`. Octets écrits (= len) ou fl_err. */
    external override fun pwrite(file: Long, offset: Long, buf: ByteArray, len: Int): Int

    /** Vers le haut (trou) ou vers le bas. 0 ou fl_err. */
    external override fun truncate(file: Long, size: Long): Int

    /** 0 ou fl_err ; un fichier écrit est synchronisé (le code de la synchronisation est rendu). */
    external override fun closeFile(file: Long): Int

    /** Les chemins sont absolus, en UTF-8. 0 ou fl_err (FL_EEXIST, FL_ENOENT, FL_EROFS…). */
    external override fun mkdir(vol: Long, path: ByteArray): Int

    /** Fichier ou lien ; FL_EISDIR sur un dossier, FL_EBUSY sur un fichier ouvert. */
    external override fun unlink(vol: Long, path: ByteArray): Int

    /** Dossier vide seulement (FL_ENOTEMPTY sinon). */
    external override fun rmdir(vol: Long, path: ByteArray): Int

    /** FL_EEXIST si `to` existe (pas de remplacement), FL_EINVAL si `to` est sous `from`. */
    external override fun rename(vol: Long, from: ByteArray, to: ByteArray): Int

    /** Vide les caches du moteur et du disque (SYNCHRONIZE CACHE sur USB). 0 ou fl_err. */
    external override fun sync(vol: Long): Int

    /** Formate tout le disque (phase 8, fj_format) ; la phrase de passe est mise à zéro par le natif. */
    external override fun format(disk: Long, opts: ByteArray, pass: ByteArray?, progress: FormatProgress?): Int

    /** Archives (phase 9, fj_arc_* et fj_arcw_*) : voir [Engine]. */
    external override fun arcOpenFd(fd: Int): Long
    external override fun arcOpenFile(file: Long, size: Long): Long
    external override fun arcNext(arc: Long): ByteArray?
    external override fun arcRead(arc: Long, buf: ByteArray, len: Int): Int
    external override fun arcClose(arc: Long): Int
    external override fun arcwOpenFd(fd: Int): Long
    external override fun arcwOpenFile(file: Long): Long
    external override fun arcwAdd(arc: Long, json: ByteArray): Int
    external override fun arcwWrite(arc: Long, buf: ByteArray, len: Int): Int
    external override fun arcwClose(arc: Long, commit: Boolean): Int
}
