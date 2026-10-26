package org.triaevum.android.diagnostics

import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.graphics.Typeface
import android.os.Bundle
import android.util.TypedValue
import android.view.Gravity
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import org.triaevum.android.R

/**
 * Shows the diagnostics report ([StartupDiagnostics.buildReport] followed by Forge's report,
 * [DiagnosticsBundle]) and lets the user copy or share it, or share the support bundle zip with an
 * app of their choice. Both are built when this screen opens and never sent anywhere by TriAevum.
 */
class DiagnosticsActivity : Activity() {
    private lateinit var report: TextView
    private lateinit var copy: Button
    private lateinit var share: Button
    private lateinit var shareBundle: Button
    @Volatile private var closing = false
    private var text: String? = null

    /** The installer's language before Android 13 (owner decision 8). */
    override fun attachBaseContext(base: android.content.Context) =
        super.attachBaseContext(org.triaevum.android.forge.ForgeLocale.wrap(base))

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        setContentView(buildLayout())
        val context = applicationContext
        Thread({
            val startup = try {
                StartupDiagnostics.buildReport(context)
            } catch (error: Exception) {
                getString(R.string.diagnostics_failed, error.javaClass.simpleName)
            }
            val forge = try {
                DiagnosticsBundle.build(context)
            } catch (error: Exception) {
                DiagnosticsBundle.Result("Forge diagnostics failed (${error.javaClass.simpleName}).", null, emptyList())
            }
            val contents = if (forge.members.isEmpty()) "" else
                "\n\n" + getString(R.string.diagnostics_bundle_contents) + "\n" + forge.members.joinToString("\n") { "  $it" }
            val built = startup + "\n\n" + forge.text + contents
            runOnUiThread {
                if (closing) return@runOnUiThread
                text = built
                report.text = built
                copy.isEnabled = true
                share.isEnabled = true
                shareBundle.isEnabled = forge.bundle != null
            }
        }, "forge-report").start()
    }

    override fun onDestroy() {
        closing = true
        super.onDestroy()
    }

    private fun copyReport() {
        val value = text ?: return
        getSystemService(ClipboardManager::class.java)
            .setPrimaryClip(ClipData.newPlainText(getString(R.string.diagnostics_title), value))
        Toast.makeText(this, R.string.diagnostics_copied, Toast.LENGTH_SHORT).show()
    }

    private fun shareReport() {
        val value = text ?: return
        val send = Intent(Intent.ACTION_SEND)
            .setType("text/plain")
            .putExtra(Intent.EXTRA_SUBJECT, getString(R.string.diagnostics_title))
            .putExtra(Intent.EXTRA_TEXT, value)
        startActivity(Intent.createChooser(send, getString(R.string.diagnostics_share_title)))
    }

    private fun shareBundle() {
        val uri = DiagnosticsBundleProvider.uri(packageName)
        val send = Intent(Intent.ACTION_SEND)
            .setType("application/zip")
            .putExtra(Intent.EXTRA_SUBJECT, getString(R.string.diagnostics_title))
            .putExtra(Intent.EXTRA_STREAM, uri)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        send.clipData = ClipData.newRawUri(DiagnosticsBundle.FILE_NAME, uri)
        startActivity(Intent.createChooser(send, getString(R.string.diagnostics_share_bundle_title)))
    }

    private fun dp(value: Int): Int =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value.toFloat(), resources.displayMetrics).toInt()

    private fun buildLayout(): LinearLayout {
        report = TextView(this).apply {
            text = getString(R.string.diagnostics_building)
            typeface = Typeface.MONOSPACE
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            setTextIsSelectable(true)
            setPadding(dp(16), dp(8), dp(16), dp(16))
        }
        val body = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(TextView(context).apply {
                text = getString(R.string.diagnostics_title)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 24f)
                setPadding(dp(16), dp(24), dp(16), dp(8))
            })
            addView(TextView(context).apply {
                text = getString(R.string.diagnostics_intro)
                setPadding(dp(16), 0, dp(16), dp(8))
            })
            // Long log lines scroll sideways instead of wrapping into an unreadable column.
            addView(HorizontalScrollView(context).apply { addView(report) })
        }
        copy = Button(this).apply {
            text = getString(R.string.diagnostics_copy)
            minHeight = dp(48)
            isEnabled = false
            setOnClickListener { copyReport() }
        }
        share = Button(this).apply {
            text = getString(R.string.diagnostics_share)
            minHeight = dp(48)
            isEnabled = false
            setOnClickListener { shareReport() }
        }
        shareBundle = Button(this).apply {
            text = getString(R.string.diagnostics_share_bundle)
            minHeight = dp(48)
            isEnabled = false
            setOnClickListener { shareBundle() }
        }
        val close = Button(this).apply {
            text = getString(R.string.diagnostics_close)
            minHeight = dp(48)
            setOnClickListener { finish() }
        }
        val actions = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.END
            setPadding(dp(16), dp(8), dp(16), dp(16))
            addView(close)
            addView(copy)
            addView(share)
            addView(shareBundle)
        }
        return LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(ScrollView(context).apply { addView(body) }, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
            addView(actions, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
            fitsSystemWindows = true
        }
    }
}
