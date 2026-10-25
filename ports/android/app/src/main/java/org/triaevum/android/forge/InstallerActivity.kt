package org.triaevum.android.forge

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.ParcelFileDescriptor
import android.util.Log
import android.util.TypedValue
import android.view.Gravity
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import org.json.JSONObject
import org.triaevum.android.TriAevumActivity
import java.io.File

/**
 * The installer screen before gameplay: the user picks their own decrypted cartridge with the
 * system document picker and the Rust Forge installs the title from the returned descriptor,
 * never a filesystem path. It runs in a process of its own (":forge" in the manifest): Forge
 * calls the runtime library's native helpers in process, and the game starts in a fresh one.
 * SDL and the game keep their own activity.
 */
class InstallerActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var detail: TextView
    private lateinit var progress: ProgressBar
    private lateinit var select: Button
    private lateinit var cancel: Button
    private lateinit var play: Button
    @Volatile private var job = 0L
    @Volatile private var closing = false
    private var worker: Thread? = null

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        setContentView(buildLayout())
        refreshPlay()
        if (!ForgePackage.available(this)) {
            status.text = "This build has no Forge package: it can only play a title prepared beforehand."
            select.isEnabled = false
        }
        // Developer installs without the picker: a file in the app's external files folder.
        intent.getStringExtra(EXTRA_DEV_ROM)?.let { name ->
            val file = File(getExternalFilesDir(null), name)
            install(ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY), name)
        }
    }

    override fun onDestroy() {
        closing = true
        val running = job
        if (running != 0L) ForgeNative.cancelJob(running)
        worker?.join(15_000)
        super.onDestroy()
    }

    @Deprecated("Platform result API; the installer has no AndroidX dependency.")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data
        if (requestCode != PICK_ROM || resultCode != RESULT_OK || uri == null) return
        val descriptor = try {
            contentResolver.openFileDescriptor(uri, "r")
        } catch (error: Exception) {
            Log.w(TAG, "Cannot open the selected document", error)
            null
        }
        if (descriptor == null) {
            status.text = "The selected file cannot be opened."
            return
        }
        install(descriptor, displayName(uri))
    }

    private fun pickRom() {
        // Providers report missing or wrong MIME types for .3ds/.cci: accept any and let the
        // container magic decide.
        val pick = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
        @Suppress("DEPRECATION")
        startActivityForResult(pick, PICK_ROM)
    }

    /** Copies the package when the APK changed, then runs the install job off the UI thread. */
    private fun install(descriptor: ParcelFileDescriptor, name: String) {
        if (worker?.isAlive == true) {
            descriptor.close()
            return
        }
        setBusy(true, "Preparing $name…")
        worker = Thread({
            try {
                val handle = startInstall(descriptor)
                if (handle != 0L) pump(handle)
            } finally {
                runOnUiThread { setBusy(false, null) }
            }
        }, "forge-install").also { it.start() }
    }

    /** Starts the job; the descriptor belongs to Forge from then on. Returns 0 when refused. */
    private fun startInstall(descriptor: ParcelFileDescriptor): Long {
        val packageRoot = try {
            ForgePackage.prepare(this)
        } catch (error: Exception) {
            Log.e(TAG, "Cannot prepare the Forge package", error)
            descriptor.close()
            post("The application's Forge package cannot be prepared: ${error.message}")
            return 0L
        }
        val profileRoot = getExternalFilesDir(null)
        if (profileRoot == null) {
            descriptor.close()
            post("The application's storage is unavailable.")
            return 0L
        }
        val android = JSONObject()
            .put("native_library_dir", applicationInfo.nativeLibraryDir)
            .put("profile_root", profileRoot.absolutePath)
            .put("cache_dir", cacheDir.absolutePath)
        val request = JSONObject()
            .put("format", "triaevum_forge_request_v1")
            .put("job", "install")
            .put("source", JSONObject().put("fd", descriptor.detachFd()))
            .put("source_kind", "rom")
            .put("package_root", packageRoot.absolutePath)
            .put("download_hd_textures", false)
            .put("android", android)
        return try {
            ForgeNative.startJob(request.toString().toByteArray()).also { job = it }
        } catch (error: IllegalArgumentException) {
            post(error.message ?: "Forge refused the request.")
            0L
        }
    }

    /** Polls events and posts them; destroys the job after its terminal event. */
    private fun pump(handle: Long) {
        try {
            while (true) {
                val bytes = ForgeNative.pollEvent(handle, 250) ?: continue
                val event = JSONObject(String(bytes))
                val kind = event.optString("event")
                runOnUiThread { show(event) }
                if (kind == "succeeded" || kind == "failed" || kind == "cancelled") break
            }
        } finally {
            job = 0L
            ForgeNative.destroyJob(handle)
        }
    }

    private fun post(message: String) = runOnUiThread { status.text = message }

    private fun show(event: JSONObject) {
        if (closing) return
        when (event.optString("event")) {
            "stage" -> {
                status.text = event.optString("message")
                detail.text = ""
            }
            "report" -> detail.text = event.optString("message")
            "succeeded" -> {
                status.text = "The title is ready to play."
                detail.text = ""
                refreshPlay()
            }
            "failed" -> {
                val error = event.getJSONObject("error")
                status.text = error.optString("message")
                detail.text = "Stage: ${error.optString("stage")} (${error.optString("code")})"
            }
            "cancelled" -> status.text = "Installation cancelled."
        }
    }

    private fun setBusy(busy: Boolean, message: String?) {
        if (closing) return
        progress.visibility = if (busy) ProgressBar.VISIBLE else ProgressBar.GONE
        select.isEnabled = !busy && ForgePackage.available(this)
        cancel.visibility = if (busy) Button.VISIBLE else Button.GONE
        play.isEnabled = !busy
        // Installation runs only while this screen is in front.
        if (busy) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        if (message != null) {
            status.text = message
            detail.text = ""
        }
    }

    private fun refreshPlay() {
        val profile = File(getExternalFilesDir(null), "TriAevum.android.launch.json")
        play.visibility = if (profile.isFile) Button.VISIBLE else Button.GONE
    }

    private fun displayName(uri: Uri): String =
        contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { cursor -> if (cursor.moveToFirst()) cursor.getString(0) else null } ?: "the selected file"

    private fun dp(value: Int): Int =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value.toFloat(), resources.displayMetrics).toInt()

    /** A scrolling body with the actions below it, usable from 360 dp phones at 200% font scale. */
    private fun buildLayout(): LinearLayout {
        val body = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(24), dp(24), dp(24), dp(8))
            addView(TextView(context).apply {
                text = "TriAevum Forge"
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 26f)
            })
            addView(TextView(context).apply {
                text = "Select your own decrypted Nintendo 3DS cartridge image (.3ds or .cci). " +
                    "Forge prepares the game on this device, without encryption keys or network access. " +
                    "The first installation takes several minutes; keep this screen open."
                setPadding(0, dp(8), 0, dp(16))
            })
        }
        status = TextView(this).apply { setPadding(0, dp(8), 0, dp(4)) }
        detail = TextView(this).apply { setPadding(0, 0, 0, dp(8)); alpha = 0.7f }
        progress = ProgressBar(this).apply { isIndeterminate = true; visibility = ProgressBar.GONE }
        body.addView(status)
        body.addView(detail)
        body.addView(progress)
        select = Button(this).apply { text = "Select ROM"; minHeight = dp(48); setOnClickListener { pickRom() } }
        cancel = Button(this).apply {
            text = "Cancel"
            minHeight = dp(48)
            visibility = Button.GONE
            setOnClickListener {
                val running = job
                if (running != 0L) ForgeNative.cancelJob(running)
                status.text = "Cancelling after the current step…"
            }
        }
        play = Button(this).apply {
            text = "Play"
            minHeight = dp(48)
            setOnClickListener { startActivity(Intent(context, TriAevumActivity::class.java)) }
        }
        val actions = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.END
            setPadding(dp(16), dp(8), dp(16), dp(16))
            addView(cancel)
            addView(select)
            addView(play)
        }
        return LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(ScrollView(context).apply { addView(body) }, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
            addView(actions, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
            fitsSystemWindows = true
        }
    }

    private companion object {
        const val TAG = "TriAevumForge"
        const val PICK_ROM = 1
        const val EXTRA_DEV_ROM = "org.triaevum.android.DEV_ROM"
    }
}
