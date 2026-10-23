package com.mawena.fluluks.files

/**
 * Préférences d'interface (phase 11). [theme] : `system`, `light` ou `dark` ; [haptics] : retours haptiques ;
 * [welcomeSeen] : l'écran de bienvenue a été montré.
 */
data class UiSettings(val theme: String = "system", val haptics: Boolean = true, val welcomeSeen: Boolean = false)

/** Persistées dans le même [PrefStore] que [ViewPrefs] ; une valeur absente ou inconnue revient au défaut. */
class UiPrefs(private val store: PrefStore) {
    fun get() = UiSettings(
        store.getString(THEME)?.takeIf { it in THEMES } ?: "system",
        store.getBoolean(HAPTICS, true),
        store.getBoolean(WELCOME, false),
    )

    fun set(v: UiSettings) {
        store.putString(THEME, v.theme.takeIf { it in THEMES } ?: "system")
        store.putBoolean(HAPTICS, v.haptics)
        store.putBoolean(WELCOME, v.welcomeSeen)
    }

    companion object {
        private val THEMES = setOf("system", "light", "dark")
        private const val THEME = "ui_theme"
        private const val HAPTICS = "ui_haptics"
        private const val WELCOME = "ui_welcome_seen"
    }
}
