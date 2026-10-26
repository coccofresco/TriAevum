package org.triaevum.android.diagnostics

import java.time.Instant

/** How one of the app's processes ended, as Android recorded it (ApplicationExitInfo, API 30+). */
data class ExitRecord(
    val process: String,
    val pid: Int,
    val timestamp: Long,
    val reason: Int,
    val status: Int,
    val description: String?,
    val pssKb: Long,
    val rssKb: Long,
) {
    val abnormal: Boolean get() = ExitReasons.abnormal(reason)
}

/** The device facts of a report; values the platform cannot provide are null. */
data class DeviceSummary(
    val app: String,
    val device: String,
    val android: String,
    val abis: String,
    val soc: String?,
    val memory: String?,
    val gpu: String?,
    val vulkan: List<String>,
)

/**
 * ApplicationExitInfo reasons, by value (stable platform API constants), so the report logic
 * stays testable on the JVM without the Android framework.
 */
object ExitReasons {
    const val EXIT_SELF = 1
    const val SIGNALED = 2
    const val LOW_MEMORY = 3
    const val CRASH = 4
    const val CRASH_NATIVE = 5
    const val ANR = 6
    const val INITIALIZATION_FAILURE = 7
    const val EXCESSIVE_RESOURCE_USAGE = 9
    const val USER_REQUESTED = 10
    const val DEPENDENCY_DIED = 12
    const val FREEZER = 14

    fun name(reason: Int): String = when (reason) {
        EXIT_SELF -> "exited normally"
        SIGNALED -> "killed by a signal"
        LOW_MEMORY -> "killed for low memory"
        CRASH -> "Java crash"
        CRASH_NATIVE -> "native crash"
        ANR -> "not responding (ANR)"
        INITIALIZATION_FAILURE -> "initialization failure"
        EXCESSIVE_RESOURCE_USAGE -> "excessive resource usage"
        USER_REQUESTED -> "stopped by the user"
        DEPENDENCY_DIED -> "dependency died"
        FREEZER -> "killed while frozen"
        else -> "reason $reason"
    }

    /** Exits worth a report: crashes, hangs, start failures and system kills under pressure. */
    fun abnormal(reason: Int): Boolean = reason in setOf(
        LOW_MEMORY, CRASH, CRASH_NATIVE, ANR, INITIALIZATION_FAILURE, EXCESSIVE_RESOURCE_USAGE,
    )
}

/**
 * The plain-text report the user can copy or share. It is technical evidence for an issue
 * report, so it is not translated. It holds device and app facts, Android's exit records, the
 * game's last startup stages and a bounded log tail; never saves, ROM names or URIs.
 */
object StartupReport {
    const val MAX_LOG_LINES = 200
    const val MAX_LINE_LENGTH = 300
    const val MAX_REPORT_CHARS = 96 * 1024

    fun format(
        device: DeviceSummary,
        exits: List<ExitRecord>,
        sessions: List<StageSession>,
        javaFailure: String?,
        installState: String?,
        logTail: List<String>,
        generatedAt: Long,
    ): String {
        val out = StringBuilder()
        out.append("TriAevum Android diagnostics report\n")
        out.append("Generated: ").append(time(generatedAt)).append('\n')
        out.append("App: ").append(device.app).append('\n')
        out.append("Device: ").append(device.device).append('\n')
        out.append("Android: ").append(device.android).append('\n')
        out.append("ABIs: ").append(device.abis).append('\n')
        device.soc?.let { out.append("SoC: ").append(it).append('\n') }
        device.memory?.let { out.append("Memory: ").append(it).append('\n') }
        out.append("GPU: ").append(device.gpu ?: "unavailable").append('\n')
        if (device.vulkan.isEmpty()) out.append("Vulkan: no feature advertised\n")
        device.vulkan.forEach { out.append("Vulkan: ").append(it).append('\n') }
        installState?.let { out.append("Last installation: ").append(it).append('\n') }

        out.append("\n== Process exits (newest first) ==\n")
        if (exits.isEmpty()) out.append("No exit records (Android 11 or newer records them).\n")
        for (exit in exits) {
            out.append(time(exit.timestamp)).append("  ").append(exit.process).append(" pid ").append(exit.pid)
                .append(": ").append(ExitReasons.name(exit.reason)).append(", status ").append(exit.status)
            if (exit.pssKb > 0 || exit.rssKb > 0) out.append(", pss ").append(exit.pssKb / 1024).append(" MiB, rss ")
                .append(exit.rssKb / 1024).append(" MiB")
            exit.description?.takeIf { it.isNotBlank() }?.let { out.append(" (").append(line(it)).append(')') }
            out.append('\n')
        }

        out.append("\n== Game startup stages ==\n")
        if (sessions.isEmpty()) out.append("No game session recorded.\n")
        for (session in sessions) {
            val exit = exits.firstOrNull { it.pid == session.pid && it.timestamp >= session.startedAt }
            out.append("Session pid ").append(session.pid).append(" started ").append(time(session.startedAt))
            out.append(if (exit == null) "" else ", ended: ${ExitReasons.name(exit.reason)}").append('\n')
            for (stage in session.stages) {
                out.append("  +").append(seconds(stage.time - session.startedAt)).append(" s  ").append(stage.name)
                    .append('\n')
            }
            if (session.stages.isNotEmpty()) out.append("  Last stage: ").append(session.stages.last().name).append('\n')
        }

        javaFailure?.takeIf { it.isNotBlank() }?.let { out.append("\n== Java failure ==\n").append(it.trimEnd()).append('\n') }

        out.append("\n== Log tail ==\n")
        if (logTail.isEmpty()) out.append("Unavailable.\n")
        logTail.takeLast(MAX_LOG_LINES).forEach { out.append(line(it)).append('\n') }
        return if (out.length <= MAX_REPORT_CHARS) out.toString() else out.substring(0, MAX_REPORT_CHARS) + "\n[truncated]\n"
    }

    /** One line of the summary shown before the report: what ended, how, at which stage. */
    fun summary(exit: ExitRecord, sessions: List<StageSession>): String {
        val stage = sessions.firstOrNull { it.pid == exit.pid }?.stages?.lastOrNull()?.name
        return ExitReasons.name(exit.reason) + (if (stage == null) "" else ", last stage: $stage")
    }

    /** The structural part of a Java failure: classes and frames, never messages or paths. */
    fun throwable(error: Throwable, maxCauses: Int = 4, maxFrames: Int = 16): String {
        val out = StringBuilder()
        var current: Throwable? = error
        var causes = 0
        while (current != null && causes < maxCauses) {
            out.append(if (causes == 0) "" else "Caused by: ").append(current.javaClass.name).append('\n')
            current.stackTrace.take(maxFrames).forEach { frame ->
                out.append("  at ").append(frame.className).append('.').append(frame.methodName).append(':')
                    .append(frame.lineNumber).append('\n')
            }
            current = current.cause
            causes++
        }
        return out.toString()
    }

    /** The Vulkan version a `android.hardware.vulkan.version` feature advertises. */
    fun vulkanVersion(encoded: Int): String =
        "${encoded ushr 22}.${(encoded ushr 12) and 0x3ff}.${encoded and 0xfff}"

    private fun time(millis: Long): String = Instant.ofEpochMilli(millis).toString()

    private fun seconds(millis: Long): String = String.format(java.util.Locale.ROOT, "%.3f", millis / 1000.0)

    private fun line(text: String): String =
        text.map { if (it < ' ' && it != '\t') ' ' else it }.joinToString("").take(MAX_LINE_LENGTH)
}
