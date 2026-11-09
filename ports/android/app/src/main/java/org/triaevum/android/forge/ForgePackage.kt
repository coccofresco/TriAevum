package org.triaevum.android.forge

import android.content.Context
import android.content.res.AssetManager
import java.io.File
import java.io.IOException
import org.json.JSONObject
import org.triaevum.android.BuildConfig

/**
 * The APK's Forge package: catalog, recipes, resources, precomputed tables and shader corpus,
 * stored as assets under [ASSET_ROOT]. Forge reads them as files, so they are copied once per
 * installed APK into the app's internal storage; the catalog's hashes verify every file Forge
 * uses. The APK's libraries stay where Android installed them (FORGE_SPEC.md 4.4, point 6).
 */
object ForgePackage {
    const val ASSET_ROOT = "forge-package"
    private const val STAMP = ".apk-stamp"

    /** True when this APK carries a Forge package (developer APKs may not). */
    fun available(context: Context): Boolean =
        context.assets.list(ASSET_ROOT)?.contains("recipes") == true

    /**
     * The package folder for this APK, copied first when the APK changed. Serialized: the
     * installer's inspection and the service's jobs may ask at once right after an APK update.
     */
    @Synchronized
    @Throws(IOException::class)
    fun prepare(context: Context): File {
        val target = File(context.filesDir, ASSET_ROOT)
        val stamp = apkStamp(context)
        val stampFile = File(target, STAMP)
        if (stampFile.isFile && stampFile.readText() == stamp) return target
        val staging = File(context.filesDir, ".$ASSET_ROOT-new")
        staging.deleteRecursively()
        copyTree(context.assets, ASSET_ROOT, staging)
        if (BuildConfig.VERIFY_FORGE_PACKAGE) requireSigned(staging)
        File(staging, STAMP).writeText(stamp)
        if (target.exists() && !target.deleteRecursively()) throw IOException("Cannot replace $target")
        if (!staging.renameTo(target)) throw IOException("Cannot publish $target")
        return target
    }

    /**
     * A signed release refuses a Forge package whose publisher signature or file hashes do not
     * verify (the Rust core, ForgeNative.verifyPackage); nothing is published then.
     */
    @Throws(IOException::class)
    private fun requireSigned(root: File) {
        val answer = JSONObject(String(ForgeNative.verifyPackage(root.absolutePath.toByteArray()), Charsets.UTF_8))
        if (answer.optString("status") != "signed") {
            root.deleteRecursively()
            throw IOException("Forge package refused: " + answer.optString("message", "not signed"))
        }
    }

    private fun apkStamp(context: Context): String {
        val info = context.packageManager.getPackageInfo(context.packageName, 0)
        return "${info.longVersionCode}:${info.lastUpdateTime}"
    }

    private fun copyTree(assets: AssetManager, path: String, destination: File) {
        val children = assets.list(path).orEmpty()
        if (children.isEmpty()) {
            destination.parentFile?.mkdirs()
            assets.open(path).use { input -> destination.outputStream().use { input.copyTo(it, 1 shl 20) } }
            return
        }
        if (!destination.isDirectory && !destination.mkdirs()) throw IOException("Cannot create $destination")
        for (child in children) copyTree(assets, "$path/$child", File(destination, child))
    }
}
