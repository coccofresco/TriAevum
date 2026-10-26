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
 * Shows the diagnostics report ([StartupDiagnostics.buildReport]) and lets the user copy it or
 * share it with an app of their choice. The report is built when this screen opens and is never
 * sent anywhere by TriAevum.
 */
class DiagnosticsActivity : Activity() {
    private lateinit var report: TextView
    private lateinit var copy: Button
    private lateinit var share: Button
    @Volatile private var closing = false
    private var text: String? = null

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        setContentView(buildLayout())
        val context = applicationContext
        Thread({
            val built = try {
                StartupDiagnostics.buildReport(context)
            } catch (error: Exception) {
                getString(R.string.diagnostics_failed, error.javaClass.simpleName)
            }
            runOnUiThread {
                if (closing) return@runOnUiThread
                text = built
                report.text = built
                copy.isEnabled = true
                share.isEnabled = true
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
        }
        return LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(ScrollView(context).apply { addView(body) }, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
            addView(actions, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
            fitsSystemWindows = true
        }
    }
}
