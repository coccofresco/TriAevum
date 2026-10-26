package org.triaevum.android.forge

import android.app.LocaleManager
import android.content.Context
import android.content.res.Configuration
import android.content.res.Resources
import android.os.Build
import android.os.LocaleList
import android.util.Log
import java.io.File
import java.util.Locale
import org.json.JSONObject

/**
 * The installer's language (owner decision 8, the desktop Forge's rule in forge-desktop
 * `texts::choose_language`): one of the game languages of the ROM in use; before a ROM is known,
 * one of the languages a supported ROM provides. The user's own choice comes first, then the
 * system language, then English; a language without translated texts shows the English ones.
 *
 * The choice is kept like the desktop's, as `ui_language` in Forge's own settings
 * `data/config/forge.json` (never the game's configuration). The language is applied to the app
 * with the platform's per-app language on Android 13+ ([LocaleManager]); older versions get a
 * configuration context for the installer's process (`attachBaseContext`, [wrap]).
 */
object ForgeLocale {
    const val ENGLISH = "en"
    /** forge-desktop `SUPPORTED_ROM_LANGUAGES`: the languages any supported ROM provides. */
    val SUPPORTED_ROM_LANGUAGES = listOf("en", "fr", "de", "es", "it")
    const val PREFERENCES_FORMAT = "triaevum_forge_preferences_v1"
    private const val KEY = "ui_language"
    private const val TAG = "TriAevumForge"
    private const val PREFERENCES = "installer-locale"
    private const val KEY_APPLIED = "applied"
    private const val KEY_ROM_LANGUAGES = "rom_languages"

    /** The game languages by their own names, with accents (the runtime reports ASCII labels). */
    val NATIVE_NAMES = mapOf(
        "en" to "English",
        "fr" to "Français",
        "de" to "Deutsch",
        "es" to "Español",
        "it" to "Italiano",
        "nl" to "Nederlands",
    )

    /**
     * The installer's language for the ROM's game languages [rom] (empty before one is known) and
     * the [preferred] languages in order (the kept choice, then the system's).
     */
    fun choose(rom: List<String>, preferred: List<String?>): String {
        val offered = { code: String -> if (rom.isEmpty()) code in SUPPORTED_ROM_LANGUAGES else code in rom }
        preferred.filterNotNull().firstOrNull { offered(it) }?.let { return it }
        return if (rom.isEmpty() || offered(ENGLISH)) ENGLISH else rom.first()
    }

    /** The languages the installer offers for [rom] (empty before one is known). */
    fun offered(rom: List<String>): List<String> = rom.ifEmpty { SUPPORTED_ROM_LANGUAGES }

    /** The system's language, not the app's own. */
    fun systemLanguage(): String = Resources.getSystem().configuration.locales[0].language

    private fun settingsFile(profileRoot: File) = File(profileRoot, "data/config/forge.json")

    /** The kept choice: two lowercase letters, or null (none, or an unreadable file). */
    fun savedChoice(profileRoot: File?): String? {
        val file = profileRoot?.let(::settingsFile)?.takeIf { it.isFile } ?: return null
        return try {
            JSONObject(file.readText()).optString(KEY).takeIf { it.length == 2 && it.all { c -> c in 'a'..'z' } }
        } catch (error: Exception) {
            null
        }
    }

    /** Keeps [code] as the user's choice; the file's other members stay. False when it cannot be written. */
    fun saveChoice(profileRoot: File, code: String): Boolean {
        val file = settingsFile(profileRoot)
        return try {
            val document = try {
                if (file.isFile) JSONObject(file.readText()) else JSONObject()
            } catch (_: Exception) {
                JSONObject()
            }
            document.put("format", PREFERENCES_FORMAT).put(KEY, code)
            file.parentFile?.mkdirs()
            val temporary = File(file.parentFile, ".forge.json.tmp")
            temporary.writeText(document.toString(2))
            if (!temporary.renameTo(file)) {
                file.delete()
                check(temporary.renameTo(file)) { "rename failed" }
            }
            true
        } catch (error: Exception) {
            Log.w(TAG, "Cannot keep the installer language", error)
            false
        }
    }

    /** The game languages of the ROM last seen (recognized or installed), so a start does not flicker. */
    fun lastRomLanguages(context: Context): List<String> =
        context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE).getString(KEY_ROM_LANGUAGES, null)
            ?.split(',')?.filter { it.isNotEmpty() }.orEmpty()

    fun rememberRomLanguages(context: Context, rom: List<String>) {
        context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE).edit()
            .putString(KEY_ROM_LANGUAGES, rom.joinToString(",")).apply()
    }

    /** The language the app's texts are in now. */
    fun current(context: Context): String = context.resources.configuration.locales[0].language

    /**
     * Applies [code] to the app. Android 13+: the per-app language, delivered to the running
     * activities as a configuration change. Older versions: kept for [wrap] and applied to
     * [context]'s resources in place. Returns whether anything changed (the caller re-renders).
     */
    fun apply(context: Context, code: String): Boolean {
        val locale = Locale.forLanguageTag(code)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val manager = context.getSystemService(LocaleManager::class.java)
            val applied = manager.applicationLocales
            if (applied.size() == 1 && applied[0].language == code) return false
            // Following the system already gives this language: nothing to set.
            if (applied.isEmpty && systemLanguage() == code) return false
            Locale.setDefault(locale)
            manager.applicationLocales = LocaleList(locale)
            return true
        }
        val preferences = context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
        if (preferences.getString(KEY_APPLIED, null) == code && current(context) == code) return false
        preferences.edit().putString(KEY_APPLIED, code).commit()
        Locale.setDefault(locale)
        for (resources in listOf(context.resources, context.applicationContext.resources)) {
            val configuration = Configuration(resources.configuration)
            configuration.setLocales(LocaleList(locale))
            @Suppress("DEPRECATION")
            resources.updateConfiguration(configuration, resources.displayMetrics)
        }
        return true
    }

    /**
     * `attachBaseContext` of the installer's components before Android 13: [base] in the applied
     * language (Android 13+ applies the per-app language itself).
     */
    fun wrap(base: Context): Context {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) return base
        val code = base.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE).getString(KEY_APPLIED, null) ?: return base
        val locale = Locale.forLanguageTag(code)
        Locale.setDefault(locale)
        val configuration = Configuration(base.resources.configuration)
        configuration.setLocales(LocaleList(locale))
        return base.createConfigurationContext(configuration)
    }
}
