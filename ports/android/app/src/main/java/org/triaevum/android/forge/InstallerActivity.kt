package org.triaevum.android.forge

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.app.NotificationManager
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
import android.provider.Settings
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
 * The installer screen before gameplay. Its state card shows the shared installation state
 * (UX plan WP-01/WP-09: [ForgeNative.inspect], the same answer the desktop Forge and the CLI give)
 * with the shared explanations, and offers what that state needs: select a ROM, Update after the
 * APK was replaced (the "refresh" job binds the new libraries to the prepared game without the
 * ROM, WP-04A), Repair, or Play, which is offered only for a state that can play.
 *
 * The user picks their own decrypted cartridge with the system document picker and the Rust
 * Forge installs the title from the returned descriptor, never a filesystem path; the app keeps a
 * read grant to the document when the provider allows it, so an interrupted installation can be
 * retried without picking again. The screen runs in a process of its own (":forge" in the
 * manifest): Forge calls the runtime library's native helpers in process, and the game starts in
 * a fresh one. SDL and the game keep their own activity.
 *
 * A picked cartridge is recognized first (the "rom_recognize" job reads it in place): the card
 * under the state names the revision and offers the game languages its RomFS has, as the runtime
 * reports them. "Install" hands the cartridge and the chosen language to [InstallService], a
 * foreground service in this process that runs the long jobs while the screen is locked or the
 * installer is closed; this screen binds to it and shows its progress whenever it is in front.
 * When that process died mid-job, the restarted screen inspects the installation and offers to
 * retry or to select the ROM again; job handles are never revived. Once a title is installed the
 * language choices change its saved setting (the "game_language" job). The screen also leads to
 * Saves and backups and to the diagnostics report, offers that report after the previous session
 * ended abnormally, and opens either page when the game's menu asked for it on its way out.
 */
class InstallerActivity : Activity() {
    private lateinit var stateBox: LinearLayout
    private lateinit var stateTitle: TextView
    private lateinit var stateBody: TextView
    private lateinit var stateDetails: TextView
    private lateinit var stateAction: Button
    private lateinit var banner: LinearLayout
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
    /** The running short job of this screen (recognition, language, preview), not the service's. */
    @Volatile private var job = 0L
    @Volatile private var closing = false
    private var worker: Thread? = null
    private var screenJob = false
    private var screenJobCancellable = false

    /** The bound installation service, null while unbound. */
    private var service: InstallService? = null
    private val waitingForService = ArrayList<(InstallService) -> Unit>()
    /** The service's running job, null when none runs. */
    private var running: ForgeJob? = null
    private var handledGeneration = 0
    private val listener = InstallService.Listener { render(it) }
    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, binder: IBinder) {
            val bound = (binder as InstallService.Local).service
            service = bound
            if (!bound.snapshot.running) InstallService.takeInterrupted(this@InstallerActivity)?.let { interrupted = it }
            bound.attach(listener)
            waitingForService.toList().forEach { it(bound) }
            waitingForService.clear()
        }

        override fun onServiceDisconnected(name: ComponentName) {
            service = null
            running = null
            refreshBusy()
        }
    }

    /** The installation state of the last inspection; null while none finished. */
    private var installation: InstallationStatus? = null
    private var inspecting = false
    private var inspectAgain = false
    /** The job whose process ended before it did, reported once by the checkpoint. */
    private var interrupted: ForgeJob? = null
    /** The update found at start was started (or offered) once; a failure leaves the button. */
    private var refreshTried = false
    /** The HUD layout choice of the update or repair in progress, for a downgrade confirmation. */
    private var maintenanceLayout = HUD_KEEP

    /** The picked cartridge, kept open until it is installed or replaced. */
    private var rom: ParcelFileDescriptor? = null
    private var romName = ""
    /** The picked document's URI when the app holds a persisted read grant to it. */
    private var romDocument: String? = null
    /** The recognized revision's label; null until [rom] is recognized. */
    private var recognizedLabel: String? = null
    /** The language codes of the card's choices, in order, and whether they are installed. */
    private var codes: List<String> = emptyList()
    private var installedChoices = false
    private var updatingChoices = false
    /** Developer installs and retries start as soon as the cartridge is recognized. */
    private var autoInstall = false
    private var autoLanguage: String? = null
    /** An interrupted restore is being undone. */
    private var recovering = false
    /** Whether this APK carries a Forge package (developer APKs may not); fixed per APK. */
    private val hasPackage by lazy { ForgePackage.available(this) }

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        StartupDiagnostics.installCrashHandler(this, "forge")
        refreshTried = state?.getBoolean(KEY_REFRESH_TRIED) ?: false
        setContentView(buildLayout())
        renderState()
        refreshInstall(false)
        // Developer installs without the picker: a file in the app's external files folder.
        val devRom = intent.getStringExtra(EXTRA_DEV_ROM)
        if (devRom != null && state == null && hasPackage) {
            autoInstall = true
            autoLanguage = intent.getStringExtra(EXTRA_DEV_LANGUAGE)
            val file = File(getExternalFilesDir(null), devRom)
            recognize(ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY), devRom, null)
        }
        if (state == null) {
            if (!openRequestedPage(intent)) offerReportAfterAbnormalExit()
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        openRequestedPage(intent)
    }

    override fun onSaveInstanceState(state: Bundle) {
        super.onSaveInstanceState(state)
        state.putBoolean(KEY_REFRESH_TRIED, refreshTried)
    }

    override fun onStart() {
        super.onStart()
        bindService(Intent(this, InstallService::class.java), connection, Context.BIND_AUTO_CREATE)
        // Back from Saves and backups or the game, or after an APK update: read the state again.
        inspect()
    }

    override fun onResume() {
        super.onResume()
        refreshBanner()
    }

    override fun onStop() {
        service?.detach(listener)
        unbindService(connection)
        service = null
        super.onStop()
    }

    override fun onDestroy() {
        closing = true
        // The service's job continues; only this screen's jobs end.
        val active = job
        if (active != 0L) ForgeNative.cancelJob(active)
        worker?.join(15_000)
        rom?.close()
        super.onDestroy()
    }

    /** The game's menu asked for a page on its way out (UX plan WP-06B "Export saves", "Share diagnostics"). */
    private fun openRequestedPage(intent: Intent): Boolean {
        val page = intent.getStringExtra(EXTRA_PAGE) ?: return false
        intent.removeExtra(EXTRA_PAGE)
        when (page) {
            PAGE_SAVES -> startActivity(Intent(this, SavesActivity::class.java))
            PAGE_DIAGNOSTICS -> openReport()
            else -> return false
        }
        return true
    }

    @Deprecated("Platform result API; the installer has no AndroidX dependency.")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data
        if (requestCode != PICK_ROM || resultCode != RESULT_OK || uri == null) return
        val document = keepGrant(uri, data?.flags ?: 0)
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
        interrupted = null
        recognize(descriptor, displayName(uri), document)
    }

    /**
     * Keeps a read grant to the picked document when its provider offers a persistable one, so
     * an interrupted installation can reopen it; releases the grants of earlier picks. Returns
     * the URI when the grant is held.
     */
    private fun keepGrant(uri: Uri, flags: Int): String? {
        if (flags and Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION == 0) return null
        return try {
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            for (held in contentResolver.persistedUriPermissions) {
                if (held.uri != uri) contentResolver.releasePersistableUriPermission(held.uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            uri.toString()
        } catch (error: SecurityException) {
            Log.i(TAG, "The provider keeps no grant to the selected document", error)
            null
        }
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
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
        @Suppress("DEPRECATION")
        startActivityForResult(pick, PICK_ROM)
    }

    /** Reopens the last installation's document (kept grant) and installs it again. */
    private fun retryInstall() {
        val last = InstallService.lastRom(this)
        val descriptor = last?.let {
            try {
                contentResolver.openFileDescriptor(Uri.parse(it.uri), "r")
            } catch (error: Exception) {
                Log.w(TAG, "Cannot reopen the last ROM", error)
                null
            }
        }
        if (last == null || descriptor == null) {
            status.text = getString(R.string.forge_retry_reselect)
            pickRom()
            return
        }
        interrupted = null
        autoInstall = true
        autoLanguage = last.language
        recognize(descriptor, last.name.ifEmpty { getString(R.string.forge_selected_file) }, last.uri)
    }

    private fun canRetryRom(): Boolean {
        val last = InstallService.lastRom(this) ?: return false
        return contentResolver.persistedUriPermissions.any { it.uri.toString() == last.uri && it.isReadPermission }
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

    /** Reads the installation state off the UI thread; runs again if asked while running. */
    private fun inspect() {
        if (!hasPackage) {
            renderState()
            return
        }
        if (inspecting) {
            inspectAgain = true
            return
        }
        inspecting = true
        refreshBusy()
        Thread({
            val result = try {
                val packageRoot = ForgePackage.prepare(this)
                val profileRoot = getExternalFilesDir(null) ?: throw IllegalStateException(getString(R.string.forge_storage_unavailable))
                val request = JSONObject()
                    .put("format", InspectFormat)
                    .put("package_root", packageRoot.absolutePath)
                    .put("android", InstallService.androidTarget(this, profileRoot))
                InstallationStatus.parse(String(ForgeNative.inspect(request.toString().toByteArray()) ?: ByteArray(0)))
            } catch (error: Exception) {
                Log.e(TAG, "Cannot inspect the installation", error)
                InstallationStatus(InstallationStatus.State.FAILED, details = error.message.orEmpty())
            }
            runOnUiThread {
                inspecting = false
                if (closing) return@runOnUiThread
                installation = result
                if (inspectAgain) {
                    inspectAgain = false
                    inspect()
                }
                renderState()
                // A newer inspection is running: act on its answer, not on this one.
                if (!inspecting) afterInspection(result)
            }
        }, "forge-inspect").start()
    }

    /** What an inspected state starts by itself: undoing a restore, the update found at start, the languages. */
    private fun afterInspection(state: InstallationStatus) {
        val plan = currentPlan()
        if (plan.card == StateCard.RESTORE_RECOVERING) recoverRestore()
        if (plan.autoRefresh && worker?.isAlive != true) {
            refreshTried = true
            startMaintenance(ForgeJob.REFRESH)
        }
        if (state.canPlay && rom == null && !screenJob) showInstalledLanguages(null)
        if (!state.canPlay && rom == null && installedChoices) card.visibility = View.GONE
    }

    private fun currentPlan(): InstallerPlan = InstallerPlanner.plan(
        hasPackage = hasPackage,
        status = installation,
        running = running,
        interrupted = interrupted,
        canRetryRom = interrupted == ForgeJob.INSTALL && canRetryRom(),
        refreshTried = refreshTried,
        developerProfile = !hasPackage && File(getExternalFilesDir(null), "TriAevum.android.launch.json").isFile,
    )

    /** The state card, Play and Select ROM, from the inspected state and the running job. */
    private fun renderState() {
        if (closing) return
        val plan = currentPlan()
        val state = installation
        val explanation = state?.explanation
        var title: String
        var body = ""
        var next = ""
        var error = false
        when (plan.card) {
            StateCard.CHECKING -> title = getString(R.string.forge_state_checking)
            StateCard.NO_PACKAGE -> title = getString(R.string.forge_no_package)
            StateCard.NOT_INSTALLED -> {
                title = getString(R.string.forge_state_not_installed)
                body = getString(R.string.forge_state_not_installed_body)
            }
            StateCard.WORKING -> {
                title = getString(
                    when (running) {
                        ForgeJob.REFRESH -> R.string.forge_state_updating
                        ForgeJob.REPAIR -> R.string.forge_state_repairing
                        else -> R.string.forge_state_installing
                    },
                )
                body = getString(R.string.forge_state_working_body)
            }
            StateCard.PREPARING_ELSEWHERE -> {
                title = getString(R.string.forge_state_preparing_elsewhere)
                body = getString(R.string.forge_state_preparing_elsewhere_body)
            }
            StateCard.READY -> {
                title = getString(R.string.forge_state_ready)
                body = listOfNotNull(
                    state?.packageVersion?.let { getString(R.string.forge_state_version, it) },
                    getString(R.string.forge_state_ready_body),
                ).joinToString(" ")
            }
            StateCard.UPDATE -> {
                val installed = state?.installedVersion
                val present = state?.presentVersion
                title = when {
                    installed != null && present != null && installed != present ->
                        getString(R.string.forge_state_update_versions, installed, present)
                    present != null -> getString(R.string.forge_state_update_version, present)
                    else -> getString(R.string.forge_state_update)
                }
                body = getString(R.string.forge_state_update_body)
            }
            StateCard.RESTORE_RECOVERING -> {
                title = getString(R.string.saves_recovering)
                body = getString(R.string.forge_state_restore_body)
            }
            StateCard.REPAIR, StateCard.NEEDS_ROM, StateCard.INCOMPATIBLE, StateCard.FAILED -> {
                error = plan.card != StateCard.NEEDS_ROM
                title = explanation?.title?.takeIf { it.isNotEmpty() } ?: getString(
                    when (plan.card) {
                        StateCard.REPAIR -> R.string.forge_state_repair
                        StateCard.NEEDS_ROM -> R.string.forge_state_needs_rom
                        StateCard.INCOMPATIBLE -> R.string.forge_state_incompatible
                        else -> R.string.forge_state_failed
                    },
                )
                body = explanation?.cause.orEmpty()
                next = when (plan.card) {
                    // Android has no ROM-assisted repair: a new installation from the ROM keeps the saves.
                    StateCard.NEEDS_ROM -> getString(
                        if (state?.canPlay == true) R.string.forge_state_needs_rom_play else R.string.forge_state_needs_rom_body,
                    )
                    StateCard.REPAIR -> getString(R.string.forge_state_repair_body)
                    else -> explanation?.nextAction.orEmpty()
                }
            }
            StateCard.INTERRUPTED_KEPT, StateCard.INTERRUPTED_NOTHING -> {
                error = true
                title = getString(R.string.forge_state_interrupted)
                body = getString(
                    if (plan.card == StateCard.INTERRUPTED_KEPT) R.string.forge_state_interrupted_kept
                    else R.string.forge_state_interrupted_nothing,
                )
                next = getString(
                    if (plan.action == StateAction.RETRY_INSTALL) R.string.forge_state_interrupted_retry
                    else R.string.forge_state_interrupted_reselect,
                )
            }
            StateCard.INTERRUPTED_UPDATE -> {
                error = true
                title = getString(R.string.forge_state_interrupted_update)
                body = getString(R.string.forge_state_interrupted_update_body)
            }
        }
        val accent = if (error) ERROR_COLOR else ACCENT_COLOR
        stateBox.background = GradientDrawable().apply {
            cornerRadius = dp(12).toFloat()
            setStroke(dp(2), accent)
            setColor((accent and 0x00FFFFFF) or 0x1A000000)
        }
        stateTitle.text = title
        stateTitle.setTextColor(accent)
        stateBody.text = listOf(body, next).filter { it.isNotEmpty() }.joinToString("\n\n")
        stateBody.visibility = if (stateBody.text.isEmpty()) View.GONE else View.VISIBLE
        val details = if (plan.card in DETAILED) explanation?.details?.takeIf { it.isNotEmpty() } ?: state?.details.orEmpty() else ""
        stateDetails.text = details
        stateDetails.visibility = if (details.isEmpty()) View.GONE else View.VISIBLE
        val action = plan.action
        stateAction.visibility = if (action == null) View.GONE else View.VISIBLE
        stateAction.text = getString(
            when (action) {
                StateAction.UPDATE -> R.string.forge_update
                StateAction.REPAIR -> R.string.forge_repair
                StateAction.RETRY_INSTALL -> R.string.forge_retry
                else -> R.string.forge_check_again
            },
        )
        stateAction.tag = action
        refreshBusy()
    }

    private fun runStateAction() {
        when (stateAction.tag as? StateAction) {
            StateAction.UPDATE -> startMaintenance(ForgeJob.REFRESH)
            StateAction.REPAIR -> startMaintenance(ForgeJob.REPAIR)
            StateAction.RETRY_INSTALL -> retryInstall()
            StateAction.CHECK_AGAIN -> inspect()
            null -> {}
        }
    }

    /**
     * Update or Repair without the ROM. The user's HUD layout is kept unless they choose the
     * standard one (owner decision 4): when theirs differs, they are asked first.
     */
    private fun startMaintenance(kind: ForgeJob) {
        val profileRoot = getExternalFilesDir(null) ?: return
        var fields: org.json.JSONArray? = null
        runJob(null, "forge-layout-preview", start = {
            request("hud_layout_preview").put("data_root", File(profileRoot, "data").absolutePath)
        }, onEvent = { event ->
            if (event.optString("event") == "succeeded") {
                val result = event.optJSONObject("result")
                if (result?.optBoolean("differs") == true) fields = result.optJSONArray("fields")
            }
        }, then = {
            val differing = fields
            if (differing == null || differing.length() == 0) {
                maintain(kind, HUD_KEEP, false)
            } else {
                askLayout(kind, differing)
            }
        })
    }

    private fun askLayout(kind: ForgeJob, fields: org.json.JSONArray) {
        val lines = (0 until fields.length()).mapNotNull { fields.optJSONObject(it) }.joinToString("\n") { field ->
            getString(R.string.forge_layout_field, field.optString("name"), shown(field.opt("yours")), shown(field.opt("standard")))
        }
        AlertDialog.Builder(this)
            .setTitle(R.string.forge_layout_question)
            .setMessage(getString(R.string.forge_layout_body) + "\n\n" + lines)
            .setPositiveButton(R.string.forge_layout_keep) { _, _ -> maintain(kind, HUD_KEEP, false) }
            .setNegativeButton(R.string.forge_layout_standard) { _, _ -> maintain(kind, HUD_STANDARD, false) }
            .setCancelable(false)
            .show()
    }

    private fun shown(value: Any?): String = when (value) {
        null, JSONObject.NULL -> "-"
        else -> value.toString()
    }

    private fun maintain(kind: ForgeJob, hudLayout: String, allowDowngrade: Boolean) {
        maintenanceLayout = hudLayout
        interrupted = null
        withService { it.maintain(kind, hudLayout, allowDowngrade) }
    }

    /** The update offers an older package than the installed one (owner decision 10). */
    private fun askDowngrade(result: JSONObject) {
        AlertDialog.Builder(this)
            .setTitle(R.string.forge_downgrade_title)
            .setMessage(getString(R.string.forge_downgrade_body, result.optString("present_version"), result.optString("installed_version")))
            .setPositiveButton(R.string.forge_downgrade_confirm) { _, _ -> maintain(ForgeJob.REFRESH, maintenanceLayout, true) }
            .setNegativeButton(R.string.forge_cancel, null)
            .show()
    }

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
                    post(refusal(error.message))
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

    /** The explanation of a refused request (its failed event is the exception message). */
    private fun refusal(message: String?): String = try {
        Explanation.of(JSONObject(message ?: ""))?.let { listOf(it.title, it.nextAction).filter(String::isNotEmpty).joinToString("\n") }
            ?: getString(R.string.forge_refused)
    } catch (_: Exception) {
        message ?: getString(R.string.forge_refused)
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
    private fun recognize(descriptor: ParcelFileDescriptor, name: String, document: String?) {
        if (worker?.isAlive == true || running != null) {
            descriptor.close()
            return
        }
        rom?.close()
        rom = descriptor
        romName = name
        romDocument = document
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
                autoLanguage?.let { chooseCode(it) }
                autoLanguage = null
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
                val explanation = Explanation.of(event)
                val message = explanation?.let { listOf(it.cause, it.nextAction).filter(String::isNotEmpty).joinToString("\n\n") }
                    ?: event.getJSONObject("error").optString("message")
                showCard(explanation?.title ?: getString(R.string.forge_not_recognized), message, emptyList(), null, installed = false, error = true)
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
        val document = romDocument
        rom = null
        interrupted = null
        refreshInstall(true)
        withService { it.install(descriptor, name, language, document) }
    }

    private fun withService(action: (InstallService) -> Unit) {
        val bound = service
        if (bound != null) action(bound) else waitingForService += action
    }

    /** Shows the installation service's state; a finished job is followed by a new inspection. */
    private fun render(snapshot: InstallService.Snapshot) {
        val wasRunning = running
        running = if (snapshot.running) snapshot.job ?: ForgeJob.INSTALL else null
        if (snapshot.running || snapshot.outcome != InstallService.Outcome.NONE) {
            status.text = snapshot.status
            detail.text = if (snapshot.running && snapshot.total > 0 && snapshot.detail.isEmpty()) {
                getString(R.string.forge_progress, snapshot.done, snapshot.total)
            } else {
                snapshot.detail
            }
        }
        if (!snapshot.running && snapshot.outcome != InstallService.Outcome.NONE && snapshot.generation != handledGeneration) {
            handledGeneration = snapshot.generation
            if (snapshot.outcome == InstallService.Outcome.SUCCEEDED && snapshot.job == ForgeJob.INSTALL) card.visibility = View.GONE
            if (snapshot.outcome == InstallService.Outcome.CONFIRMATION_REQUIRED) {
                val result = snapshot.result?.let { JSONObject(it) }
                if (result?.optString("reason") == "downgrade") askDowngrade(result)
            }
            inspect()
        } else if (wasRunning != running) {
            renderState()
        }
        refreshBanner()
        refreshBusy()
    }

    /**
     * While a job runs without notifications (permission denied or turned off), the progress
     * is only on this screen: say so, and that the job continues while the phone is locked.
     */
    private fun refreshBanner() {
        if (closing) return
        val enabled = getSystemService(NotificationManager::class.java).areNotificationsEnabled()
        banner.visibility = if (running != null && !enabled) View.VISIBLE else View.GONE
    }

    private fun openNotificationSettings() {
        val settings = Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS).putExtra(Settings.EXTRA_APP_PACKAGE, packageName)
        try {
            startActivity(settings)
        } catch (error: Exception) {
            Log.w(TAG, "Cannot open the notification settings", error)
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
                    val title = recognizedLabel ?: getString(R.string.forge_game_language)
                    showCard(title, getString(R.string.forge_language_applies), choices(document),
                        document.optString("selected"), installed = true, error = false)
                    if (select != null) status.text = getString(R.string.forge_language_saved)
                }
                "failed" -> {
                    failed = true
                    status.text = Explanation.of(event)?.title ?: event.getJSONObject("error").optString("message")
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
            val code = entry.optString("code")
            code to (NATIVE_LANGUAGE_NAMES[code] ?: entry.optString("label"))
        }
    }

    /** The card under the state: a heading, a note and the languages as large choices. */
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

    /** Enables the controls for the screen's job and the service's job, as the state allows. */
    private fun refreshBusy() {
        if (closing) return
        val plan = currentPlan()
        val busy = screenJob || running != null || recovering
        progress.visibility = if (busy || inspecting) ProgressBar.VISIBLE else ProgressBar.GONE
        select.visibility = if (plan.select || (running == null && rom != null)) Button.VISIBLE else Button.GONE
        select.isEnabled = !busy && hasPackage
        cancel.visibility = if (running != null || (screenJob && screenJobCancellable)) Button.VISIBLE else Button.GONE
        stateAction.isEnabled = !busy
        // Play only for a state that can play, never while Forge may be committing the title.
        play.visibility = if (plan.play && !busy) Button.VISIBLE else Button.GONE
        for (index in 0 until languages.childCount) languages.getChildAt(index).isEnabled = !busy
        refreshInstall(busy)
        // A recognition runs only while this screen is in front; the service's jobs do not need it.
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
     * An interrupted restore (UX plan WP-05B) is undone before anything else, on a worker
     * thread; the state is inspected again afterwards.
     */
    private fun recoverRestore() {
        if (recovering) return
        recovering = true
        refreshBusy()
        Thread({
            try {
                ForgeBackups.run(ForgeBackups.request(this, "restore_recover"))
            } catch (error: Exception) {
                Log.e(TAG, "Cannot recover the interrupted restore", error)
                post(error.message ?: error.javaClass.simpleName)
            }
            runOnUiThread {
                recovering = false
                if (!closing) inspect()
            }
        }, "restore-recover").start()
    }

    private fun cancelRunning() {
        val active = service?.takeIf { running != null }
        if (active != null) {
            active.cancel()
            return
        }
        val screen = job
        if (screen != 0L) ForgeNative.cancelJob(screen)
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
        stateTitle = TextView(this).apply {
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 20f)
            setTypeface(typeface, Typeface.BOLD)
        }
        stateBody = TextView(this).apply { setPadding(0, dp(6), 0, dp(6)) }
        stateDetails = TextView(this).apply {
            setPadding(0, 0, 0, dp(6))
            alpha = 0.7f
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            setTextIsSelectable(true)
        }
        stateAction = Button(this).apply {
            minHeight = dp(48)
            visibility = View.GONE
            setOnClickListener { runStateAction() }
        }
        stateBox = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(14), dp(16), dp(14))
            addView(stateTitle)
            addView(stateBody)
            addView(stateDetails)
            addView(stateAction, LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT).apply { gravity = Gravity.END })
        }
        body.addView(stateBox, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { bottomMargin = dp(12) })
        // Without notifications the job still runs; only its progress notification is hidden.
        banner = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(10), dp(16), dp(10))
            visibility = View.GONE
            background = GradientDrawable().apply {
                cornerRadius = dp(12).toFloat()
                setColor(0x33FFB300)
            }
            addView(TextView(context).apply { text = getString(R.string.forge_banner_notifications) })
            addView(Button(context).apply {
                text = getString(R.string.forge_banner_turn_on)
                minHeight = dp(48)
                setOnClickListener { openNotificationSettings() }
            }, LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT).apply { gravity = Gravity.END })
        }
        body.addView(banner, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { bottomMargin = dp(12) })
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
        // Secondary destinations: Saves and backups and the diagnostics report.
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
            visibility = Button.GONE
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
            visibility = Button.GONE
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

    companion object {
        private const val TAG = "TriAevumForge"
        private const val PICK_ROM = 1
        private const val REQUEST_NOTIFICATIONS = 2
        private const val PREFERENCES = "installer"
        private const val KEY_NOTIFICATIONS_ASKED = "notifications_asked"
        private const val KEY_REFRESH_TRIED = "refresh_tried"
        private const val EXTRA_DEV_ROM = "org.triaevum.android.DEV_ROM"
        /** Developer installs: the language to choose when the cartridge offers it. */
        private const val EXTRA_DEV_LANGUAGE = "org.triaevum.android.DEV_LANGUAGE"
        /** The page the game's menu asked for on its way out: [PAGE_SAVES] or [PAGE_DIAGNOSTICS]. */
        const val EXTRA_PAGE = "org.triaevum.android.INSTALLER_PAGE"
        const val PAGE_SAVES = "saves"
        const val PAGE_DIAGNOSTICS = "diagnostics"
        private const val InspectFormat = "triaevum_forge_inspect_v1"
        private const val HUD_KEEP = "keep"
        private const val HUD_STANDARD = "standard"
        private val DETAILED = setOf(StateCard.REPAIR, StateCard.NEEDS_ROM, StateCard.INCOMPATIBLE, StateCard.FAILED)
        private const val ACCENT_COLOR = 0xFF2E7DD7.toInt()
        private const val ERROR_COLOR = 0xFFD32F2F.toInt()

        /** The game languages by their own names, with accents (the runtime reports ASCII labels). */
        private val NATIVE_LANGUAGE_NAMES = mapOf(
            "en" to "English",
            "fr" to "Français",
            "de" to "Deutsch",
            "es" to "Español",
            "it" to "Italiano",
            "nl" to "Nederlands",
        )
    }
}
