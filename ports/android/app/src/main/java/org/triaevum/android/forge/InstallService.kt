package org.triaevum.android.forge

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.drawable.Icon
import android.os.Binder
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.os.PowerManager
import android.os.SystemClock
import android.util.Log
import java.io.File
import org.json.JSONObject
import org.triaevum.android.R
import org.triaevum.android.diagnostics.StartupDiagnostics

/**
 * Runs Forge's long jobs as a foreground service with an ongoing notification, so a preparation
 * of several minutes continues while the screen is locked or the installer is in the background
 * (Samsung's freezer stops background processes without one): "install" from the user's picked
 * cartridge, and "refresh" (an APK update bound to the prepared game, UX plan WP-04A) and
 * "repair", which need no ROM.
 *
 * The service lives in the installer's process (":forge"): the job handle is an in-process
 * pointer of libtriaevum_forge.so and Forge calls the runtime library's helpers in this process.
 * The installer binds to it (same process, a local binder) and re-attaches after it is
 * recreated; the service keeps the latest [Snapshot] and delivers it on the main thread.
 *
 * A job handle never survives its process. A checkpoint (shared preferences) records the running
 * job, and for an installation the picked document's URI when the provider let the app keep a
 * grant to it: after the process died mid-job the installer reads it ([takeInterrupted]),
 * inspects the installation and offers to retry or to select the ROM again.
 *
 * Type "dataSync": the job imports the user's picked file into app storage and processes it,
 * which is the documented use of that type. "shortService" would end the job after about three
 * minutes, and "mediaProcessing" (API 35) is for media transcoding. Android 15 limits dataSync
 * to six hours a day, far above an installation; [onTimeout] cancels the job if it is reached.
 * A partial wake lock keeps the CPU running with the screen off, only while the job runs.
 * Without the notification permission the service still runs; only its notification is hidden
 * (the installer then shows a banner).
 */
class InstallService : Service() {
    enum class Outcome { NONE, SUCCEEDED, FAILED, CANCELLED, CONFIRMATION_REQUIRED }

    /** What the installer shows; replaced on every change, never mutated. */
    data class Snapshot(
        val running: Boolean = false,
        val job: ForgeJob? = null,
        val outcome: Outcome = Outcome.NONE,
        val status: String = "",
        val detail: String = "",
        val done: Long = 0,
        val total: Long = -1,
        /** Increments with every job this process started. */
        val generation: Int = 0,
        /** The terminal event's result (refresh, repair), as JSON text. */
        val result: String? = null,
    )

    fun interface Listener {
        fun onSnapshot(snapshot: Snapshot)
    }

    inner class Local : Binder() {
        val service: InstallService get() = this@InstallService
    }

    /** The document an installation reads, kept so an interrupted one can be retried. */
    data class RomDocument(val uri: String, val name: String, val language: String?)

    private val main = Handler(Looper.getMainLooper())
    private val listeners = LinkedHashSet<Listener>()
    private val jobLock = Any()
    @Volatile var snapshot = Snapshot()
        private set
    private var job = 0L
    private var cancelRequested = false
    private var worker: Thread? = null
    private var wakeLock: PowerManager.WakeLock? = null
    private var lastNotified = 0L

    override fun onCreate() {
        super.onCreate()
        StartupDiagnostics.installCrashHandler(this, "forge")
        val channel = NotificationChannel(CHANNEL, getString(R.string.forge_channel_name), NotificationManager.IMPORTANCE_LOW)
        channel.description = getString(R.string.forge_channel_description)
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
    }

    /** The installer's language before Android 13 (decision 8; [ForgeLocale]). */
    override fun attachBaseContext(base: Context) = super.attachBaseContext(ForgeLocale.wrap(base))

    override fun onBind(intent: Intent): IBinder = Local()

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_CANCEL -> cancel()
            ACTION_RUN -> if (snapshot.running) promote()
        }
        if (!snapshot.running) stopSelf(startId)
        // The descriptor and the job handle cannot survive the process: never restart without the installer.
        return START_NOT_STICKY
    }

    /** Android 15's daily dataSync budget ran out: cancel and leave the foreground. */
    override fun onTimeout(startId: Int, fgsType: Int) {
        Log.w(TAG, "Foreground time limit reached; cancelling the job")
        cancel()
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    override fun onDestroy() {
        cancel()
        releaseWakeLock()
        super.onDestroy()
    }

    /** Main thread: [listener] receives the current snapshot now and every later one. */
    fun attach(listener: Listener) {
        listeners += listener
        listener.onSnapshot(snapshot)
    }

    fun detach(listener: Listener) {
        listeners -= listener
    }

    /**
     * Main thread: installs the recognized cartridge [descriptor] (the service owns it from now
     * on) with the chosen [language]. [document] is the picked document's URI when the app holds
     * a persisted grant to it. False when a job is already running.
     */
    fun install(descriptor: ParcelFileDescriptor, name: String, language: String?, document: String?): Boolean {
        if (snapshot.running) {
            descriptor.close()
            return false
        }
        val rom = document?.let { RomDocument(it, name, language) }
        return begin(ForgeJob.INSTALL, getString(R.string.forge_preparing, name), rom) {
            installRequest(descriptor, language)
        }.also { if (!it) descriptor.close() }
    }

    /**
     * Main thread: runs "refresh" (bind the new APK to the prepared game) or "repair" without the
     * ROM. [hudLayout] is "keep" or "standard" (owner decision 4); [allowDowngrade] is the user's
     * confirmation of an older package (decision 10). False when a job is already running.
     */
    fun maintain(kind: ForgeJob, hudLayout: String, allowDowngrade: Boolean): Boolean {
        require(kind != ForgeJob.INSTALL)
        val status = getString(if (kind == ForgeJob.REFRESH) R.string.forge_updating else R.string.forge_repairing)
        return begin(kind, status, null) { maintenanceRequest(kind, hudLayout, allowDowngrade) }
    }

    /**
     * Main thread: downloads the HD interface pack into the app's data folder. Only ever started
     * by the user's tap (owner decision 5); the next installation applies it. False when a job is
     * already running.
     */
    fun downloadHdPack(): Boolean =
        begin(ForgeJob.HD_DOWNLOAD, getString(R.string.hd_downloading), null) { hdPackRequest(ForgeJob.HD_DOWNLOAD, null) }

    /**
     * Main thread: imports the HD pack archive the user picked ([descriptor], owned by the service
     * from now on); Forge keeps it only when it is the original. False when a job is already running.
     */
    fun importHdPack(descriptor: ParcelFileDescriptor): Boolean {
        if (snapshot.running) {
            descriptor.close()
            return false
        }
        return begin(ForgeJob.HD_IMPORT, getString(R.string.hd_importing), null) {
            hdPackRequest(ForgeJob.HD_IMPORT, descriptor)
        }.also { if (!it) descriptor.close() }
    }

    private fun begin(kind: ForgeJob, status: String, rom: RomDocument?, request: () -> JSONObject?): Boolean {
        if (snapshot.running) return false
        synchronized(jobLock) { cancelRequested = false }
        publish(Snapshot(running = true, job = kind, status = status, generation = snapshot.generation + 1))
        writeCheckpoint(STATE_RUNNING, kind, "", rom)
        // Started and promoted while the installer is in front, as Android 12+ requires.
        try {
            startForegroundService(Intent(this, InstallService::class.java).setAction(ACTION_RUN))
            promote()
        } catch (error: IllegalStateException) {
            // ForegroundServiceStartNotAllowedException: the installer left the screen first.
            Log.w(TAG, "Job runs without a foreground service", error)
        }
        acquireWakeLock()
        worker = Thread({ run(request) }, "forge-${kind.code}").also { it.start() }
        return true
    }

    /** Cancels the running job after Forge's current step. */
    fun cancel() {
        synchronized(jobLock) {
            if (!snapshot.running || cancelRequested) return
            cancelRequested = true
            if (job != 0L) ForgeNative.cancelJob(job)
        }
        main.post {
            if (snapshot.running) publish(snapshot.copy(status = getString(R.string.forge_cancelling), detail = ""))
        }
    }

    private fun promote() {
        startForeground(NOTIFICATION_ID, progressNotification(snapshot), ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
    }

    private fun run(build: () -> JSONObject?) {
        var terminal: JSONObject? = null
        try {
            val request = build()
            if (request != null) {
                val handle = synchronized(jobLock) {
                    if (cancelRequested) 0L else ForgeNative.startJob(request.toString().toByteArray()).also { job = it }
                }
                if (handle != 0L) terminal = pump(handle)
            }
        } catch (error: IllegalArgumentException) {
            terminal = refused(error.message)
        } catch (error: Exception) {
            Log.e(TAG, "Forge job failed", error)
            terminal = failure(error.message ?: error.javaClass.simpleName)
        } finally {
            val ended = terminal ?: if (synchronized(jobLock) { cancelRequested }) {
                JSONObject().put("event", "cancelled")
            } else {
                failure(getString(R.string.forge_refused))
            }
            // Explanations are read here, off the main thread, from the shared catalogue.
            // A failure of this app's own (no package, no storage) already says what happened.
            val explanation = if (ended.optString("event") == "succeeded" || ended.optBoolean(LOCAL)) null else Explanation.of(ended)
            val notices = noticeTitles(ended)
            main.post { finish(ended, explanation, notices) }
        }
    }

    /** The package folder (copied first when the APK changed) and the profile folder. */
    private fun roots(): Pair<File, File> {
        val packageRoot = try {
            ForgePackage.prepare(this)
        } catch (error: Exception) {
            Log.e(TAG, "Cannot prepare the Forge package", error)
            throw IllegalStateException(getString(R.string.forge_package_error, error.message.orEmpty()))
        }
        val profileRoot = getExternalFilesDir(null)
            ?: throw IllegalStateException(getString(R.string.forge_storage_unavailable))
        return packageRoot to profileRoot
    }

    /** The install request, built off the main thread (the package copy can take a while), or null after closing [descriptor]. */
    private fun installRequest(descriptor: ParcelFileDescriptor, language: String?): JSONObject? {
        val (packageRoot, profileRoot) = try {
            roots()
        } catch (error: Exception) {
            descriptor.close()
            throw error
        }
        if (synchronized(jobLock) { cancelRequested }) {
            descriptor.close()
            return null
        }
        return JSONObject()
            .put("format", "triaevum_forge_request_v1")
            .put("job", "install")
            .put("source", JSONObject().put("fd", descriptor.detachFd()))
            .put("source_kind", "rom")
            .put("package_root", packageRoot.absolutePath)
            .put("download_hd_textures", false)
            .put("android", androidTarget(this, profileRoot))
            .apply { if (language != null) put("game_language", language) }
    }

    /** The "ui_pack_download" or "ui_pack_import" request, or null after closing [descriptor]. */
    private fun hdPackRequest(kind: ForgeJob, descriptor: ParcelFileDescriptor?): JSONObject? {
        val (packageRoot, profileRoot) = try {
            roots()
        } catch (error: Exception) {
            descriptor?.close()
            throw error
        }
        if (synchronized(jobLock) { cancelRequested }) {
            descriptor?.close()
            return null
        }
        return JSONObject()
            .put("format", "triaevum_forge_request_v1")
            .put("job", kind.code)
            .put("package_root", packageRoot.absolutePath)
            .put("android", androidTarget(this, profileRoot))
            .apply { if (descriptor != null) put("source", JSONObject().put("fd", descriptor.detachFd())) }
    }

    private fun maintenanceRequest(kind: ForgeJob, hudLayout: String, allowDowngrade: Boolean): JSONObject? {
        val (packageRoot, profileRoot) = roots()
        if (synchronized(jobLock) { cancelRequested }) return null
        return JSONObject()
            .put("format", "triaevum_forge_request_v1")
            .put("job", kind.code)
            .put("package_root", packageRoot.absolutePath)
            .put("android", androidTarget(this, profileRoot))
            .put("hud_layout", hudLayout)
            .apply { if (kind == ForgeJob.REFRESH) put("allow_downgrade", allowDowngrade) }
    }

    /** Polls events until the terminal one, then destroys the job; returns the terminal event. */
    private fun pump(handle: Long): JSONObject {
        try {
            while (true) {
                val bytes = ForgeNative.pollEvent(handle, 250) ?: continue
                val event = JSONObject(String(bytes))
                when (event.optString("event")) {
                    "succeeded", "failed", "cancelled" -> return event
                    else -> main.post { progress(event) }
                }
            }
        } finally {
            synchronized(jobLock) {
                job = 0L
                ForgeNative.destroyJob(handle)
            }
        }
    }

    private fun progress(event: JSONObject) {
        if (!snapshot.running) return
        val current = snapshot
        val next = when (event.optString("event")) {
            // The HD pack jobs keep this app's translated status; installations show Forge's stages.
            "stage" -> current.copy(
                status = if (current.job?.hdPack == true) current.status else event.optString("message"),
                detail = "", done = 0, total = -1,
            )
            "report" -> current.copy(detail = event.optString("message"))
            "progress" -> current.copy(
                done = event.optLong("done"),
                total = if (event.isNull("total")) -1 else event.optLong("total", -1),
            )
            else -> return
        }
        val stageChanged = next.status != current.status
        publish(next)
        val now = SystemClock.elapsedRealtime()
        if (stageChanged || now - lastNotified >= NOTIFY_INTERVAL_MS) {
            lastNotified = now
            notifications().notify(NOTIFICATION_ID, progressNotification(next))
        }
    }

    /** The titles of a succeeded event's notices (graphics preparation incomplete, ...). */
    private fun noticeTitles(event: JSONObject): List<String> {
        val notices = event.optJSONArray("notices") ?: return emptyList()
        return (0 until notices.length()).mapNotNull { index ->
            notices.optJSONObject(index)?.let { Explanation.of(it)?.title }
        }
    }

    private fun finish(event: JSONObject, explanation: Explanation?, notices: List<String>) {
        val kind = snapshot.job
        val result = event.optJSONObject("result")
        val final = when (event.optString("event")) {
            "succeeded" -> succeeded(kind, result ?: JSONObject(), notices)
            "cancelled" -> snapshot.copy(
                outcome = Outcome.CANCELLED,
                status = getString(
                    when {
                        kind == ForgeJob.INSTALL -> R.string.forge_cancelled
                        kind?.hdPack == true -> R.string.hd_cancelled
                        else -> R.string.forge_job_cancelled
                    },
                ),
                detail = "",
            )
            else -> {
                val error = event.optJSONObject("error") ?: JSONObject()
                snapshot.copy(
                    outcome = Outcome.FAILED,
                    status = explanation?.title?.takeIf { it.isNotEmpty() } ?: error.optString("message"),
                    detail = if (explanation != null) {
                        listOf(explanation.cause, explanation.nextAction).filter { it.isNotEmpty() }.joinToString("\n")
                    } else {
                        ""
                    },
                )
            }
        }.copy(running = false, done = 0, total = -1, result = result?.toString())
        releaseWakeLock()
        stopForeground(STOP_FOREGROUND_REMOVE)
        writeCheckpoint(
            when (final.outcome) {
                Outcome.SUCCEEDED, Outcome.CONFIRMATION_REQUIRED -> STATE_SUCCEEDED
                Outcome.CANCELLED -> STATE_CANCELLED
                else -> STATE_FAILED
            },
            kind,
            final.detail.ifEmpty { final.status },
            null,
        )
        // Nobody is looking at the installer: say how it ended in a notification instead.
        if (listeners.isEmpty()) notifications().notify(RESULT_NOTIFICATION_ID, resultNotification(final))
        publish(final)
        worker = null
        stopSelf()
    }

    /** The texts of a succeeded job: what it did, what changed in the settings and its notices. */
    private fun succeeded(kind: ForgeJob?, result: JSONObject, notices: List<String>): Snapshot {
        if (result.optString("status") == "confirmation_required") {
            return snapshot.copy(outcome = Outcome.CONFIRMATION_REQUIRED, status = "", detail = "")
        }
        if (kind?.hdPack == true) {
            return snapshot.copy(outcome = Outcome.SUCCEEDED, status = getString(R.string.hd_done), detail = "")
        }
        val changes = result.optJSONArray("config_changes")
        val changed = (0 until (changes?.length() ?: 0)).mapNotNull { changes?.optJSONObject(it) }
        val kept = changed.all { it.optString("change") == "created" }
        val keptText = getString(if (kept) R.string.forge_kept_all else R.string.forge_kept_saves)
        val status = when (kind) {
            ForgeJob.REFRESH -> if (result.optString("status") == "unchanged") {
                getString(R.string.forge_already_current)
            } else {
                val version = result.optJSONObject("present")?.let { if (it.isNull("version")) "" else it.optString("version") }.orEmpty()
                if (version.isEmpty()) getString(R.string.forge_updated, keptText)
                else getString(R.string.forge_updated_to, version, keptText)
            }
            ForgeJob.REPAIR -> getString(R.string.forge_repaired, keptText)
            else -> getString(R.string.forge_ready)
        }
        val lines = ArrayList<String>()
        if (kind != ForgeJob.INSTALL && changed.isNotEmpty()) {
            val list = changed.joinToString("; ") { change ->
                val file = change.optString("file")
                when (change.optString("change")) {
                    "created" -> getString(R.string.forge_change_created, file)
                    "standard_hud_layout" -> getString(R.string.forge_change_layout)
                    "language_selected" -> getString(R.string.forge_change_language)
                    "removed" -> getString(R.string.forge_change_removed, file)
                    else -> getString(R.string.forge_change_other, file)
                }
            }
            lines += getString(if (kind == ForgeJob.REFRESH) R.string.forge_update_changed else R.string.forge_repair_changed, list)
        }
        lines += notices
        return snapshot.copy(outcome = Outcome.SUCCEEDED, status = status, detail = lines.joinToString("\n"))
    }

    /** A request Forge refused carries its failed event as the exception message. */
    private fun refused(message: String?): JSONObject = try {
        JSONObject(message ?: "").takeIf { it.optString("event") == "failed" } ?: failure(message ?: getString(R.string.forge_refused))
    } catch (_: Exception) {
        failure(message ?: getString(R.string.forge_refused))
    }

    private fun failure(message: String): JSONObject = JSONObject().put("event", "failed").put(LOCAL, true)
        .put("error", JSONObject().put("message", message).put("stage", JSONObject.NULL).put("code", "internal"))

    private fun publish(next: Snapshot) {
        snapshot = next
        for (listener in listeners.toList()) listener.onSnapshot(next)
    }

    private fun notifications() = getSystemService(NotificationManager::class.java)

    private fun openInstaller(): PendingIntent = PendingIntent.getActivity(
        this, 0,
        Intent(this, InstallerActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP),
        PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
    )

    private fun progressNotification(state: Snapshot): Notification {
        val cancel = PendingIntent.getService(
            this, 1, Intent(this, InstallService::class.java).setAction(ACTION_CANCEL),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val determinate = state.total > 0
        val percent = if (determinate) ((state.done.coerceIn(0, state.total) * 100) / state.total).toInt() else 0
        val title = getString(
            when {
                state.job == ForgeJob.INSTALL || state.job == null -> R.string.forge_notification_title
                state.job.hdPack -> R.string.hd_notification_title
                else -> R.string.forge_notification_update
            },
        )
        return Notification.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentTitle(title)
            .setContentText(state.status)
            .setStyle(Notification.BigTextStyle().bigText(listOf(state.status, state.detail).filter { it.isNotEmpty() }.joinToString("\n")))
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setCategory(Notification.CATEGORY_PROGRESS)
            .setProgress(100, percent, !determinate)
            .setContentIntent(openInstaller())
            .addAction(Notification.Action.Builder(null as Icon?, getString(R.string.forge_cancel), cancel).build())
            .apply {
                // Show the notification at once instead of after Android 12's ten-second grace.
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE)
                }
            }
            .build()
    }

    private fun resultNotification(state: Snapshot): Notification = Notification.Builder(this, CHANNEL)
        .setSmallIcon(
            if (state.outcome == Outcome.SUCCEEDED) android.R.drawable.stat_sys_download_done
            else android.R.drawable.stat_notify_error,
        )
        .setContentTitle(getString(R.string.forge_notification_title))
        .setContentText(state.status.ifEmpty { getString(R.string.forge_open_installer) })
        .setAutoCancel(true)
        .setContentIntent(openInstaller())
        .build()

    private fun acquireWakeLock() {
        if (wakeLock?.isHeld == true) return
        wakeLock = getSystemService(PowerManager::class.java)
            .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "TriAevum:forge-install")
            .apply {
                setReferenceCounted(false)
                acquire(WAKE_LOCK_TIMEOUT_MS)
            }
    }

    private fun releaseWakeLock() {
        wakeLock?.let { if (it.isHeld) it.release() }
        wakeLock = null
    }

    /** The checkpoint; [rom] is kept until the next installation replaces it. */
    private fun writeCheckpoint(state: String, kind: ForgeJob?, message: String, rom: RomDocument?) {
        val editor = getSharedPreferences(CHECKPOINT, Context.MODE_PRIVATE).edit()
            .putString(KEY_STATE, state)
            .putString(KEY_JOB, kind?.code)
            .putString(KEY_MESSAGE, message.take(300))
            .putLong(KEY_TIME, System.currentTimeMillis())
        if (kind == ForgeJob.INSTALL && state == STATE_RUNNING) {
            editor.putString(KEY_ROM_URI, rom?.uri).putString(KEY_ROM_NAME, rom?.name).putString(KEY_ROM_LANGUAGE, rom?.language)
        }
        // Synchronous: the process may die right after a job starts.
        editor.commit()
    }

    companion object {
        private const val TAG = "TriAevumForge"
        private const val CHANNEL = "forge-install"
        private const val NOTIFICATION_ID = 1
        private const val RESULT_NOTIFICATION_ID = 2
        private const val NOTIFY_INTERVAL_MS = 500L
        /** Released when the job ends; the timeout only bounds a lost release. */
        private const val WAKE_LOCK_TIMEOUT_MS = 3L * 60 * 60 * 1000
        private const val ACTION_RUN = "org.triaevum.android.forge.RUN"
        private const val ACTION_CANCEL = "org.triaevum.android.forge.CANCEL"
        private const val CHECKPOINT = "forge-install"
        private const val KEY_STATE = "state"
        private const val KEY_JOB = "job"
        private const val KEY_MESSAGE = "message"
        private const val KEY_TIME = "time"
        private const val KEY_ROM_URI = "rom_uri"
        private const val KEY_ROM_NAME = "rom_name"
        private const val KEY_ROM_LANGUAGE = "rom_language"
        private const val STATE_RUNNING = "running"
        private const val STATE_SUCCEEDED = "succeeded"
        private const val STATE_FAILED = "failed"
        private const val STATE_CANCELLED = "cancelled"
        private const val STATE_INTERRUPTED = "interrupted"
        /** Marks a failed event this app made itself, not Forge. */
        private const val LOCAL = "triaevum_android_local"

        /** The Android target of a Forge request: libraries, launch profile root and helper scratch folder. */
        fun androidTarget(context: Context, profileRoot: File): JSONObject = JSONObject()
            .put("native_library_dir", context.applicationInfo.nativeLibraryDir)
            .put("profile_root", profileRoot.absolutePath)
            .put("cache_dir", context.cacheDir.absolutePath)

        /**
         * Call with no job running in this process: the job the checkpoint says was running,
         * i.e. whose process ended before the job did, or null. Marks it seen. A checkpoint of
         * an older build without a job name was an installation.
         */
        fun takeInterrupted(context: Context): ForgeJob? {
            val preferences = context.getSharedPreferences(CHECKPOINT, Context.MODE_PRIVATE)
            if (preferences.getString(KEY_STATE, null) != STATE_RUNNING) return null
            preferences.edit().putString(KEY_STATE, STATE_INTERRUPTED).commit()
            return ForgeJob.of(preferences.getString(KEY_JOB, null)) ?: ForgeJob.INSTALL
        }

        /** The document of the last installation, when the app kept a grant to it. */
        fun lastRom(context: Context): RomDocument? {
            val preferences = context.getSharedPreferences(CHECKPOINT, Context.MODE_PRIVATE)
            val uri = preferences.getString(KEY_ROM_URI, null) ?: return null
            return RomDocument(uri, preferences.getString(KEY_ROM_NAME, null).orEmpty(), preferences.getString(KEY_ROM_LANGUAGE, null))
        }

        /** The checkpoint for a diagnostics report: state, job, time and message. */
        fun checkpointSummary(context: Context): String? {
            val preferences = context.getSharedPreferences(CHECKPOINT, Context.MODE_PRIVATE)
            val state = preferences.getString(KEY_STATE, null) ?: return null
            val job = preferences.getString(KEY_JOB, null) ?: "install"
            val time = java.time.Instant.ofEpochMilli(preferences.getLong(KEY_TIME, 0))
            val message = preferences.getString(KEY_MESSAGE, "").orEmpty()
            return "$job $state at $time" + if (message.isEmpty()) "" else " ($message)"
        }
    }
}
