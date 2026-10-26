package org.triaevum.android.diagnostics

import java.io.File
import java.io.FileOutputStream
import java.io.IOException

/** One recorded startup stage: wall-clock milliseconds and a short single-line name. */
data class Stage(val time: Long, val name: String)

/** The stages of one game process, identified by its process ID. */
data class StageSession(val pid: Int, val startedAt: Long, val stages: List<Stage>)

/**
 * The startup stages of the game process, one small text file per session: a header line
 * `triaevum_stages_v1 <pid> <millis>` and then `<millis> <stage>` lines. A new session moves the
 * previous file aside ([previous]) so the installer can still match it with Android's record of
 * how that process ended. Lines are appended without fsync: the page cache outlives a crashed
 * process, and a write costs microseconds on the thread that reaches the stage.
 */
class StageLog(private val file: File, private val clock: () -> Long = System::currentTimeMillis) {
    private val previous = File(file.parentFile, file.name + ".previous")
    private var written = 0

    /** Starts the file of a new session for process [pid]. */
    @Synchronized
    fun begin(pid: Int) {
        file.parentFile?.mkdirs()
        if (file.isFile) {
            previous.delete()
            file.renameTo(previous)
        }
        written = 0
        append("$HEADER $pid ${clock()}\n", truncate = true)
    }

    /** Appends a stage of the current session; at most [MAX_STAGES] per session. */
    @Synchronized
    fun record(stage: String) {
        if (written >= MAX_STAGES) return
        written++
        append("${clock()} ${clean(stage)}\n", truncate = false)
    }

    /** The current and the previous session, newest first, skipping unreadable files. */
    fun sessions(): List<StageSession> = listOf(file, previous).mapNotNull { read(it) }

    private fun append(line: String, truncate: Boolean) {
        try {
            FileOutputStream(file, !truncate).use { it.write(line.toByteArray(Charsets.UTF_8)) }
        } catch (_: IOException) {
            // Diagnostics never stop the game.
        }
    }

    companion object {
        const val HEADER = "triaevum_stages_v1"
        const val MAX_STAGES = 256
        const val MAX_STAGE_LENGTH = 120

        /** A single printable line of at most [MAX_STAGE_LENGTH] characters. */
        fun clean(stage: String): String =
            stage.map { if (it < ' ' || it == '\u007f') ' ' else it }.joinToString("").trim().take(MAX_STAGE_LENGTH)

        /** Parses one session file; null when it is missing or not a stage file. */
        fun read(file: File): StageSession? {
            val lines = try {
                if (!file.isFile || file.length() > 256 * 1024) return null
                file.readLines(Charsets.UTF_8)
            } catch (_: IOException) {
                return null
            }
            val header = lines.firstOrNull()?.split(' ') ?: return null
            if (header.size != 3 || header[0] != HEADER) return null
            val pid = header[1].toIntOrNull() ?: return null
            val started = header[2].toLongOrNull() ?: return null
            val stages = lines.drop(1).mapNotNull { line ->
                val space = line.indexOf(' ')
                val time = if (space > 0) line.substring(0, space).toLongOrNull() else null
                if (time == null) null else Stage(time, line.substring(space + 1))
            }
            return StageSession(pid, started, stages)
        }
    }
}
