package com.mawena.fluluks.engine

/**
 * Surface du moteur utilisée par le gestionnaire de volumes. [NativeEngine] l'implémente ; les tests
 * JVM fournissent un faux moteur. Même convention que NativeEngine : poignée > 0, valeur < 0 = fl_err.
 */
interface Engine {
    fun version(): String
    fun strerror(code: Int): ByteArray
    fun openImage(path: ByteArray): Long
    fun openUsb(fd: Int, iface: Int, epIn: Int, epOut: Int): Long
    fun closeDisk(disk: Long): Int
    fun scan(disk: Long): ByteArray
    fun mount(disk: Long, partIndex: Int, pass: ByteArray?): Long
    fun unmount(vol: Long, force: Boolean): Int
    fun statfs(vol: Long): ByteArray
    fun stat(vol: Long, path: ByteArray): ByteArray
    fun list(vol: Long, path: ByteArray): ByteArray
    fun openFile(vol: Long, path: ByteArray): Long
    fun pread(file: Long, offset: Long, buf: ByteArray, len: Int): Int
    fun closeFile(file: Long): Int
}
