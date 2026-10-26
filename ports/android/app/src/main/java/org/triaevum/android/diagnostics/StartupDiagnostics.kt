package org.triaevum.android.diagnostics

import android.app.ActivityManager
import android.content.Context
import android.content.pm.PackageManager
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.GLES20
import android.os.Build
import android.os.Process
import java.io.File
import java.io.IOException
import java.util.concurrent.TimeUnit
import org.triaevum.android.forge.InstallService

/**
 * Startup diagnostics, kept on this device: the game process records its startup stages
 * ([StageLog]), every process stores the structure of an uncaught Java failure, and the
 * installer (the launcher) reads Android's record of how the previous processes ended
 * (ApplicationExitInfo, API 30+) to offer a report the user can copy or share. Nothing is
 * uploaded; the report is built only when the user asks for it.
 */
object StartupDiagnostics {
    private const val PREFERENCES = "diagnostics"
    private const val LAST_SEEN = "last_seen_exit"
    /** On the first check, older exits than this are history, not news. */
    private const val FIRST_CHECK_WINDOW_MS = 24L * 60 * 60 * 1000
    private const val MAX_EXITS = 16
    private var handlerInstalled = false

    private fun directory(context: Context) = File(context.filesDir, "diagnostics")

    private fun stageLog(context: Context) = StageLog(File(directory(context), "game-stages.txt"))

    @Volatile private var game: StageLog? = null

    private fun gameLog(context: Context): StageLog =
        game ?: synchronized(this) { game ?: stageLog(context.applicationContext).also { game = it } }

    /** The game process starts a new stage session. */
    @JvmStatic
    fun beginGameSession(context: Context) {
        gameLog(context).begin(Process.myPid())
        installCrashHandler(context, "game")
    }

    /** Records a stage of the game process; cheap enough for any thread. */
    @JvmStatic
    fun stage(context: Context, stage: String) = gameLog(context).record(stage)

    /**
     * Stores the structure of an uncaught Java failure (classes and frames, no messages) for the
     * next report, then lets the previous handler end the process as before.
     */
    @JvmStatic
    @Synchronized
    fun installCrashHandler(context: Context, process: String) {
        if (handlerInstalled) return
        handlerInstalled = true
        val file = File(directory(context.applicationContext), "java-failure-$process.txt")
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, error ->
            try {
                file.parentFile?.mkdirs()
                file.writeText(
                    "process $process pid ${Process.myPid()} at ${System.currentTimeMillis()} thread ${thread.name}\n" +
                        StartupReport.throwable(error),
                )
            } catch (_: Throwable) {
                // The failure itself matters more than its record.
            }
            if (previous != null) previous.uncaughtException(thread, error) else Process.killProcess(Process.myPid())
        }
    }

    /** The app's recent process exits, newest first; empty before Android 11. */
    fun exits(context: Context): List<ExitRecord> {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return emptyList()
        return try {
            context.getSystemService(ActivityManager::class.java)
                .getHistoricalProcessExitReasons(context.packageName, 0, MAX_EXITS)
                .map { info ->
                    ExitRecord(
                        process = info.processName,
                        pid = info.pid,
                        timestamp = info.timestamp,
                        reason = info.reason,
                        status = info.status,
                        description = info.description,
                        pssKb = info.pss,
                        rssKb = info.rss,
                    )
                }
                .sortedByDescending { it.timestamp }
        } catch (_: RuntimeException) {
            emptyList()
        }
    }

    /**
     * Abnormal exits recorded since the previous check, newest first; marks them seen. The
     * installer calls this once per launch, from a worker thread.
     */
    fun newAbnormalExits(context: Context): List<ExitRecord> {
        val all = exits(context)
        val preferences = context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
        val lastSeen = preferences.getLong(LAST_SEEN, -1L)
        val since = if (lastSeen >= 0) lastSeen else System.currentTimeMillis() - FIRST_CHECK_WINDOW_MS
        val newest = all.maxOfOrNull { it.timestamp } ?: lastSeen.coerceAtLeast(0)
        preferences.edit().putLong(LAST_SEEN, maxOf(newest, lastSeen)).apply()
        return all.filter { it.abnormal && it.timestamp > since }
    }

    /** The game's recorded stage sessions, newest first. */
    fun sessions(context: Context): List<StageSession> = stageLog(context).sessions()

    /** Builds the full report (GPU query and log tail included); call it off the UI thread. */
    fun buildReport(context: Context): String {
        val failures = listOf("game", "forge").mapNotNull { process ->
            val file = File(directory(context), "java-failure-$process.txt")
            try {
                if (file.isFile && file.length() < 64 * 1024) file.readText() else null
            } catch (_: IOException) {
                null
            }
        }
        return StartupReport.format(
            device = device(context),
            exits = exits(context),
            sessions = sessions(context),
            javaFailure = failures.joinToString("\n").ifBlank { null },
            installState = InstallService.checkpointSummary(context),
            logTail = logTail(),
            generatedAt = System.currentTimeMillis(),
        )
    }

    private fun device(context: Context): DeviceSummary {
        val app = try {
            val info = context.packageManager.getPackageInfo(context.packageName, 0)
            "${info.versionName} (${info.longVersionCode})"
        } catch (_: PackageManager.NameNotFoundException) {
            "unknown"
        }
        val soc = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            "${Build.SOC_MANUFACTURER} ${Build.SOC_MODEL}"
        } else {
            Build.HARDWARE
        }
        val memory = try {
            val info = ActivityManager.MemoryInfo()
            context.getSystemService(ActivityManager::class.java).getMemoryInfo(info)
            "${info.totalMem shr 20} MiB total, ${info.availMem shr 20} MiB available"
        } catch (_: RuntimeException) {
            null
        }
        val vulkan = context.packageManager.systemAvailableFeatures.mapNotNull { feature ->
            when (feature.name) {
                "android.hardware.vulkan.version" -> "version ${StartupReport.vulkanVersion(feature.version)}"
                "android.hardware.vulkan.level" -> "hardware level ${feature.version}"
                "android.hardware.vulkan.compute" -> "compute level ${feature.version}"
                "android.software.vulkan.deqp.level" -> "dEQP level ${feature.version}"
                else -> null
            }
        }.sorted()
        return DeviceSummary(
            app = app,
            device = "${Build.MANUFACTURER} ${Build.MODEL} (${Build.DEVICE})",
            android = "${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})",
            abis = Build.SUPPORTED_ABIS.joinToString(", "),
            soc = soc,
            memory = memory,
            gpu = gpuName(),
            vulkan = vulkan,
        )
    }

    /** The GPU's vendor, renderer and driver version string, from a throwaway 1x1 GLES context. */
    private fun gpuName(): String? = try {
        val display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
        val version = IntArray(2)
        if (display == EGL14.EGL_NO_DISPLAY || !EGL14.eglInitialize(display, version, 0, version, 1)) {
            null
        } else {
            try {
                val configs = arrayOfNulls<EGLConfig>(1)
                val count = IntArray(1)
                val wanted = intArrayOf(
                    EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
                    EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT,
                    EGL14.EGL_NONE,
                )
                if (!EGL14.eglChooseConfig(display, wanted, 0, configs, 0, 1, count, 0) || count[0] == 0) {
                    null
                } else {
                    val config = configs[0]
                    val gl = EGL14.eglCreateContext(
                        display, config, EGL14.EGL_NO_CONTEXT,
                        intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE), 0,
                    )
                    val surface = EGL14.eglCreatePbufferSurface(
                        display, config, intArrayOf(EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE), 0,
                    )
                    try {
                        if (!EGL14.eglMakeCurrent(display, surface, surface, gl)) {
                            null
                        } else {
                            "${GLES20.glGetString(GLES20.GL_VENDOR)} ${GLES20.glGetString(GLES20.GL_RENDERER)}, " +
                                GLES20.glGetString(GLES20.GL_VERSION)
                        }
                    } finally {
                        EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
                        EGL14.eglDestroySurface(display, surface)
                        EGL14.eglDestroyContext(display, gl)
                    }
                }
            } finally {
                EGL14.eglTerminate(display)
            }
        }
    } catch (_: Throwable) {
        null
    }

    /**
     * The tail of the app's own log buffer (Android shows an app only its own entries, including
     * those of the process that ended), or empty when the system refuses.
     */
    private fun logTail(): List<String> = try {
        val process = ProcessBuilder("logcat", "-d", "-t", "${StartupReport.MAX_LOG_LINES * 2}", "-v", "threadtime")
            .redirectErrorStream(true)
            .start()
        val lines = ArrayDeque<String>()
        val reader = Thread {
            try {
                process.inputStream.bufferedReader().useLines { sequence ->
                    sequence.forEach { line ->
                        synchronized(lines) {
                            lines.addLast(line)
                            if (lines.size > StartupReport.MAX_LOG_LINES) lines.removeFirst()
                        }
                    }
                }
            } catch (_: IOException) {
                // Partial output is still useful.
            }
        }.apply { start() }
        if (!process.waitFor(5, TimeUnit.SECONDS)) process.destroy()
        reader.join(1000)
        synchronized(lines) { lines.toList() }
    } catch (_: Exception) {
        emptyList()
    }
}
