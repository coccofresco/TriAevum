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
 * Runs the Forge "install" job as a foreground service with an ongoing notification, so a
 * preparation of several minutes continues while the screen is locked or the installer is in
 * the background (Samsung's freezer stops background processes without one).
 *
 * The service lives in the installer's process (":forge"): the job handle is an in-process
 * pointer of libtriaevum_forge.so and Forge calls the runtime library's helpers in this process.
 * The installer binds to it (same process, a local binder) and re-attaches after it is
 * recreated; the service keeps the latest [Snapshot] and delivers it on the main thread.
 *
 * Type "dataSync": the job imports the user's picked file into app storage and processes it,
 * which is the documented use of that type. "shortService" would end the job after about three
 * minutes, and "mediaProcessing" (API 35) is for media transcoding. Android 15 limits dataSync
 * to six hours a day, far above an installation; [onTimeout] cancels the job if it is reached.
 * A partial wake lock keeps the CPU running with the screen off, only while the job runs.
 */
class InstallService : Service() {
    enum class Outcome { NONE, SUCCEEDED, FAILED, CANCELLED }

    /** What the installer shows; replaced on every change, never mutated. */
    data class Snapshot(
        val running: Boolean = false,
        val outcome: Outcome = Outcome.NONE,
        val status: String = "",
        val detail: String = "",
        val done: Long = 0,
        val total: Long = -1,
        /** Increments with every installation this process started. */
        val generation: Int = 0,
    )

    fun interface Listener {
        fun onSnapshot(snapshot: Snapshot)
    }

    inner class Local : Binder() {
        val service: InstallService get() = this@InstallService
    }

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

    override fun onBind(intent: Intent): IBinder = Local()

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_CANCEL -> cancel()
            ACTION_RUN -> if (snapshot.running) promote()
        }
        if (!snapshot.running) stopSelf(startId)
        // The descriptor cannot survive the process: never restart without the installer.
        return START_NOT_STICKY
    }

    /** Android 15's daily dataSync budget ran out: cancel and leave the foreground. */
    override fun onTimeout(startId: Int, fgsType: Int) {
        Log.w(TAG, "Foreground time limit reached; cancelling the installation")
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
     * on) with the chosen [language]. False when an installation is already running.
     */
    fun install(descriptor: ParcelFileDescriptor, name: String, language: String?): Boolean {
        if (snapshot.running) {
            descriptor.close()
            return false
        }
        synchronized(jobLock) { cancelRequested = false }
        publish(Snapshot(running = true, status = getString(R.string.forge_preparing, name), generation = snapshot.generation + 1))
        writeCheckpoint(STATE_RUNNING, "")
        // Started and promoted while the installer is in front, as Android 12+ requires.
        try {
            startForegroundService(Intent(this, InstallService::class.java).setAction(ACTION_RUN))
            promote()
        } catch (error: IllegalStateException) {
            // ForegroundServiceStartNotAllowedException: the installer left the screen first.
            Log.w(TAG, "Installation runs without a foreground service", error)
        }
        acquireWakeLock()
        worker = Thread({ run(descriptor, language) }, "forge-install").also { it.start() }
        return true
    }

    /** Cancels the running installation after Forge's current step. */
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

    private fun run(descriptor: ParcelFileDescriptor, language: String?) {
        var terminal: JSONObject? = null
        try {
            val request = request(descriptor, language)
            if (request != null) {
                val handle = synchronized(jobLock) {
                    if (cancelRequested) 0L else ForgeNative.startJob(request.toString().toByteArray()).also { job = it }
                }
                if (handle != 0L) terminal = pump(handle)
            }
        } catch (error: IllegalArgumentException) {
            terminal = failure(error.message ?: getString(R.string.forge_refused))
        } catch (error: Exception) {
            Log.e(TAG, "Installation failed", error)
            terminal = failure(error.message ?: error.javaClass.simpleName)
        } finally {
            val ended = terminal ?: if (synchronized(jobLock) { cancelRequested }) {
                JSONObject().put("event", "cancelled")
            } else {
                failure(getString(R.string.forge_refused))
            }
            main.post { finish(ended) }
        }
    }

    /** The request, built off the main thread (the package copy can take a while), or null after closing [descriptor]. */
    private fun request(descriptor: ParcelFileDescriptor, language: String?): JSONObject? {
        val packageRoot = try {
            ForgePackage.prepare(this)
        } catch (error: Exception) {
            Log.e(TAG, "Cannot prepare the Forge package", error)
            descriptor.close()
            throw IllegalArgumentException(getString(R.string.forge_package_error, error.message.orEmpty()))
        }
        val profileRoot = getExternalFilesDir(null)
        if (profileRoot == null || synchronized(jobLock) { cancelRequested }) {
            descriptor.close()
            if (profileRoot == null) throw IllegalArgumentException(getString(R.string.forge_storage_unavailable))
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
            "stage" -> current.copy(status = event.optString("message"), detail = "", done = 0, total = -1)
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

    private fun finish(event: JSONObject) {
        val final = when (event.optString("event")) {
            "succeeded" -> snapshot.copy(outcome = Outcome.SUCCEEDED, status = getString(R.string.forge_ready), detail = "")
            "cancelled" -> snapshot.copy(outcome = Outcome.CANCELLED, status = getString(R.string.forge_cancelled), detail = "")
            else -> {
                val error = event.optJSONObject("error") ?: JSONObject()
                snapshot.copy(
                    outcome = Outcome.FAILED,
                    status = error.optString("message"),
                    detail = getString(R.string.forge_failed_stage, error.optString("stage"), error.optString("code")),
                )
            }
        }.copy(running = false, done = 0, total = -1)
        releaseWakeLock()
        stopForeground(STOP_FOREGROUND_REMOVE)
        writeCheckpoint(
            when (final.outcome) {
                Outcome.SUCCEEDED -> STATE_SUCCEEDED
                Outcome.CANCELLED -> STATE_CANCELLED
                else -> STATE_FAILED
            },
            final.detail.ifEmpty { final.status },
        )
        // Nobody is looking at the installer: say how it ended in a notification instead.
        if (listeners.isEmpty()) notifications().notify(RESULT_NOTIFICATION_ID, resultNotification(final))
        publish(final)
        worker = null
        stopSelf()
    }

    private fun failure(message: String): JSONObject = JSONObject().put("event", "failed")
        .put("error", JSONObject().put("message", message).put("stage", "preflight").put("code", "internal"))

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
        return Notification.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentTitle(getString(R.string.forge_notification_title))
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
        .setContentText(state.status)
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

    private fun writeCheckpoint(state: String, message: String) {
        getSharedPreferences(CHECKPOINT, Context.MODE_PRIVATE).edit()
            .putString(KEY_STATE, state)
            .putString(KEY_MESSAGE, message.take(300))
            .putLong(KEY_TIME, System.currentTimeMillis())
            .apply()
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
        private const val KEY_MESSAGE = "message"
        private const val KEY_TIME = "time"
        private const val STATE_RUNNING = "running"
        private const val STATE_SUCCEEDED = "succeeded"
        private const val STATE_FAILED = "failed"
        private const val STATE_CANCELLED = "cancelled"
        private const val STATE_INTERRUPTED = "interrupted"

        /** The Android target of a Forge request: libraries, launch profile root and helper scratch folder. */
        fun androidTarget(context: Context, profileRoot: File): JSONObject = JSONObject()
            .put("native_library_dir", context.applicationInfo.nativeLibraryDir)
            .put("profile_root", profileRoot.absolutePath)
            .put("cache_dir", context.cacheDir.absolutePath)

        /**
         * Call with no installation running in this process: true when the checkpoint says an
         * installation was running, i.e. its process ended before the job did. Marks it seen.
         */
        fun takeInterrupted(context: Context): Boolean {
            val preferences = context.getSharedPreferences(CHECKPOINT, Context.MODE_PRIVATE)
            if (preferences.getString(KEY_STATE, null) != STATE_RUNNING) return false
            preferences.edit().putString(KEY_STATE, STATE_INTERRUPTED).apply()
            return true
        }

        /** The checkpoint for a diagnostics report: state, time and message. */
        fun checkpointSummary(context: Context): String? {
            val preferences = context.getSharedPreferences(CHECKPOINT, Context.MODE_PRIVATE)
            val state = preferences.getString(KEY_STATE, null) ?: return null
            val time = java.time.Instant.ofEpochMilli(preferences.getLong(KEY_TIME, 0))
            val message = preferences.getString(KEY_MESSAGE, "").orEmpty()
            return "$state at $time" + if (message.isEmpty()) "" else " ($message)"
        }
    }
}
