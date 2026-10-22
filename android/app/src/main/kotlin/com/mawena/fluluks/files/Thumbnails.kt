package com.mawena.fluluks.files

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.media.ExifInterface
import android.media.MediaMetadataRetriever
import android.os.Build
import androidx.annotation.VisibleForTesting
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.provider.MimeTypes
import com.mawena.fluluks.volumes.VolumeManager
import java.io.ByteArrayOutputStream
import java.util.concurrent.Semaphore

/** Calcul d'une vignette (JPEG) ; null si le décodage échoue. Remplacé dans les tests JVM. */
interface ThumbDecoder {
    fun image(src: FileSource, path: String, sizePx: Int): ByteArray?
    fun video(src: FileSource, path: String, sizePx: Int): ByteArray?
}

/**
 * Vignettes des photos et vidéos, gardées **en mémoire seulement** (jamais écrites sur le téléphone) :
 * cache LRU de [CACHE_BYTES], clé `source|chemin|taille|mtime|px`, vidé pour un volume à son éjection.
 * Au plus [PARALLEL] calculs à la fois.
 */
object Thumbnails {
    const val MAX_PX = 256
    const val CACHE_BYTES = 32 shl 20
    const val PARALLEL = 3
    const val QUALITY = 80

    @Volatile var decoder: ThumbDecoder = AndroidThumbDecoder
        @VisibleForTesting set

    private val permits = Semaphore(PARALLEL)
    private val cache = LinkedHashMap<String, ByteArray>(64, 0.75f, true) // ordre d'accès
    private var cacheBytes = 0L

    @VisibleForTesting
    fun clearForTests() = synchronized(cache) { cache.clear(); cacheBytes = 0 }

    @VisibleForTesting
    fun cachedBytes(): Long = synchronized(cache) { cacheBytes }

    /** JPEG d'au plus [sizePx] (≤ [MAX_PX]) de côté, ou null : type inconnu, élément absent, décodage raté. */
    fun get(sourceId: String, path: String, sizePx: Int): ByteArray? {
        val px = sizePx.coerceIn(16, MAX_PX)
        return try {
            val src = Files.source(sourceId)
            val e = src.stat(PathRules.require(path)) ?: return null
            if (e.type != FileType.FILE) return null
            val mime = MimeTypes.forName(e.name)
            val video = mime.startsWith("video/")
            if (!video && !mime.startsWith("image/")) return null
            val key = "$sourceId|${e.path}|${e.size}|${e.mtime}|$px"
            synchronized(cache) { cache[key] }?.let { return it }
            permits.acquire()
            try {
                synchronized(cache) { cache[key] }?.let { return it } // calculée pendant l'attente
                val bytes = (if (video) decoder.video(src, e.path, px) else decoder.image(src, e.path, px)) ?: return null
                put(key, bytes) { src !is DiskSource || VolumeManager.volume(sourceId) != null }
                bytes
            } finally {
                permits.release()
            }
        } catch (t: Throwable) { // OutOfMemoryError compris : pas de vignette, jamais d'échec de l'écran
            if (t is InterruptedException) Thread.currentThread().interrupt()
            null
        }
    }

    /**
     * [stillOpen] est relu sous le verrou du cache : un disque éjecté pendant le calcul (retiré des volumes
     * avant [drop]) ne laisse aucune vignette derrière lui.
     */
    private fun put(key: String, bytes: ByteArray, stillOpen: () -> Boolean) = synchronized(cache) {
        if (bytes.size > CACHE_BYTES || !stillOpen()) return@synchronized
        cache.put(key, bytes)?.let { cacheBytes -= it.size }
        cacheBytes += bytes.size
        val it = cache.entries.iterator()
        while (cacheBytes > CACHE_BYTES && it.hasNext()) {
            cacheBytes -= it.next().value.size
            it.remove()
        }
    }

    /** Oublie les vignettes d'une source (volume éjecté). */
    fun drop(sourceId: String) = synchronized(cache) {
        val it = cache.entries.iterator()
        while (it.hasNext()) {
            val e = it.next()
            if (e.key.startsWith("$sourceId|")) {
                cacheBytes -= e.value.size
                it.remove()
            }
        }
    }
}

/** BitmapFactory (deux passes, flux rouvert) et MediaMetadataRetriever. */
object AndroidThumbDecoder : ThumbDecoder {
    override fun image(src: FileSource, path: String, sizePx: Int): ByteArray? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        src.openRead(path).use { BitmapFactory.decodeStream(it, null, bounds) }
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null
        var sample = 1
        while (maxOf(bounds.outWidth, bounds.outHeight) / (sample * 2) >= sizePx) sample *= 2
        val opts = BitmapFactory.Options().apply { inSampleSize = sample }
        val bmp = src.openRead(path).use { BitmapFactory.decodeStream(it, null, opts) } ?: return null
        val rotation = try {
            src.openRead(path).use { ExifInterface(it).rotationDegrees }
        } catch (e: Exception) {
            0
        }
        return encode(bmp, sizePx, rotation)
    }

    override fun video(src: FileSource, path: String, sizePx: Int): ByteArray? {
        val r = MediaMetadataRetriever()
        var pfd: android.os.ParcelFileDescriptor? = null
        try {
            val local = (src as? LocalSource)?.localFile(path)
            if (local != null) {
                r.setDataSource(local.path)
            } else {
                val ctx = Files.context ?: return null
                pfd = ctx.contentResolver.openFileDescriptor(src.contentUri(path), "r") ?: return null
                r.setDataSource(pfd.fileDescriptor)
            }
            val opt = MediaMetadataRetriever.OPTION_CLOSEST_SYNC
            val bmp = if (Build.VERSION.SDK_INT >= 27) r.getScaledFrameAtTime(1_000_000, opt, sizePx, sizePx)
            else r.getFrameAtTime(1_000_000, opt)
            return bmp?.let { encode(it, sizePx, 0) }
        } finally {
            r.release()
            pfd?.close()
        }
    }

    private val ExifInterface.rotationDegrees: Int
        get() = when (getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL)) {
            ExifInterface.ORIENTATION_ROTATE_90 -> 90
            ExifInterface.ORIENTATION_ROTATE_180 -> 180
            ExifInterface.ORIENTATION_ROTATE_270 -> 270
            else -> 0
        }

    /** Réduit pour tenir dans [sizePx] (rotation EXIF appliquée), puis JPEG de qualité 80. */
    private fun encode(bmp: Bitmap, sizePx: Int, rotation: Int): ByteArray {
        val scale = minOf(1f, sizePx.toFloat() / maxOf(bmp.width, bmp.height))
        val m = Matrix().apply {
            postScale(scale, scale)
            if (rotation != 0) postRotate(rotation.toFloat())
        }
        val out = if (scale < 1f || rotation != 0) Bitmap.createBitmap(bmp, 0, 0, bmp.width, bmp.height, m, true) else bmp
        val buf = ByteArrayOutputStream()
        out.compress(Bitmap.CompressFormat.JPEG, Thumbnails.QUALITY, buf)
        if (out !== bmp) out.recycle()
        bmp.recycle()
        return buf.toByteArray()
    }
}
