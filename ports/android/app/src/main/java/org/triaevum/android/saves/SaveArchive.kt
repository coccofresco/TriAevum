package org.triaevum.android.saves

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.nio.file.AtomicMoveNotSupportedException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.StandardCopyOption
import java.security.MessageDigest
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.UUID
import java.util.zip.Deflater
import java.util.zip.ZipEntry
import java.util.zip.ZipException
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/**
 * Where the game keeps its saves: [saveData] is the launch profile's `--save-data` folder
 * (`data/savedata`, the game's own save files) and [saveStates] the save-state folder
 * (`data/savestates` unless the player chose another). Backups and import staging live under
 * [dataRoot], on the same filesystem as the saves, so publishing a file is a rename.
 */
data class SaveLayout(val dataRoot: File, val saveData: File, val saveStates: File) {
    val backups: File get() = File(dataRoot, "save-backups")

    fun target(file: StagedFile): File = when (file.kind) {
        SaveKind.SAVE_DATA -> File(saveData, file.relative)
        SaveKind.SAVE_STATE -> File(saveStates, file.relative)
    }

    companion object {
        fun standard(dataRoot: File) =
            SaveLayout(dataRoot, File(dataRoot, "savedata"), File(dataRoot, "savestates"))
    }
}

enum class SaveKind { SAVE_DATA, SAVE_STATE }

/** Why an archive or a save folder was refused; [SaveArchiveException.subject] names the file. */
enum class SaveProblem {
    NOT_AN_ARCHIVE,
    UNSAFE_PATH,
    UNSUPPORTED_FILE,
    DUPLICATE,
    TOO_LARGE,
    TOO_MANY,
    NOT_A_SAVE_STATE,
    DAMAGED,
    EMPTY,
    NOTHING_TO_EXPORT,
    TARGET_NOT_FILE,
}

class SaveArchiveException(val problem: SaveProblem, val subject: String = "") :
    IOException(if (subject.isEmpty()) problem.name else "${problem.name}: $subject")

data class ExportFile(val archivePath: String, val file: File)

/** The files an export writes, and the names left out (too large or not save files). */
data class ExportSet(val files: List<ExportFile>, val skipped: List<String>) {
    val saveFiles: Int get() = files.count { it.archivePath.startsWith(SaveArchive.SAVEDATA_PREFIX) }
    val saveStates: Int get() = files.size - saveFiles
}

/** One validated file of an import, already written to the staging folder. */
data class StagedFile(
    val archivePath: String,
    val kind: SaveKind,
    val relative: String,
    val staged: File,
    val size: Long,
    val sha256: String,
)

/** A validated import waiting for the user's confirmation; nothing in the save folders changed yet. */
class ImportPlan(val staging: File, val files: List<StagedFile>, val verifiedByManifest: Boolean) {
    val saveFiles: Int get() = files.count { it.kind == SaveKind.SAVE_DATA }
    val saveStates: Int get() = files.count { it.kind == SaveKind.SAVE_STATE }
}

data class ImportResult(val installed: Int, val replaced: Int, val backup: File?)

/**
 * Save export and import as one ZIP archive through the system document picker.
 *
 * An export holds `savedata/<path>` (the game's save files), optionally
 * `savestates/<name>.oot3dsav`, and a last entry [MANIFEST]: the line [FORMAT], then one
 * `<sha256> <size> <path>` line per file. An import accepts such an archive (every file must
 * match the manifest exactly) or a plain archive of the game's own save files (`save00.bin`..,
 * `system.dat`, at the top or in one folder, as emulators store them) and plain `.oot3dsav`
 * states at the top. Paths are checked before anything is written, sizes are bounded while
 * reading whatever an entry claims, and a save state must start with the runtime's magic.
 *
 * The import is staged first ([stage]); [commit] then moves every file it replaces into a
 * dated backup folder before publishing the new one, and restores them if a step fails.
 * Other saves are kept. The ideas follow Ocarina ReTuned's Android save import/export.
 */
object SaveArchive {
    const val MANIFEST = "triaevum-saves.manifest"
    const val FORMAT = "triaevum_android_saves_v1"
    const val SAVEDATA_PREFIX = "savedata/"
    const val SAVESTATES_PREFIX = "savestates/"
    const val SAVESTATE_EXTENSION = ".oot3dsav"
    val SAVESTATE_MAGIC = byteArrayOf(0x4F, 0x4F, 0x54, 0x33, 0x44, 0x53, 0x56, 0x00) // "OOT3DSV\0"

    const val MAX_SAVE_FILE_BYTES = 4L shl 20
    const val MAX_SAVESTATE_BYTES = 512L shl 20
    const val MAX_TOTAL_BYTES = 8L shl 30
    const val MAX_ENTRIES = 512
    const val MAX_DEPTH = 4
    const val MAX_COMPONENT = 128
    const val MAX_MANIFEST_BYTES = 256 * 1024
    const val KEPT_BACKUPS = 5
    private const val STAGING_PREFIX = ".save-import-"
    /** The runtime's crash dumps (desktop) are not saves. */
    private const val CRASHES = "crashes"
    private val GAME_SAVE_NAME = Regex("save[0-9]{2}\\.bin|system\\.dat")

    // ---- Export ----

    /** The files an export would write: every save file, and the save states when asked. */
    fun collect(layout: SaveLayout, includeStates: Boolean): ExportSet {
        val files = ArrayList<ExportFile>()
        val skipped = ArrayList<String>()
        walk(layout.saveData, "", 1, files, skipped)
        if (includeStates) {
            layout.saveStates.listFiles().orEmpty().sortedBy { it.name }.forEach { file ->
                if (!file.name.endsWith(SAVESTATE_EXTENSION) || !regular(file)) return@forEach
                val path = SAVESTATES_PREFIX + file.name
                if (safeComponent(file.name) && file.length() <= MAX_SAVESTATE_BYTES && hasMagic(file)) {
                    files += ExportFile(path, file)
                } else {
                    skipped += path
                }
            }
        }
        if (files.size > MAX_ENTRIES) throw SaveArchiveException(SaveProblem.TOO_MANY)
        return ExportSet(files, skipped)
    }

    private fun walk(folder: File, relative: String, depth: Int, files: MutableList<ExportFile>, skipped: MutableList<String>) {
        for (child in folder.listFiles().orEmpty().sortedBy { it.name }) {
            val path = relative + child.name
            if (Files.isSymbolicLink(child.toPath())) continue
            if (child.isDirectory) {
                if (depth < MAX_DEPTH && !(depth == 1 && child.name == CRASHES)) {
                    walk(child, "$path/", depth + 1, files, skipped)
                }
                continue
            }
            if (!child.isFile) continue
            if (safeComponent(child.name) && child.length() <= MAX_SAVE_FILE_BYTES) {
                files += ExportFile(SAVEDATA_PREFIX + path, child)
            } else {
                skipped += SAVEDATA_PREFIX + path
            }
        }
    }

    /** Writes [set] and its manifest as a ZIP; [onFile] gets the count of files written so far. */
    fun write(set: ExportSet, output: OutputStream, onFile: (Int) -> Unit = {}) {
        if (set.files.isEmpty()) throw SaveArchiveException(SaveProblem.NOTHING_TO_EXPORT)
        val manifest = StringBuilder(FORMAT).append('\n')
        ZipOutputStream(output.buffered(1 shl 16)).use { zip ->
            zip.setLevel(Deflater.BEST_SPEED)
            set.files.forEachIndexed { index, file ->
                val entry = ZipEntry(file.archivePath).apply { time = file.file.lastModified() }
                zip.putNextEntry(entry)
                val digest = MessageDigest.getInstance("SHA-256")
                var size = 0L
                file.file.inputStream().use { input ->
                    val buffer = ByteArray(1 shl 16)
                    while (true) {
                        val read = input.read(buffer)
                        if (read < 0) break
                        digest.update(buffer, 0, read)
                        zip.write(buffer, 0, read)
                        size += read
                    }
                }
                zip.closeEntry()
                manifest.append(hex(digest.digest())).append(' ').append(size).append(' ').append(file.archivePath)
                    .append('\n')
                onFile(index + 1)
            }
            zip.putNextEntry(ZipEntry(MANIFEST))
            zip.write(manifest.toString().toByteArray(Charsets.UTF_8))
            zip.closeEntry()
        }
    }

    // ---- Import ----

    /**
     * Validates [input] and writes its files to a new staging folder under [SaveLayout.dataRoot].
     * Throws [SaveArchiveException] (staging removed) when the archive is refused.
     */
    fun stage(input: InputStream, layout: SaveLayout): ImportPlan {
        layout.dataRoot.mkdirs()
        val staging = File(layout.dataRoot, STAGING_PREFIX + UUID.randomUUID())
        if (!staging.mkdirs()) throw IOException("Cannot create $staging")
        try {
            return stageInto(input, staging)
        } catch (error: Exception) {
            staging.deleteRecursively()
            throw error
        }
    }

    private fun stageInto(input: InputStream, staging: File): ImportPlan {
        val files = LinkedHashMap<String, StagedFile>()
        var manifest: String? = null
        var entries = 0
        var total = 0L
        var bare = false
        try {
            ZipInputStream(input.buffered(1 shl 16)).use { zip ->
                while (true) {
                    val entry = zip.nextEntry ?: break
                    if (++entries > MAX_ENTRIES) throw SaveArchiveException(SaveProblem.TOO_MANY)
                    val path = entry.name
                    checkPath(path)
                    if (entry.isDirectory) continue
                    if (path == MANIFEST) {
                        if (manifest != null) throw SaveArchiveException(SaveProblem.DUPLICATE, path)
                        manifest = String(readBounded(zip, MAX_MANIFEST_BYTES.toLong(), path), Charsets.UTF_8)
                        continue
                    }
                    val (kind, relative, plain) = classify(path)
                    bare = bare || plain
                    val key = "$kind/$relative"
                    if (files.containsKey(key)) throw SaveArchiveException(SaveProblem.DUPLICATE, path)
                    val limit = if (kind == SaveKind.SAVE_STATE) MAX_SAVESTATE_BYTES else MAX_SAVE_FILE_BYTES
                    val staged = File(staging, "${files.size}.part")
                    val (size, sha) = copyBounded(zip, staged, minOf(limit, MAX_TOTAL_BYTES - total), path)
                    total += size
                    if (kind == SaveKind.SAVE_STATE && !hasMagic(staged)) {
                        throw SaveArchiveException(SaveProblem.NOT_A_SAVE_STATE, path)
                    }
                    files[key] = StagedFile(path, kind, relative, staged, size, sha)
                }
            }
        } catch (error: ZipException) {
            // Android 14+ refuses traversal names itself ("Invalid zip entry path").
            val problem = when {
                error.message.orEmpty().contains("path", ignoreCase = true) -> SaveProblem.UNSAFE_PATH
                entries == 0 -> SaveProblem.NOT_AN_ARCHIVE
                else -> SaveProblem.DAMAGED
            }
            throw SaveArchiveException(problem)
        }
        if (entries == 0) throw SaveArchiveException(SaveProblem.NOT_AN_ARCHIVE)
        if (files.isEmpty()) throw SaveArchiveException(SaveProblem.EMPTY)
        manifest?.let { text ->
            if (bare) throw SaveArchiveException(SaveProblem.DAMAGED, MANIFEST)
            verifyManifest(text, files.values)
        }
        return ImportPlan(staging, files.values.toList(), manifest != null)
    }

    /** The staged files that would replace an existing file. */
    fun replacements(plan: ImportPlan, layout: SaveLayout): List<StagedFile> =
        plan.files.filter { layout.target(it).exists() }

    /**
     * Publishes [plan]: each replaced file first moves to `save-backups/<time>/<archive path>`.
     * On failure every published file is removed and every backup restored.
     */
    fun commit(plan: ImportPlan, layout: SaveLayout, now: Long = System.currentTimeMillis()): ImportResult {
        val backup = uniqueFolder(layout.backups, SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(Date(now)))
        val done = ArrayList<Pair<File, File?>>()
        try {
            for (file in plan.files) {
                val target = layout.target(file)
                checkContained(target, if (file.kind == SaveKind.SAVE_DATA) layout.saveData else layout.saveStates, file)
                target.parentFile?.mkdirs()
                var saved: File? = null
                if (target.exists() || Files.isSymbolicLink(target.toPath())) {
                    if (!regular(target)) throw SaveArchiveException(SaveProblem.TARGET_NOT_FILE, file.archivePath)
                    saved = File(backup, file.archivePath).also { it.parentFile?.mkdirs() }
                    move(target, saved)
                }
                try {
                    move(file.staged, target)
                } catch (error: IOException) {
                    if (saved != null) move(saved, target)
                    throw error
                }
                done += target to saved
            }
        } catch (error: IOException) {
            for ((target, saved) in done.asReversed()) {
                target.delete()
                if (saved != null) runCatching { move(saved, target) }
            }
            if (backup.walkBottomUp().none { it.isFile }) backup.deleteRecursively()
            throw error
        }
        discard(plan)
        val replaced = done.count { it.second != null }
        val kept = if (replaced == 0) {
            backup.deleteRecursively()
            null
        } else {
            backup
        }
        pruneBackups(layout, KEPT_BACKUPS)
        return ImportResult(done.size, replaced, kept)
    }

    fun discard(plan: ImportPlan) {
        plan.staging.deleteRecursively()
    }

    /** Removes staging folders an interrupted import left behind. */
    fun cleanStaleStaging(layout: SaveLayout) {
        layout.dataRoot.listFiles().orEmpty()
            .filter { it.isDirectory && it.name.startsWith(STAGING_PREFIX) }
            .forEach { it.deleteRecursively() }
    }

    /** Keeps the newest [keep] backup folders (their names sort by time). */
    fun pruneBackups(layout: SaveLayout, keep: Int) {
        layout.backups.listFiles().orEmpty().filter { it.isDirectory }.sortedByDescending { it.name }.drop(keep)
            .forEach { it.deleteRecursively() }
    }

    // ---- Validation helpers ----

    /** A relative ZIP path of safe components, never absolute, never leaving its folder. */
    internal fun checkPath(path: String) {
        val trimmed = path.removeSuffix("/")
        if (trimmed.isEmpty() || path.startsWith("/") || path.length > 512) {
            throw SaveArchiveException(SaveProblem.UNSAFE_PATH, path)
        }
        if (!trimmed.split('/').all { safeComponent(it) }) throw SaveArchiveException(SaveProblem.UNSAFE_PATH, path)
    }

    internal fun safeComponent(name: String): Boolean =
        name.isNotEmpty() && name.length <= MAX_COMPONENT && name != "." && name != ".." &&
            name.none { it < ' ' || it == '\u007f' || it in "\\:*?\"<>|" }

    /** The kind and folder-relative path of a file entry, and whether it is a plain (manifest-less) form. */
    private fun classify(path: String): Triple<SaveKind, String, Boolean> {
        val parts = path.split('/')
        if (path.startsWith(SAVEDATA_PREFIX)) {
            val relative = path.removePrefix(SAVEDATA_PREFIX)
            val depth = parts.size - 1
            if (depth > MAX_DEPTH || parts[1] == CRASHES) throw SaveArchiveException(SaveProblem.UNSUPPORTED_FILE, path)
            return Triple(SaveKind.SAVE_DATA, relative, false)
        }
        if (path.startsWith(SAVESTATES_PREFIX)) {
            if (parts.size != 2 || !parts[1].endsWith(SAVESTATE_EXTENSION)) {
                throw SaveArchiveException(SaveProblem.UNSUPPORTED_FILE, path)
            }
            return Triple(SaveKind.SAVE_STATE, parts[1], false)
        }
        val name = parts.last()
        if (parts.size <= 2 && GAME_SAVE_NAME.matches(name)) return Triple(SaveKind.SAVE_DATA, name, true)
        if (parts.size == 1 && name.endsWith(SAVESTATE_EXTENSION)) return Triple(SaveKind.SAVE_STATE, name, true)
        throw SaveArchiveException(SaveProblem.UNSUPPORTED_FILE, path)
    }

    private fun verifyManifest(text: String, files: Collection<StagedFile>) {
        val lines = text.split('\n').filter { it.isNotEmpty() }
        if (lines.firstOrNull() != FORMAT) throw SaveArchiveException(SaveProblem.DAMAGED, MANIFEST)
        val listed = HashMap<String, Pair<String, Long>>()
        for (line in lines.drop(1)) {
            val first = line.indexOf(' ')
            val second = if (first < 0) -1 else line.indexOf(' ', first + 1)
            if (second < 0) throw SaveArchiveException(SaveProblem.DAMAGED, MANIFEST)
            val sha = line.substring(0, first)
            val size = line.substring(first + 1, second).toLongOrNull()
            val path = line.substring(second + 1)
            if (sha.length != 64 || size == null || listed.put(path, sha to size) != null) {
                throw SaveArchiveException(SaveProblem.DAMAGED, MANIFEST)
            }
        }
        if (listed.size != files.size) throw SaveArchiveException(SaveProblem.DAMAGED, MANIFEST)
        for (file in files) {
            val (sha, size) = listed[file.archivePath] ?: throw SaveArchiveException(SaveProblem.DAMAGED, file.archivePath)
            if (sha != file.sha256 || size != file.size) throw SaveArchiveException(SaveProblem.DAMAGED, file.archivePath)
        }
    }

    private fun readBounded(input: InputStream, limit: Long, subject: String): ByteArray {
        val out = java.io.ByteArrayOutputStream()
        val buffer = ByteArray(1 shl 14)
        while (true) {
            val read = input.read(buffer)
            if (read < 0) break
            if (out.size() + read > limit) throw SaveArchiveException(SaveProblem.TOO_LARGE, subject)
            out.write(buffer, 0, read)
        }
        return out.toByteArray()
    }

    /** Copies at most [limit] bytes, whatever the entry declared; returns the size and SHA-256. */
    private fun copyBounded(input: InputStream, target: File, limit: Long, subject: String): Pair<Long, String> {
        val digest = MessageDigest.getInstance("SHA-256")
        var size = 0L
        FileOutputStream(target).use { output ->
            val buffer = ByteArray(1 shl 16)
            while (true) {
                val read = input.read(buffer)
                if (read < 0) break
                size += read
                if (size > limit) throw SaveArchiveException(SaveProblem.TOO_LARGE, subject)
                digest.update(buffer, 0, read)
                output.write(buffer, 0, read)
            }
            output.fd.sync()
        }
        return size to hex(digest.digest())
    }

    private fun hasMagic(file: File): Boolean = try {
        file.inputStream().use { input ->
            val head = ByteArray(SAVESTATE_MAGIC.size)
            var filled = 0
            while (filled < head.size) {
                val read = input.read(head, filled, head.size - filled)
                if (read < 0) break
                filled += read
            }
            filled == head.size && head.contentEquals(SAVESTATE_MAGIC)
        }
    } catch (_: IOException) {
        false
    }

    private fun regular(file: File): Boolean = Files.isRegularFile(file.toPath(), LinkOption.NOFOLLOW_LINKS)

    private fun checkContained(target: File, root: File, file: StagedFile) {
        val base = root.canonicalFile
        val resolved = target.canonicalFile
        if (!resolved.path.startsWith(base.path + File.separator)) {
            throw SaveArchiveException(SaveProblem.UNSAFE_PATH, file.archivePath)
        }
    }

    private fun uniqueFolder(parent: File, name: String): File {
        var candidate = File(parent, name)
        var suffix = 2
        while (candidate.exists()) candidate = File(parent, "$name-${suffix++}")
        return candidate
    }

    private fun move(source: File, target: File) {
        try {
            Files.move(source.toPath(), target.toPath(), StandardCopyOption.ATOMIC_MOVE)
        } catch (_: AtomicMoveNotSupportedException) {
            // A save-state folder on another filesystem: copy, then remove the source.
            Files.move(source.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING)
        }
    }

    private fun hex(bytes: ByteArray): String = bytes.joinToString("") { "%02x".format(it.toInt() and 0xff) }
}
