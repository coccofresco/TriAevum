package org.triaevum.android.saves

import android.content.Context
import java.io.File
import java.io.IOException
import java.util.zip.ZipFile
import org.json.JSONArray
import org.json.JSONObject
import org.triaevum.android.forge.Explanation
import org.triaevum.android.forge.ForgeNative
import org.triaevum.android.forge.ForgePackage

/** A Forge job failed; [title] and [detail] are its explanation for the user. */
class ForgeJobException(val title: String, val detail: String) : IOException(title)

/**
 * The full backup of the Data & backups page (UX plan WP-05B), through Forge's jobs: Forge
 * builds and verifies the `triaevum_backup_v1` zip (saves, save states, settings and these touch
 * preferences) and restores one with a journal, a snapshot of every replaced file and recovery
 * after an interruption. The document picker side stays in [SavesActivity]; plain save archives
 * keep going through [SaveArchive].
 */
object ForgeBackups {
    const val TOUCH_FORMAT = "triaevum_touch_preferences_v1"
    private const val MANIFEST = "manifest.json"

    /** A job request for this app's installation (the inventory's Android fields). */
    fun request(context: Context, job: String): JSONObject {
        val profile = context.getExternalFilesDir(null) ?: throw IOException("No external files folder")
        return JSONObject()
            .put("format", "triaevum_forge_request_v1")
            .put("job", job)
            .put("package_root", ForgePackage.prepare(context).absolutePath)
            .put(
                "android",
                JSONObject()
                    .put("native_library_dir", context.applicationInfo.nativeLibraryDir)
                    .put("profile_root", profile.absolutePath)
                    .put("app_data_dir", context.dataDir.absolutePath)
                    .put("package_name", context.packageName),
            )
    }

    /** Runs a job to its end on the calling (worker) thread; returns its result. */
    fun run(request: JSONObject, onProgress: (Long, Long?) -> Unit = { _, _ -> }): JSONObject {
        val handle = try {
            ForgeNative.startJob(request.toString().toByteArray())
        } catch (error: IllegalArgumentException) {
            throw failure(JSONObject(error.message ?: "{}"))
        }
        try {
            while (true) {
                val bytes = ForgeNative.pollEvent(handle, 250) ?: continue
                val event = JSONObject(String(bytes))
                when (event.optString("event")) {
                    "progress" -> onProgress(event.optLong("done"), if (event.isNull("total")) null else event.optLong("total"))
                    "succeeded" -> return event.optJSONObject("result") ?: JSONObject()
                    "failed", "cancelled" -> throw failure(event)
                }
            }
        } finally {
            ForgeNative.destroyJob(handle)
        }
    }

    private fun failure(event: JSONObject): ForgeJobException {
        val explanation = try {
            JSONObject(String(ForgeNative.explain(Explanation.request(event))))
        } catch (_: Exception) {
            JSONObject()
        }
        val title = explanation.optString("title", event.optJSONObject("error")?.optString("message").orEmpty())
        val detail = listOf(explanation.optString("next_action"), explanation.optString("details"))
            .filter { it.isNotEmpty() }
            .joinToString("\n\n")
        return ForgeJobException(title, detail)
    }

    /** True for a `triaevum_backup_v1` zip (anything else may be a plain save archive). */
    fun isForgeBackup(file: File): Boolean = try {
        ZipFile(file).use { zip -> zip.getEntry(MANIFEST) != null }
    } catch (_: IOException) {
        false
    }

    private fun touchStore(context: Context) =
        context.getSharedPreferences("${context.packageName}_preferences", Context.MODE_PRIVATE)

    /** The touch controls' preferences, with their types, for the backup. */
    fun touchPreferences(context: Context): JSONObject {
        val values = JSONObject()
        for ((key, value) in touchStore(context).all) {
            val typed = when (value) {
                is Boolean -> JSONObject().put("type", "bool").put("value", value)
                is Int -> JSONObject().put("type", "int").put("value", value)
                is Long -> JSONObject().put("type", "long").put("value", value)
                is Float -> JSONObject().put("type", "float").put("value", value.toDouble())
                is String -> JSONObject().put("type", "string").put("value", value)
                is Set<*> -> JSONObject().put("type", "strings").put("value", JSONArray(value.map { it.toString() }))
                else -> continue
            }
            values.put(key, typed)
        }
        return JSONObject().put("format", TOUCH_FORMAT).put("values", values)
    }

    /** Replaces the touch preferences with a backup's; false when the backup holds none. */
    fun applyTouchPreferences(context: Context, backup: JSONObject): Boolean {
        if (backup.optString("format") != TOUCH_FORMAT) return false
        val values = backup.optJSONObject("values") ?: return false
        val editor = touchStore(context).edit().clear()
        for (key in values.keys()) {
            val typed = values.optJSONObject(key) ?: continue
            when (typed.optString("type")) {
                "bool" -> editor.putBoolean(key, typed.optBoolean("value"))
                "int" -> editor.putInt(key, typed.optInt("value"))
                "long" -> editor.putLong(key, typed.optLong("value"))
                "float" -> editor.putFloat(key, typed.optDouble("value").toFloat())
                "string" -> editor.putString(key, typed.optString("value"))
                "strings" -> {
                    val items = typed.optJSONArray("value") ?: JSONArray()
                    editor.putStringSet(key, (0 until items.length()).map { items.optString(it) }.toSet())
                }
            }
        }
        return editor.commit()
    }

    /** True while an interrupted restore waits for its recovery (`data/backups/restore-*`). */
    fun pendingRestore(context: Context): Boolean {
        val backups = File(context.getExternalFilesDir(null) ?: return false, "data/backups")
        return backups.listFiles().orEmpty().filter { it.name.startsWith("restore-") }.any { folder ->
            val journal = File(folder, "journal.json")
            journal.isFile && try {
                JSONObject(journal.readText()).optString("state") !in setOf("complete", "rolled_back")
            } catch (_: Exception) {
                true
            }
        }
    }
}
