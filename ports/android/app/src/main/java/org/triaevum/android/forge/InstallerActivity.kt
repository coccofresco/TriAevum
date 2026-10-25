package org.triaevum.android.forge

import android.app.Activity
import android.content.Intent
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Bundle
import android.os.ParcelFileDescriptor
import android.util.Log
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.RadioButton
import android.widget.RadioGroup
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
 *
 * A picked cartridge is recognized first (the "rom_recognize" job reads it in place): the card
 * under the introduction names the revision and offers the game languages its RomFS has, as
 * the runtime reports them. "Install" passes the chosen language; once a title is installed
 * the same choices change its saved setting (the "game_language" job).
 */
class InstallerActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var detail: TextView
    private lateinit var progress: ProgressBar
    private lateinit var card: LinearLayout
    private lateinit var cardTitle: TextView
    private lateinit var cardText: TextView
    private lateinit var languages: RadioGroup
    private lateinit var select: Button
    private lateinit var installButton: Button
    private lateinit var cancel: Button
    private lateinit var play: Button
    @Volatile private var job = 0L
    @Volatile private var closing = false
    private var worker: Thread? = null

    /** The picked cartridge, kept open until it is installed or replaced. */
    private var rom: ParcelFileDescriptor? = null
    private var romName = ""
    /** The recognized revision's label; null until [rom] is recognized. */
    private var recognizedLabel: String? = null
    /** The language codes of the card's choices, in order, and whether they are installed. */
    private var codes: List<String> = emptyList()
    private var installedChoices = false
    private var updatingChoices = false
    /** Developer installs start as soon as the cartridge is recognized. */
    private var autoInstall = false

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        setContentView(buildLayout())
        refreshPlay()
        refreshInstall(false)
        if (!ForgePackage.available(this)) {
            status.text = "This build has no Forge package: it can only play a title prepared beforehand."
            select.isEnabled = false
        }
        // Developer installs without the picker: a file in the app's external files folder.
        val devRom = intent.getStringExtra(EXTRA_DEV_ROM)
        if (devRom != null) {
            autoInstall = true
            val file = File(getExternalFilesDir(null), devRom)
            recognize(ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY), devRom)
        } else {
            showInstalledLanguages(null)
        }
    }

    override fun onDestroy() {
        closing = true
        val running = job
        if (running != 0L) ForgeNative.cancelJob(running)
        worker?.join(15_000)
        rom?.close()
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
        recognize(descriptor, displayName(uri))
    }

    private fun pickRom() {
        // Providers report missing or wrong MIME types for .3ds/.cci: accept any and let the
        // container magic decide.
        val pick = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
        @Suppress("DEPRECATION")
        startActivityForResult(pick, PICK_ROM)
    }

    /** The package folder (copied when the APK changed), or null after saying why not. */
    private fun packageRoot(): File? = try {
        ForgePackage.prepare(this)
    } catch (error: Exception) {
        Log.e(TAG, "Cannot prepare the Forge package", error)
        post("The application's Forge package cannot be prepared: ${error.message}")
        null
    }

    private fun profileRoot(): File? =
        getExternalFilesDir(null).also { if (it == null) post("The application's storage is unavailable.") }

    private fun androidTarget(profileRoot: File): JSONObject = JSONObject()
        .put("native_library_dir", applicationInfo.nativeLibraryDir)
        .put("profile_root", profileRoot.absolutePath)
        .put("cache_dir", cacheDir.absolutePath)

    private fun request(name: String): JSONObject =
        JSONObject().put("format", "triaevum_forge_request_v1").put("job", name)

    /**
     * Runs one job off the UI thread: [start] builds the request there (null when it cannot),
     * events reach [onEvent] and, once the job ended, [then] runs on the UI thread.
     */
    private fun runJob(
        busyMessage: String?,
        name: String,
        start: () -> JSONObject?,
        onEvent: (JSONObject) -> Unit,
        then: () -> Unit = {},
    ) {
        if (worker?.isAlive == true) return
        setBusy(true, busyMessage)
        worker = Thread({
            try {
                val request = start()
                val handle = if (request == null) 0L else try {
                    ForgeNative.startJob(request.toString().toByteArray()).also { job = it }
                } catch (error: IllegalArgumentException) {
                    post(error.message ?: "Forge refused the request.")
                    0L
                }
                if (handle != 0L) pump(handle, onEvent)
            } finally {
                runOnUiThread {
                    setBusy(false, null)
                    if (!closing) then()
                }
            }
        }, name).also { it.start() }
    }

    /** Polls events and posts them; destroys the job after its terminal event. */
    private fun pump(handle: Long, onEvent: (JSONObject) -> Unit) {
        try {
            while (true) {
                val bytes = ForgeNative.pollEvent(handle, 250) ?: continue
                val event = JSONObject(String(bytes))
                val kind = event.optString("event")
                runOnUiThread { if (!closing) onEvent(event) }
                if (kind == "succeeded" || kind == "failed" || kind == "cancelled") break
            }
        } finally {
            job = 0L
            ForgeNative.destroyJob(handle)
        }
    }

    /** Recognizes a picked cartridge; Forge reads a duplicate of the descriptor in place. */
    private fun recognize(descriptor: ParcelFileDescriptor, name: String) {
        if (worker?.isAlive == true) {
            descriptor.close()
            return
        }
        rom?.close()
        rom = descriptor
        romName = name
        recognizedLabel = null
        showCard(RECOGNIZING, name, emptyList(), null, installed = false, error = false)
        runJob("Recognizing $name…", "forge-recognize", start = {
            val packageRoot = packageRoot()
            val profileRoot = profileRoot()
            if (packageRoot == null || profileRoot == null) null else request("rom_recognize")
                .put("source", JSONObject().put("fd", descriptor.dup().detachFd()))
                .put("package_root", packageRoot.absolutePath)
                .put("android", androidTarget(profileRoot))
        }, onEvent = { event -> if (rom === descriptor) showRecognition(event) }, then = {
            val installNow = autoInstall && recognizedLabel != null
            autoInstall = false
            if (installNow) {
                intent.getStringExtra(EXTRA_DEV_LANGUAGE)?.let { chooseCode(it) }
                install()
            }
        })
    }

    private fun showRecognition(event: JSONObject) {
        when (event.optString("event")) {
            "stage" -> cardText.text = event.optString("message")
            "succeeded" -> {
                val result = event.getJSONObject("result")
                val label = "ROM recognized: ${result.optString("label")}"
                val document = result.optJSONObject("languages")
                val note = if (document == null) {
                    "The game languages are read during the installation. ${result.optString("language_error")}"
                } else {
                    "Install sets up the game in this language. You can change it later here or in game."
                }
                recognizedLabel = label
                showCard(label, note, choices(document), document?.optString("selected"), installed = false, error = false)
                status.text = if (document == null) "Ready to install." else "Choose the game language, then install."
                detail.text = ""
            }
            "failed" -> {
                rom?.close()
                rom = null
                val message = event.getJSONObject("error").optString("message")
                showCard("ROM not recognized", message, emptyList(), null, installed = false, error = true)
                status.text = "Select a supported decrypted cartridge."
            }
            "cancelled" -> {
                rom?.close()
                rom = null
                card.visibility = View.GONE
                status.text = "Recognition cancelled."
            }
        }
    }

    /** Installs the recognized cartridge with the chosen language; Forge owns the descriptor. */
    private fun install() {
        val descriptor = rom ?: return
        val language = codes.getOrNull(languageIndex())
        rom = null
        var succeeded = false
        runJob("Preparing $romName…", "forge-install", start = {
            val packageRoot = packageRoot()
            val profileRoot = profileRoot()
            if (packageRoot == null || profileRoot == null) {
                descriptor.close()
                null
            } else {
                request("install")
                    .put("source", JSONObject().put("fd", descriptor.detachFd()))
                    .put("source_kind", "rom")
                    .put("package_root", packageRoot.absolutePath)
                    .put("download_hd_textures", false)
                    .put("android", androidTarget(profileRoot))
                    .apply { if (language != null) put("game_language", language) }
            }
        }, onEvent = { event ->
            succeeded = succeeded || event.optString("event") == "succeeded"
            showInstall(event)
        }, then = { if (succeeded) showInstalledLanguages(null) })
    }

    private fun showInstall(event: JSONObject) {
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

    /** Shows the installed title's languages, after saving [select] when given. */
    private fun showInstalledLanguages(select: String?) {
        val profileRoot = getExternalFilesDir(null) ?: return
        if (!File(profileRoot, "data/config/game_language.json").isFile) return
        var failed = false
        runJob(null, "forge-language", start = {
            request("game_language")
                .put("data_root", File(profileRoot, "data").absolutePath)
                .apply { if (select != null) put("select", select) }
        }, onEvent = { event ->
            when (event.optString("event")) {
                "succeeded" -> event.getJSONObject("result").optJSONObject("languages")?.let { document ->
                    val title = recognizedLabel ?: GAME_LANGUAGE
                    showCard(title, "Changes apply on the next full game start.", choices(document),
                        document.optString("selected"), installed = true, error = false)
                    if (select != null) status.text = "Game language saved. Applies on the next full game start."
                }
                "failed" -> {
                    failed = true
                    status.text = event.getJSONObject("error").optString("message")
                }
            }
        }, then = { if (failed && select != null) showInstalledLanguages(null) })
    }

    private fun choices(document: JSONObject?): List<Pair<String, String>> {
        val available = document?.optJSONArray("available") ?: return emptyList()
        return (0 until available.length()).map { index ->
            val entry = available.getJSONObject(index)
            entry.optString("code") to entry.optString("label")
        }
    }

    /** The card under the introduction: a heading, a note and the languages as large choices. */
    private fun showCard(
        title: String,
        text: String,
        entries: List<Pair<String, String>>,
        selected: String?,
        installed: Boolean,
        error: Boolean,
    ) {
        val accent = if (error) ERROR_COLOR else ACCENT_COLOR
        card.visibility = View.VISIBLE
        card.background = GradientDrawable().apply {
            cornerRadius = dp(12).toFloat()
            setStroke(dp(2), accent)
            setColor((accent and 0x00FFFFFF) or 0x1A000000)
        }
        cardTitle.text = title
        cardTitle.setTextColor(accent)
        cardText.text = text
        codes = entries.map { it.first }
        installedChoices = installed
        updatingChoices = true
        languages.clearCheck()
        languages.removeAllViews()
        entries.forEachIndexed { index, (code, label) ->
            val choice = RadioButton(this).apply {
                id = View.generateViewId()
                this.text = label
                tag = index
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 20f)
                minHeight = dp(52)
                setPadding(dp(8), 0, 0, 0)
            }
            languages.addView(choice, RadioGroup.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
            if (code == selected) languages.check(choice.id)
        }
        updatingChoices = false
        languages.visibility = if (entries.isEmpty()) View.GONE else View.VISIBLE
        refreshInstall(worker?.isAlive == true)
    }

    private fun languageIndex(): Int =
        languages.findViewById<RadioButton>(languages.checkedRadioButtonId)?.tag as? Int ?: -1

    private fun chooseCode(code: String) {
        val index = codes.indexOf(code)
        if (index >= 0) languages.check(languages.getChildAt(index).id)
    }

    /** A pending choice is read when installing; an installed one is saved now. */
    private fun languageChosen() {
        if (updatingChoices || !installedChoices) return
        codes.getOrNull(languageIndex())?.let { showInstalledLanguages(it) }
    }

    private fun post(message: String) = runOnUiThread { status.text = message }

    private fun setBusy(busy: Boolean, message: String?) {
        if (closing) return
        progress.visibility = if (busy) ProgressBar.VISIBLE else ProgressBar.GONE
        select.isEnabled = !busy && ForgePackage.available(this)
        cancel.visibility = if (busy && message != null) Button.VISIBLE else Button.GONE
        play.isEnabled = !busy
        for (index in 0 until languages.childCount) languages.getChildAt(index).isEnabled = !busy
        refreshInstall(busy)
        // Installation runs only while this screen is in front.
        if (busy) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        if (message != null) {
            status.text = message
            detail.text = ""
        }
    }

    /** "Install" belongs to a recognized cartridge that is not installed yet. */
    private fun refreshInstall(busy: Boolean) {
        val ready = rom != null && recognizedLabel != null
        installButton.visibility = if (ready) Button.VISIBLE else Button.GONE
        installButton.isEnabled = ready && !busy
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
        cardTitle = TextView(this).apply {
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 20f)
            setTypeface(typeface, Typeface.BOLD)
        }
        cardText = TextView(this).apply { setPadding(0, dp(6), 0, dp(6)) }
        languages = RadioGroup(this).apply {
            orientation = RadioGroup.VERTICAL
            setOnCheckedChangeListener { _, _ -> languageChosen() }
        }
        card = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(14), dp(16), dp(14))
            visibility = View.GONE
            addView(cardTitle)
            addView(cardText)
            addView(languages)
        }
        body.addView(card, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { bottomMargin = dp(12) })
        status = TextView(this).apply { setPadding(0, dp(8), 0, dp(4)) }
        detail = TextView(this).apply { setPadding(0, 0, 0, dp(8)); alpha = 0.7f }
        progress = ProgressBar(this).apply { isIndeterminate = true; visibility = ProgressBar.GONE }
        body.addView(status)
        body.addView(detail)
        body.addView(progress)
        select = Button(this).apply { text = "Select ROM"; minHeight = dp(48); setOnClickListener { pickRom() } }
        installButton = Button(this).apply {
            text = "Install"
            minHeight = dp(48)
            visibility = Button.GONE
            setOnClickListener { install() }
        }
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
            addView(installButton)
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
        /** Developer installs: the language to choose when the cartridge offers it. */
        const val EXTRA_DEV_LANGUAGE = "org.triaevum.android.DEV_LANGUAGE"
        const val RECOGNIZING = "Recognizing the ROM…"
        const val GAME_LANGUAGE = "Game language"
        const val ACCENT_COLOR = 0xFF2E7DD7.toInt()
        const val ERROR_COLOR = 0xFFD32F2F.toInt()
    }
}
