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
 * Save export and import through the system document picker ([SaveArchive]); no storage
 * permission. It runs in the installer's process, so it can tell whether the game process is
 * running: an import then waits, because the game would write over the imported files.
 * Nothing changes until the user confirms what an import replaces, and replaced files go to a
 * backup folder first.
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
            val saves = SaveArchive.collect(layout, includeStates = true)
            ui { summary.text = getString(R.string.saves_summary, saves.saveFiles, saves.saveStates) }
        }
    }

    override fun onDestroy() {
        closing = true
        worker?.join(5_000)
        pending?.let { SaveArchive.discard(it) }
        pending = null
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
        }
    }

    private fun writeExport(uri: Uri, layout: SaveLayout) {
        val states = includeStates
        background(getString(R.string.saves_exporting)) {
            val set = SaveArchive.collect(layout, states)
            if (set.files.isEmpty()) throw SaveArchiveException(SaveProblem.NOTHING_TO_EXPORT)
            // Truncate an existing document; some providers only know plain "w".
            val output = try {
                contentResolver.openOutputStream(uri, "wt")
            } catch (_: Exception) {
                contentResolver.openOutputStream(uri, "w")
            } ?: throw java.io.IOException("No output stream")
            output.use { SaveArchive.write(set, it) }
            ui {
                status.text = if (set.skipped.isEmpty()) {
                    getString(R.string.saves_exported, set.files.size)
                } else {
                    getString(R.string.saves_exported_skipped, set.files.size, set.skipped.size)
                }
            }
        }
    }

    private fun stageImport(uri: Uri, layout: SaveLayout) {
        background(getString(R.string.saves_reading)) {
            val input = contentResolver.openInputStream(uri) ?: throw java.io.IOException("No input stream")
            val plan = input.use { SaveArchive.stage(it, layout) }
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
            action(R.string.saves_export) { export(false) },
            action(R.string.saves_export_states) { export(true) },
            action(R.string.saves_import) { pickImport() },
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
        const val MAX_LISTED = 6
    }
}
