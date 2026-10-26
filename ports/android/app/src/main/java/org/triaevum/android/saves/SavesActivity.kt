package org.triaevum.android.saves

import android.app.Activity
import android.app.ActivityManager
import android.app.AlertDialog
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.util.Log
import android.util.TypedValue
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import org.json.JSONObject
import org.triaevum.android.R

/**
 * Backups and save transfer through the system document picker; no storage permission.
 * "Back up your files" writes Forge's full backup ([ForgeBackups]: saves, save states, settings
 * and the touch controls). "Restore" reads either such a backup (Forge verifies it, previews what
 * changes and restores with a journal and a snapshot) or a plain save archive ([SaveArchive]).
 * It runs in the installer's process, so it can tell whether the game process is running: a
 * restore or import then waits, because the game would write over the files. Nothing changes
 * until the user confirms what is replaced, and replaced files are kept first.
 */
class SavesActivity : Activity() {
    private lateinit var summary: TextView
    private lateinit var status: TextView
    private lateinit var progress: ProgressBar
    private lateinit var buttons: List<Button>
    @Volatile private var closing = false
    private var worker: Thread? = null
    private var includeStates = false
    /** A validated import awaiting confirmation. */
    private var pending: ImportPlan? = null
    /** A Forge backup copied from the picker, awaiting confirmation. */
    private var pendingBackup: File? = null

    /** The installer's language before Android 13 (owner decision 8). */
    override fun attachBaseContext(base: android.content.Context) =
        super.attachBaseContext(org.triaevum.android.forge.ForgeLocale.wrap(base))

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        setContentView(buildLayout())
        val layout = layout()
        if (layout == null) {
            status.text = getString(R.string.saves_storage_unavailable)
            buttons.forEach { it.isEnabled = false }
            return
        }
        background(null) {
            SaveArchive.cleanStaleStaging(layout)
            if (ForgeBackups.pendingRestore(this)) {
                ui { status.text = getString(R.string.saves_recovering) }
                ForgeBackups.run(ForgeBackups.request(this, "restore_recover"))
                ui { status.text = getString(R.string.saves_recovered) }
            }
            val saves = SaveArchive.collect(layout, includeStates = true)
            ui { summary.text = getString(R.string.saves_summary, saves.saveFiles, saves.saveStates) }
        }
    }

    override fun onDestroy() {
        closing = true
        worker?.join(5_000)
        pending?.let { SaveArchive.discard(it) }
        pending = null
        pendingBackup?.delete()
        pendingBackup = null
        super.onDestroy()
    }

    /** The launch profile's save folders; the save-state folder the player chose, when set. */
    private fun layout(): SaveLayout? {
        val dataRoot = File(getExternalFilesDir(null) ?: return null, "data")
        val standard = SaveLayout.standard(dataRoot)
        val preferences = File(dataRoot, "config/savestates.json")
        val chosen = try {
            if (preferences.isFile) JSONObject(preferences.readText()).optString("Directory") else ""
        } catch (error: Exception) {
            Log.w(TAG, "Unreadable save-state preferences; using the standard folder", error)
            ""
        }
        val folder = File(chosen)
        return if (chosen.isNotEmpty() && folder.isAbsolute && folder.isDirectory) standard.copy(saveStates = folder) else standard
    }

    private fun export(states: Boolean) {
        includeStates = states
        val stamp = SimpleDateFormat("yyyyMMdd-HHmm", Locale.ROOT).format(Date())
        val create = Intent(Intent.ACTION_CREATE_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType("application/zip")
            .putExtra(Intent.EXTRA_TITLE, "TriAevum-saves-$stamp.zip")
        @Suppress("DEPRECATION")
        startActivityForResult(create, EXPORT)
    }

    private fun pickBackupDestination() {
        val stamp = SimpleDateFormat("yyyyMMdd-HHmm", Locale.ROOT).format(Date())
        val create = Intent(Intent.ACTION_CREATE_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType("application/zip")
            .putExtra(Intent.EXTRA_TITLE, "TriAevum-backup-$stamp.zip")
        @Suppress("DEPRECATION")
        startActivityForResult(create, BACKUP)
    }

    private fun pickImport() {
        if (gameRunning()) {
            status.text = getString(R.string.saves_game_running)
            return
        }
        // Providers label ZIPs inconsistently: accept any and let the archive check decide.
        val open = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
            .putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/zip", "application/x-zip-compressed", "*/*"))
        @Suppress("DEPRECATION")
        startActivityForResult(open, IMPORT)
    }

    @Deprecated("Platform result API; the app has no AndroidX dependency.")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data
        if (resultCode != RESULT_OK || uri == null) return
        val layout = layout() ?: return
        when (requestCode) {
            EXPORT -> writeExport(uri, layout)
            IMPORT -> stageImport(uri, layout)
            BACKUP -> writeBackup(uri)
        }
    }

    private fun writeExport(uri: Uri, layout: SaveLayout) {
        val states = includeStates
        background(getString(R.string.saves_exporting)) {
            val set = SaveArchive.collect(layout, states)
            if (set.files.isEmpty()) throw SaveArchiveException(SaveProblem.NOTHING_TO_EXPORT)
            openOutput(uri).use { SaveArchive.write(set, it) }
            ui {
                status.text = if (set.skipped.isEmpty()) {
                    getString(R.string.saves_exported, set.files.size)
                } else {
                    getString(R.string.saves_exported_skipped, set.files.size, set.skipped.size)
                }
            }
        }
    }

    private fun openOutput(uri: Uri) = try {
        // Truncate an existing document; some providers only know plain "w".
        contentResolver.openOutputStream(uri, "wt")
    } catch (_: Exception) {
        contentResolver.openOutputStream(uri, "w")
    } ?: throw java.io.IOException("No output stream")

    /** Forge writes and verifies the backup in the cache, then it is copied to the document. */
    private fun writeBackup(uri: Uri) {
        background(getString(R.string.saves_backup_running)) {
            val zip = File(cacheDir, "triaevum-backup.zip")
            try {
                val request = ForgeBackups.request(this, "backup")
                    .put("destination", zip.absolutePath)
                    .put("touch_preferences", ForgeBackups.touchPreferences(this))
                ForgeBackups.run(request)
                openOutput(uri).use { output -> zip.inputStream().use { it.copyTo(output, 1 shl 20) } }
                val size = android.text.format.Formatter.formatShortFileSize(this, zip.length())
                ui { status.text = getString(R.string.saves_backup_done, size) }
            } finally {
                zip.delete()
            }
        }
    }

    private fun stageImport(uri: Uri, layout: SaveLayout) {
        background(getString(R.string.saves_reading)) {
            // The archive is copied once: Forge and the save check read a local file.
            val copy = File(cacheDir, "triaevum-restore.zip")
            val input = contentResolver.openInputStream(uri) ?: throw java.io.IOException("No input stream")
            input.use { source -> copy.outputStream().use { source.copyTo(it, 1 shl 20) } }
            if (ForgeBackups.isForgeBackup(copy)) {
                val preview = try {
                    ForgeBackups.run(ForgeBackups.request(this, "restore_preview").put("backup", copy.absolutePath))
                } catch (error: Exception) {
                    copy.delete()
                    throw error
                }
                ui {
                    if (closing) {
                        copy.delete()
                    } else {
                        pendingBackup = copy
                        confirmBackup(copy, preview)
                    }
                }
                return@background
            }
            val plan = try {
                copy.inputStream().use { SaveArchive.stage(it, layout) }
            } finally {
                copy.delete()
            }
            val replaced = SaveArchive.replacements(plan, layout)
            ui {
                if (closing) {
                    SaveArchive.discard(plan)
                } else {
                    pending = plan
                    confirm(plan, replaced, layout)
                }
            }
        }
    }

    private fun confirm(plan: ImportPlan, replaced: List<StagedFile>, layout: SaveLayout) {
        status.text = ""
        val names = replaced.take(MAX_LISTED).joinToString(", ") { it.relative } +
            if (replaced.size > MAX_LISTED) ", …" else ""
        val replacing = if (replaced.isEmpty()) {
            getString(R.string.saves_confirm_new)
        } else {
            getString(R.string.saves_confirm_replaces, replaced.size, names, SaveArchive.KEPT_BACKUPS)
        }
        AlertDialog.Builder(this)
            .setTitle(R.string.saves_confirm_title)
            .setMessage(getString(R.string.saves_confirm_message, plan.saveFiles, plan.saveStates, replacing))
            .setPositiveButton(R.string.saves_confirm_import) { _, _ -> commit(plan, layout) }
            .setNegativeButton(R.string.saves_confirm_cancel) { _, _ -> dropPending(plan) }
            .setOnCancelListener { dropPending(plan) }
            .show()
    }

    private fun unitLabel(name: String): String = when (name) {
        "save_data" -> getString(R.string.saves_unit_saves)
        "save_states" -> getString(R.string.saves_unit_states)
        "config" -> getString(R.string.saves_unit_settings)
        "imgui_layout" -> getString(R.string.saves_unit_window)
        "touch_preferences" -> getString(R.string.saves_unit_touch)
        else -> name
    }

    /** What restoring the verified backup changes, from Forge's preview. */
    private fun confirmBackup(copy: File, preview: JSONObject) {
        status.text = ""
        val lines = ArrayList<String>()
        val warnings = preview.optJSONArray("warnings") ?: org.json.JSONArray()
        for (index in 0 until warnings.length()) {
            when (warnings.optJSONObject(index)?.optString("kind")) {
                "other_platform" -> lines += getString(R.string.saves_restore_other_platform)
                "different_revision" -> lines += getString(R.string.saves_restore_revision)
            }
        }
        val groups = preview.optJSONArray("groups") ?: org.json.JSONArray()
        for (index in 0 until groups.length()) {
            val group = groups.optJSONObject(index) ?: continue
            lines += getString(
                R.string.saves_restore_group,
                unitLabel(group.optString("name")),
                group.optInt("replaced"),
                group.optInt("added"),
                group.optInt("removed"),
            )
        }
        if (preview.optBoolean("touch_preferences")) lines += getString(R.string.saves_unit_touch)
        lines += getString(R.string.saves_restore_snapshot)
        AlertDialog.Builder(this)
            .setTitle(R.string.saves_restore_title)
            .setMessage(lines.joinToString("\n\n"))
            .setPositiveButton(R.string.saves_restore_confirm) { _, _ -> restoreBackup(copy) }
            .setNegativeButton(R.string.saves_confirm_cancel) { _, _ -> dropBackup(copy) }
            .setOnCancelListener { dropBackup(copy) }
            .show()
    }

    private fun dropBackup(copy: File) {
        if (pendingBackup !== copy) return
        pendingBackup = null
        copy.delete()
        status.text = getString(R.string.saves_import_cancelled)
    }

    private fun restoreBackup(copy: File) {
        if (pendingBackup !== copy) return
        pendingBackup = null
        if (gameRunning()) {
            copy.delete()
            status.text = getString(R.string.saves_game_running)
            return
        }
        val layout = layout()
        background(getString(R.string.saves_restoring)) {
            val result = try {
                ForgeBackups.run(ForgeBackups.request(this, "restore").put("backup", copy.absolutePath))
            } finally {
                copy.delete()
            }
            result.optJSONObject("touch_preferences")?.let { ForgeBackups.applyTouchPreferences(this, it) }
            val snapshot = File(result.optString("snapshot")).parentFile?.name.orEmpty()
            val saves = layout?.let { SaveArchive.collect(it, includeStates = true) }
            ui {
                if (saves != null) summary.text = getString(R.string.saves_summary, saves.saveFiles, saves.saveStates)
                status.text = getString(R.string.saves_restored, "data/backups/$snapshot")
            }
        }
    }

    private fun dropPending(plan: ImportPlan) {
        if (pending !== plan) return
        pending = null
        SaveArchive.discard(plan)
        status.text = getString(R.string.saves_import_cancelled)
    }

    private fun commit(plan: ImportPlan, layout: SaveLayout) {
        if (pending !== plan) return
        pending = null
        if (gameRunning()) {
            SaveArchive.discard(plan)
            status.text = getString(R.string.saves_game_running)
            return
        }
        background(getString(R.string.saves_importing)) {
            val result = try {
                SaveArchive.commit(plan, layout)
            } finally {
                SaveArchive.discard(plan)
            }
            val saves = SaveArchive.collect(layout, includeStates = true)
            ui {
                summary.text = getString(R.string.saves_summary, saves.saveFiles, saves.saveStates)
                status.text = result.backup?.let { backup ->
                    getString(R.string.saves_imported_backup, result.installed, "save-backups/${backup.name}")
                } ?: getString(R.string.saves_imported, result.installed)
            }
        }
    }

    /** True while the game's process (the app's main process) is running. */
    private fun gameRunning(): Boolean =
        getSystemService(ActivityManager::class.java).runningAppProcesses.orEmpty().any { it.processName == packageName }

    /** Runs [work] on one worker thread with the controls disabled; failures become a message. */
    private fun background(message: String?, work: () -> Unit) {
        if (worker?.isAlive == true) return
        setBusy(true)
        if (message != null) status.text = message
        worker = Thread({
            try {
                work()
            } catch (error: SaveArchiveException) {
                ui { status.text = problem(error) }
            } catch (error: ForgeJobException) {
                ui { status.text = listOf(error.title, error.detail).filter { it.isNotEmpty() }.joinToString("\n\n") }
            } catch (error: Exception) {
                Log.w(TAG, "Save operation failed", error)
                ui { status.text = getString(R.string.saves_failed, error.message ?: error.javaClass.simpleName) }
            } finally {
                ui { setBusy(false) }
            }
        }, "saves").also { it.start() }
    }

    private fun ui(action: () -> Unit) = runOnUiThread { if (!closing) action() }

    private fun setBusy(busy: Boolean) {
        progress.visibility = if (busy) ProgressBar.VISIBLE else ProgressBar.GONE
        buttons.forEach { it.isEnabled = !busy }
    }

    private fun problem(error: SaveArchiveException): String {
        val subject = error.subject
        return when (error.problem) {
            SaveProblem.NOT_AN_ARCHIVE -> getString(R.string.saves_problem_not_archive)
            SaveProblem.UNSAFE_PATH -> getString(R.string.saves_problem_unsafe_path, subject)
            SaveProblem.UNSUPPORTED_FILE -> getString(R.string.saves_problem_unsupported, subject)
            SaveProblem.DUPLICATE -> getString(R.string.saves_problem_duplicate, subject)
            SaveProblem.TOO_LARGE -> getString(R.string.saves_problem_too_large, subject)
            SaveProblem.TOO_MANY -> getString(R.string.saves_problem_too_many)
            SaveProblem.NOT_A_SAVE_STATE -> getString(R.string.saves_problem_not_state, subject)
            SaveProblem.DAMAGED -> getString(R.string.saves_problem_damaged, subject)
            SaveProblem.EMPTY -> getString(R.string.saves_problem_empty)
            SaveProblem.NOTHING_TO_EXPORT -> getString(R.string.saves_problem_nothing)
            SaveProblem.TARGET_NOT_FILE -> getString(R.string.saves_problem_target, subject)
        }
    }

    private fun dp(value: Int): Int =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value.toFloat(), resources.displayMetrics).toInt()

    private fun buildLayout(): ScrollView {
        summary = TextView(this).apply { setPadding(0, dp(8), 0, dp(8)) }
        status = TextView(this).apply { setPadding(0, dp(8), 0, dp(4)) }
        progress = ProgressBar(this).apply { isIndeterminate = true; visibility = ProgressBar.GONE }
        val action = { label: Int, onClick: () -> Unit ->
            Button(this).apply {
                text = getString(label)
                minHeight = dp(48)
                setOnClickListener { onClick() }
            }
        }
        buttons = listOf(
            action(R.string.saves_backup_all) { pickBackupDestination() },
            action(R.string.saves_import) { pickImport() },
            action(R.string.saves_export) { export(false) },
        )
        val body = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(24), dp(24), dp(24), dp(24))
            addView(TextView(context).apply {
                text = getString(R.string.saves_title)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 26f)
            })
            addView(TextView(context).apply {
                text = getString(R.string.saves_intro)
                setPadding(0, dp(8), 0, dp(8))
            })
            addView(summary)
            buttons.forEach { addView(it, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)) }
            addView(status)
            addView(progress)
        }
        return ScrollView(this).apply {
            addView(body)
            fitsSystemWindows = true
        }
    }

    private companion object {
        const val TAG = "TriAevumSaves"
        const val EXPORT = 1
        const val IMPORT = 2
        const val BACKUP = 3
        const val MAX_LISTED = 6
    }
}
