package com.mawena.fluluks.engine

import org.json.JSONArray
import org.json.JSONObject

/*
 * Modèles des réponses JSON du moteur (octets UTF-8 produits par native/jni/json_out.c).
 * Chaque `parse` lève EngineError si la réponse est {"err":<code>}.
 */

private fun parseObject(json: ByteArray): JSONObject {
    val o = JSONObject(String(json, Charsets.UTF_8))
    if (o.has("err")) throw EngineError(o.getInt("err"))
    return o
}

/** En-tête LUKS d'une partition. `maxKdfMemoryKib` : plus grande mémoire Argon2 des slots (0 = PBKDF2). */
data class LuksInfo(
    val version: Int,
    val cipher: String,
    val primaryDamaged: Boolean,
    val maxKdfMemoryKib: Long,
) {
    companion object {
        fun from(o: JSONObject) = LuksInfo(
            version = o.getInt("version"),
            cipher = o.getString("cipher"),
            primaryDamaged = o.getBoolean("primary_damaged"),
            maxKdfMemoryKib = o.getLong("max_kdf_memory_kib"),
        )
    }
}

/**
 * Une partition (ou le disque entier, index 0, sans table). `content` = nom de fl_content_name
 * (« LUKS2 », « ext4 », « FAT32 », « inconnu »…). `luks` n'existe que pour LUKS ; `error` est un
 * fl_err quand la partition n'a pas pu être analysée (en-tête LUKS illisible, erreur d'E/S).
 */
data class PartitionInfo(
    val index: Int,
    val firstLba: Long,
    val sizeBytes: Long,
    val content: String,
    val label: String,
    val uuid: String,
    val supported: Boolean,
    val luks: LuksInfo?,
    val error: Int?,
) {
    val encrypted: Boolean get() = luks != null || content.startsWith("LUKS")

    companion object {
        fun from(o: JSONObject) = PartitionInfo(
            index = o.getInt("index"),
            firstLba = o.getLong("first_lba"),
            sizeBytes = o.getLong("size"),
            content = o.getString("content"),
            label = o.getString("label"),
            uuid = o.getString("uuid"),
            supported = o.getBoolean("supported"),
            luks = o.optJSONObject("luks")?.let { LuksInfo.from(it) },
            error = if (o.has("err")) o.getInt("err") else null,
        )
    }
}

/** `table` : "gpt", "mbr" ou "none". */
data class ScanResult(
    val sectorSize: Int,
    val sizeBytes: Long,
    val table: String,
    val parts: List<PartitionInfo>,
) {
    companion object {
        fun parse(json: ByteArray): ScanResult {
            val o = parseObject(json)
            val a = o.getJSONArray("parts")
            return ScanResult(
                sectorSize = o.getInt("sector_size"),
                sizeBytes = o.getLong("size"),
                table = o.getString("table"),
                parts = List(a.length()) { PartitionInfo.from(a.getJSONObject(it)) },
            )
        }
    }
}

enum class FileType {
    FILE, DIR, SYMLINK, OTHER;

    companion object {
        fun from(code: String) = when (code) {
            "f" -> FILE
            "d" -> DIR
            "l" -> SYMLINK
            else -> OTHER
        }
    }
}

/** Entrée de dossier. `mtime` en secondes Unix. Un nom non UTF-8 sur le disque arrive avec U+FFFD. */
data class DirEntry(val name: String, val type: FileType, val size: Long, val mtime: Long) {
    companion object {
        fun from(o: JSONObject) = DirEntry(
            name = o.getString("name"),
            type = FileType.from(o.getString("type")),
            size = o.getLong("size"),
            mtime = o.getLong("mtime"),
        )

        /** Réponse de [NativeEngine.list] : tableau trié par nom (octets UTF-8). */
        fun parseList(json: ByteArray): List<DirEntry> {
            val s = String(json, Charsets.UTF_8)
            if (s.startsWith("{")) parseObject(json) // {"err":…} → lève
            val a = JSONArray(s)
            return List(a.length()) { from(a.getJSONObject(it)) }
        }
    }
}

/** Réponse de [NativeEngine.stat] (mêmes champs qu'une entrée ; `name` = dernier composant, "" pour "/"). */
data class FsStat(val name: String, val type: FileType, val size: Long, val mtime: Long) {
    companion object {
        fun parse(json: ByteArray): FsStat {
            val e = DirEntry.from(parseObject(json))
            return FsStat(e.name, e.type, e.size, e.mtime)
        }
    }
}

/** Réponse de [NativeEngine.statfs]. */
data class VolumeStat(
    val totalBytes: Long,
    val freeBytes: Long,
    val label: String,
    val readOnly: Boolean,
    val roReason: String,
    val needsRecovery: Boolean,
    /** Système de fichiers (`fl_content_name`) ; vide pour un moteur ancien. */
    val fs: String = "",
) {
    companion object {
        fun parse(json: ByteArray): VolumeStat {
            val o = parseObject(json)
            return VolumeStat(
                totalBytes = o.getLong("total"),
                freeBytes = o.getLong("free"),
                label = o.getString("label"),
                readOnly = o.getBoolean("read_only"),
                roReason = o.getString("ro_reason"),
                needsRecovery = o.getBoolean("needs_recovery"),
                fs = o.optString("fs", ""),
            )
        }
    }
}
