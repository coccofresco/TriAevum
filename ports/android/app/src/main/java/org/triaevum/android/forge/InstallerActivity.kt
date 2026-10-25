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
 * system document picker and the Rust Forge reads it through the returned descriptor, never a
 * filesystem path. SDL and the game keep their own activity.
 */
class InstallerActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var progress: ProgressBar
    private lateinit var select: Button
    private lateinit var play: Button
    @Volatile private var job = 0L
    private var worker: Thread? = null

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        setContentView(buildLayout())
        refreshPlay()
        // Developer check without the picker: a file in the app's external files folder.
        intent.getStringExtra(EXTRA_DEV_ROM)?.let { name ->
            val file = File(getExternalFilesDir(null), name)
            identify(ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY), name)
        }
    }

    override fun onDestroy() {
        val running = job
        job = 0L
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
        identify(descriptor, displayName(uri))
    }

    private fun pickRom() {
        // Providers report missing or wrong MIME types for .3ds/.cci: accept any and let the
        // container magic decide.
        val pick = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
        @Suppress("DEPRECATION")
        startActivityForResult(pick, PICK_ROM)
    }

    private fun identify(descriptor: ParcelFileDescriptor, name: String) {
        if (job != 0L) return
        val request = JSONObject()
            .put("format", "triaevum_forge_request_v1")
            .put("job", "rom_identify")
            .put("source", JSONObject().put("fd", descriptor.detachFd()))
        val handle = try {
            ForgeNative.startJob(request.toString().toByteArray())
        } catch (error: IllegalArgumentException) {
            status.text = error.message
            return
        }
        job = handle
        setBusy(true, "Reading $name…")
        worker = Thread({ pump(handle) }, "forge-events").also { it.start() }
    }

    /** Polls events off the UI thread and posts them; destroys the job after its terminal event. */
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
            ForgeNative.destroyJob(handle)
            job = 0L
            runOnUiThread { setBusy(false, null) }
        }
    }

    private fun show(event: JSONObject) {
        when (event.optString("event")) {
            "stage" -> status.text = event.optString("message")
            "succeeded" -> {
                val result = event.getJSONObject("result")
                status.text = "Program ${result.optString("program_id")} (${result.optString("container")}), " +
                    "RomFS ${result.optLong("romfs_size") / (1024 * 1024)} MiB."
            }
            "failed" -> status.text = event.getJSONObject("error").optString("message")
            "cancelled" -> status.text = "Cancelled."
        }
    }

    private fun setBusy(busy: Boolean, message: String?) {
        progress.visibility = if (busy) ProgressBar.VISIBLE else ProgressBar.GONE
        select.isEnabled = !busy
        play.isEnabled = !busy
        if (message != null) status.text = message
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
                    "Forge reads it on this device and does not handle encryption keys."
                setPadding(0, dp(8), 0, dp(16))
            })
        }
        status = TextView(this).apply { setPadding(0, dp(8), 0, dp(8)) }
        progress = ProgressBar(this).apply { isIndeterminate = true; visibility = ProgressBar.GONE }
        body.addView(status)
        body.addView(progress)
        select = Button(this).apply { text = "Select ROM"; minHeight = dp(48); setOnClickListener { pickRom() } }
        play = Button(this).apply {
            text = "Play"
            minHeight = dp(48)
            setOnClickListener { startActivity(Intent(context, TriAevumActivity::class.java)) }
        }
        val actions = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.END
            setPadding(dp(16), dp(8), dp(16), dp(16))
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
