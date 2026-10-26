package org.triaevum.android.diagnostics

import android.content.Context
import java.io.File
import org.json.JSONObject
import org.triaevum.android.forge.ForgeNative
import org.triaevum.android.forge.ForgePackage

/**
 * Forge's diagnostics for this installation (forge-core `diagnostics`, the same report and
 * support bundle as the desktop Forge): the redacted report text, the game's last fatal error
 * from data/logs/native-stderr.log, and the support bundle zip written to this app's cache, where
 * [DiagnosticsBundleProvider] serves it for sharing. Built only when the user opens the
 * Diagnostics screen; nothing is sent anywhere.
 */
object DiagnosticsBundle {
    const val FILE_NAME = "TriAevum-diagnostics.zip"

    /** The folder the provider serves, inside the app's cache. */
    fun directory(context: Context) = File(context.cacheDir, "share")

    class Result(
        /** Forge's redacted report, or why it could not be made. */
        val text: String,
        /** The zip to share, or null when it could not be written. */
        val bundle: File?,
        /** The bundle's member names and sizes, shown before sharing. */
        val members: List<String>,
    )

    /** Call off the UI thread: it reads logs and hashes the installed runtime. */
    fun build(context: Context): Result {
        val profileRoot = context.getExternalFilesDir(null)
            ?: return Result("Forge diagnostics: app storage is unavailable.", null, emptyList())
        val packageRoot = try {
            ForgePackage.prepare(context)
        } catch (error: Exception) {
            return Result("Forge diagnostics: the Forge package is unavailable (${error.javaClass.simpleName}).", null, emptyList())
        }
        val destination = File(directory(context), FILE_NAME)
        destination.parentFile?.mkdirs()
        val request = JSONObject()
            .put("format", "triaevum_forge_diagnostics_v1")
            .put("package_root", packageRoot.absolutePath)
            .put(
                "android",
                JSONObject()
                    .put("native_library_dir", context.applicationInfo.nativeLibraryDir)
                    .put("profile_root", profileRoot.absolutePath)
                    .put("app_data_dir", context.dataDir.absolutePath)
                    .put("package_name", context.packageName),
            )
            .put("bundle", destination.absolutePath)
        val answer = ForgeNative.diagnostics(request.toString().toByteArray(Charsets.UTF_8))
            ?.let { JSONObject(String(it, Charsets.UTF_8)) }
            ?: return Result("Forge diagnostics: no answer from Forge.", null, emptyList())
        if (answer.optString("status") != "ok") {
            val explanation = answer.optJSONObject("explanation")
            val title = explanation?.optString("title").orEmpty()
            val details = explanation?.optString("details").orEmpty()
            return Result("Forge diagnostics failed: $title\n$details".trim(), null, emptyList())
        }
        val members = mutableListOf<String>()
        answer.optJSONObject("bundle_plan")?.optJSONArray("members")?.let { array ->
            for (index in 0 until array.length()) {
                val member = array.getJSONObject(index)
                members += "${member.optString("name")} (${member.optLong("size")} bytes)"
            }
        }
        val written = answer.optJSONObject("bundle")?.let { destination.takeIf { it.isFile } }
        return Result(answer.optString("text"), written, members)
    }
}
