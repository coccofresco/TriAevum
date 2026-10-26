package org.triaevum.android.forge

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.IBinder
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
import org.triaevum.android.R
import org.triaevum.android.TriAevumActivity
import org.triaevum.android.diagnostics.DiagnosticsActivity
import org.triaevum.android.diagnostics.StartupDiagnostics
import org.triaevum.android.diagnostics.StartupReport
import org.triaevum.android.saves.ForgeBackups
import org.triaevum.android.saves.SavesActivity
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
 * the runtime reports them. "Install" hands the cartridge and the chosen language to
 * [InstallService], a foreground service in this process that runs the job while the screen is
 * locked or the installer is closed; this screen binds to it and shows its progress whenever it
 * is in front. Once a title is installed the same choices change its saved setting (the
 * "game_language" job). The screen also leads to save import/export and to the diagnostics
 * report, and offers that report after the previous session ended abnormally.
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
    /** The running short job of this screen (recognition, language), not the installation. */
    @Volatile private var job = 0L
    @Volatile private var closing = false
    private var worker: Thread? = null
    private var screenJob = false
    private var screenJobCancellable = false

    /** The bound installation service, null while unbound. */
    private var service: InstallService? = null
    private val waitingForService = ArrayList<(InstallService) -> Unit>()
    private var installRunning = false
    private var handledGeneration = 0
    private val listener = InstallService.Listener { render(it) }
    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, binder: IBinder) {
            val bound = (binder as InstallService.Local).service
            service = bound
            if (!bound.snapshot.running && InstallService.takeInterrupted(this@InstallerActivity)) {
                status.text = getString(R.string.forge_interrupted)
                detail.text = ""
            }
            bound.attach(listener)
            waitingForService.toList().forEach { it(bound) }
            waitingForService.clear()
        }

        override fun onServiceDisconnected(name: ComponentName) {
            service = null
            installRunning = false
            refreshBusy()
        }
    }

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
    /** An interrupted restore is being undone. */
    private var recovering = false

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        StartupDiagnostics.installCrashHandler(this, "forge")
        setContentView(buildLayout())
        refreshPlay()
        refreshInstall(false)
        if (!ForgePackage.available(this)) {
            status.text = getString(R.string.forge_no_package)
            select.isEnabled = false
        }
        // Developer installs without the picker: a file in the app's external files folder.
        val devRom = intent.getStringExtra(EXTRA_DEV_ROM)
        if (devRom != null && state == null) {
            autoInstall = true
            val file = File(getExternalFilesDir(null), devRom)
            recognize(ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY), devRom)
        } else {
            showInstalledLanguages(null)
        }
        if (state == null) offerReportAfterAbnormalExit()
    }

    override fun onStart() {
        super.onStart()
        bindService(Intent(this, InstallService::class.java), connection, Context.BIND_AUTO_CREATE)
    }

    override fun onStop() {
        service?.detach(listener)
        unbindService(connection)
        service = null
        super.onStop()
    }

    override fun onDestroy() {
        closing = true
        // The installation belongs to the service and continues; only this screen's jobs end.
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
            status.text = getString(R.string.forge_cannot_open)
            return
        }
        recognize(descriptor, displayName(uri))
    }

    @Deprecated("Platform permission API; the installer has no AndroidX dependency.")
    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        @Suppress("DEPRECATION")
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        // Without the permission the installation still runs; only its notification is hidden.
        if (requestCode == REQUEST_NOTIFICATIONS) install()
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
        post(getString(R.string.forge_package_error, error.message.orEmpty()))
        null
    }

    private fun profileRoot(): File? =
        getExternalFilesDir(null).also { if (it == null) post(getString(R.string.forge_storage_unavailable)) }

    private fun request(name: String): JSONObject =
        JSONObject().put("format", "triaevum_forge_request_v1").put("job", name)

    /**
     * Runs one short job off the UI thread: [start] builds the request there (null when it
     * cannot), events reach [onEvent] and, once the job ended, [then] runs on the UI thread.
     */
    private fun runJob(
        busyMessage: String?,
        name: String,
        start: () -> JSONObject?,
        onEvent: (JSONObject) -> Unit,
        then: () -> Unit = {},
    ) {
        if (worker?.isAlive == true) return
        screenJob = true
        screenJobCancellable = busyMessage != null
        if (busyMessage != null) {
            status.text = busyMessage
            detail.text = ""
        }
        refreshBusy()
        worker = Thread({
            try {
                val request = start()
                val handle = if (request == null) 0L else try {
                    ForgeNative.startJob(request.toString().toByteArray()).also { job = it }
                } catch (error: IllegalArgumentException) {
                    post(error.message ?: getString(R.string.forge_refused))
                    0L
                }
                if (handle != 0L) pump(handle, onEvent)
            } finally {
                runOnUiThread {
                    screenJob = false
                    if (!closing) {
                        refreshBusy()
                        then()
                    }
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
        if (worker?.isAlive == true || installRunning) {
            descriptor.close()
            return
        }
        rom?.close()
        rom = descriptor
        romName = name
        recognizedLabel = null
        showCard(getString(R.string.forge_recognizing), name, emptyList(), null, installed = false, error = false)
        runJob(getString(R.string.forge_recognizing_named, name), "forge-recognize", start = {
            val packageRoot = packageRoot()
            val profileRoot = profileRoot()
            if (packageRoot == null || profileRoot == null) null else request("rom_recognize")
                .put("source", JSONObject().put("fd", descriptor.dup().detachFd()))
                .put("package_root", packageRoot.absolutePath)
                .put("android", InstallService.androidTarget(this, profileRoot))
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
                val label = getString(R.string.forge_recognized, result.optString("label"))
                val document = result.optJSONObject("languages")
                val note = if (document == null) {
                    getString(R.string.forge_languages_later, result.optString("language_error"))
                } else {
                    getString(R.string.forge_languages_note)
                }
                recognizedLabel = label
                showCard(label, note, choices(document), document?.optString("selected"), installed = false, error = false)
                status.text = getString(if (document == null) R.string.forge_ready_to_install else R.string.forge_choose_language)
                detail.text = ""
            }
            "failed" -> {
                rom?.close()
                rom = null
                val message = event.getJSONObject("error").optString("message")
                showCard(getString(R.string.forge_not_recognized), message, emptyList(), null, installed = false, error = true)
                status.text = getString(R.string.forge_select_supported)
            }
            "cancelled" -> {
                rom?.close()
                rom = null
                card.visibility = View.GONE
                status.text = getString(R.string.forge_recognition_cancelled)
            }
        }
    }

    /** Asks once for the notification permission (Android 13+), then installs either way. */
    private fun requestInstall() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            val preferences = getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
            if (!preferences.getBoolean(KEY_NOTIFICATIONS_ASKED, false)) {
                preferences.edit().putBoolean(KEY_NOTIFICATIONS_ASKED, true).apply()
                requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), REQUEST_NOTIFICATIONS)
                return
            }
        }
        install()
    }

    /** Hands the recognized cartridge and the chosen language to the installation service. */
    private fun install() {
        val descriptor = rom ?: return
        val language = codes.getOrNull(languageIndex())
        val name = romName
        rom = null
        refreshInstall(true)
        withService { it.install(descriptor, name, language) }
    }

    private fun withService(action: (InstallService) -> Unit) {
        val bound = service
        if (bound != null) action(bound) else waitingForService += action
    }

    /** Shows the installation service's state. */
    private fun render(snapshot: InstallService.Snapshot) {
        installRunning = snapshot.running
        if (snapshot.running || snapshot.outcome != InstallService.Outcome.NONE) {
            status.text = snapshot.status
            detail.text = if (snapshot.running && snapshot.total > 0 && snapshot.detail.isEmpty()) {
                getString(R.string.forge_progress, snapshot.done, snapshot.total)
            } else {
                snapshot.detail
            }
        }
        if (snapshot.outcome == InstallService.Outcome.SUCCEEDED && snapshot.generation != handledGeneration) {
            handledGeneration = snapshot.generation
            refreshPlay()
            showInstalledLanguages(null)
        }
        refreshBusy()
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
                    val title = recognizedLabel ?: getString(R.string.forge_game_language)
                    showCard(title, getString(R.string.forge_language_applies), choices(document),
                        document.optString("selected"), installed = true, error = false)
                    if (select != null) status.text = getString(R.string.forge_language_saved)
                }
                "failed" -> {
                    failed = true
                    status.text = event.getJSONObject("error").optString("message")
                }
            }
        }, then = { if (failed && select != null) showInstalledLanguages(null) })
    }

    /** After an abnormal end of the previous game or installer process, offers the report. */
    private fun offerReportAfterAbnormalExit() {
        val context = applicationContext
        Thread({
            val exits = StartupDiagnostics.newAbnormalExits(context)
            if (exits.isEmpty()) return@Thread
            val summary = StartupReport.summary(exits.first(), StartupDiagnostics.sessions(context))
            runOnUiThread {
                if (closing || isFinishing) return@runOnUiThread
                AlertDialog.Builder(this)
                    .setTitle(R.string.diagnostics_crash_title)
                    .setMessage(getString(R.string.diagnostics_crash_message, summary))
                    .setPositiveButton(R.string.diagnostics_view) { _, _ -> openReport() }
                    .setNegativeButton(R.string.diagnostics_dismiss, null)
                    .show()
            }
        }, "forge-diagnostics").start()
    }

    private fun openReport() = startActivity(Intent(this, DiagnosticsActivity::class.java))

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
        refreshBusy()
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

    /** Enables the controls for the screen's job and the service's installation. */
    private fun refreshBusy() {
        if (closing) return
        val busy = screenJob || installRunning
        progress.visibility = if (busy) ProgressBar.VISIBLE else ProgressBar.GONE
        select.isEnabled = !busy && ForgePackage.available(this)
        cancel.visibility = if (installRunning || (screenJob && screenJobCancellable)) Button.VISIBLE else Button.GONE
        // Never start the game while Forge may be committing the title.
        play.isEnabled = !busy
        for (index in 0 until languages.childCount) languages.getChildAt(index).isEnabled = !busy
        refreshInstall(busy)
        // A recognition runs only while this screen is in front; the installation does not need it.
        if (screenJob) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }

    /** "Install" belongs to a recognized cartridge that is not installed yet. */
    private fun refreshInstall(busy: Boolean) {
        val ready = rom != null && recognizedLabel != null
        installButton.visibility = if (ready) Button.VISIBLE else Button.GONE
        installButton.isEnabled = ready && !busy
    }

    /**
     * Play is offered only for one coherent version of the player's files: an interrupted
     * restore (UX plan WP-05B) is undone first, on a worker thread, and Play waits for it.
     */
    private fun refreshPlay() {
        val profile = File(getExternalFilesDir(null), "TriAevum.android.launch.json")
        val pending = ForgeBackups.pendingRestore(this)
        play.visibility = if (profile.isFile && !pending) Button.VISIBLE else Button.GONE
        if (pending && !recovering) {
            recovering = true
            Thread({
                try {
                    ForgeBackups.run(ForgeBackups.request(this, "restore_recover"))
                } catch (error: Exception) {
                    Log.e(TAG, "Cannot recover the interrupted restore", error)
                    post(error.message ?: error.javaClass.simpleName)
                }
                runOnUiThread {
                    recovering = false
                    if (!closing && !ForgeBackups.pendingRestore(this)) refreshPlay()
                }
            }, "restore-recover").start()
        }
    }

    private fun cancelRunning() {
        val installing = service?.takeIf { installRunning }
        if (installing != null) {
            installing.cancel()
            return
        }
        val running = job
        if (running != 0L) ForgeNative.cancelJob(running)
        status.text = getString(R.string.forge_cancelling)
    }

    private fun displayName(uri: Uri): String =
        contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { cursor -> if (cursor.moveToFirst()) cursor.getString(0) else null }
            ?: getString(R.string.forge_selected_file)

    private fun dp(value: Int): Int =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value.toFloat(), resources.displayMetrics).toInt()

    /** A scrolling body with the actions below it, usable from 360 dp phones at 200% font scale. */
    private fun buildLayout(): LinearLayout {
        val body = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(24), dp(24), dp(24), dp(8))
            addView(TextView(context).apply {
                text = getString(R.string.forge_title)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 26f)
            })
            addView(TextView(context).apply {
                text = getString(R.string.forge_intro)
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
        // Secondary destinations: save import/export and the diagnostics report.
        val more = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, dp(16), 0, 0)
            addView(Button(context).apply {
                text = getString(R.string.forge_saves)
                minHeight = dp(48)
                setOnClickListener { startActivity(Intent(context, SavesActivity::class.java)) }
            }, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
            addView(Button(context).apply {
                text = getString(R.string.forge_report)
                minHeight = dp(48)
                setOnClickListener { openReport() }
            }, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        }
        body.addView(more, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
        select = Button(this).apply {
            text = getString(R.string.forge_select_rom)
            minHeight = dp(48)
            setOnClickListener { pickRom() }
        }
        installButton = Button(this).apply {
            text = getString(R.string.forge_install)
            minHeight = dp(48)
            visibility = Button.GONE
            setOnClickListener { requestInstall() }
        }
        cancel = Button(this).apply {
            text = getString(R.string.forge_cancel)
            minHeight = dp(48)
            visibility = Button.GONE
            setOnClickListener { cancelRunning() }
        }
        play = Button(this).apply {
            text = getString(R.string.forge_play)
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
        const val REQUEST_NOTIFICATIONS = 2
        const val PREFERENCES = "installer"
        const val KEY_NOTIFICATIONS_ASKED = "notifications_asked"
        const val EXTRA_DEV_ROM = "org.triaevum.android.DEV_ROM"
        /** Developer installs: the language to choose when the cartridge offers it. */
        const val EXTRA_DEV_LANGUAGE = "org.triaevum.android.DEV_LANGUAGE"
        const val ACCENT_COLOR = 0xFF2E7DD7.toInt()
        const val ERROR_COLOR = 0xFFD32F2F.toInt()
    }
}
