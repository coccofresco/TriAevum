package org.triaevum.android.diagnostics

import android.content.ContentProvider
import android.content.ContentValues
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.OpenableColumns
import java.io.File
import java.io.FileNotFoundException

/**
 * Serves exactly one file, the support bundle [DiagnosticsBundle] wrote, read-only, to the app the
 * user picks in the share sheet (a temporary grant on the ACTION_SEND intent). Not exported:
 * nothing else on the device can reach it, and no other file is ever served.
 */
class DiagnosticsBundleProvider : ContentProvider() {
    override fun onCreate() = true

    private fun bundle(uri: Uri): File {
        val context = context ?: throw FileNotFoundException(uri.toString())
        if (uri.pathSegments != listOf(DiagnosticsBundle.FILE_NAME)) throw FileNotFoundException(uri.toString())
        return File(DiagnosticsBundle.directory(context), DiagnosticsBundle.FILE_NAME).takeIf { it.isFile }
            ?: throw FileNotFoundException(uri.toString())
    }

    override fun openFile(uri: Uri, mode: String): ParcelFileDescriptor {
        if (mode != "r") throw SecurityException("The diagnostics bundle is read-only")
        return ParcelFileDescriptor.open(bundle(uri), ParcelFileDescriptor.MODE_READ_ONLY)
    }

    override fun getType(uri: Uri) = "application/zip"

    override fun query(uri: Uri, projection: Array<String>?, selection: String?, arguments: Array<String>?, order: String?): Cursor {
        val file = bundle(uri)
        val columns = projection ?: arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE)
        return MatrixCursor(columns, 1).apply {
            addRow(
                columns.map {
                    when (it) {
                        OpenableColumns.DISPLAY_NAME -> file.name
                        OpenableColumns.SIZE -> file.length()
                        else -> null
                    }
                },
            )
        }
    }

    override fun insert(uri: Uri, values: ContentValues?): Uri? = throw UnsupportedOperationException()

    override fun delete(uri: Uri, selection: String?, arguments: Array<String>?): Int = throw UnsupportedOperationException()

    override fun update(uri: Uri, values: ContentValues?, selection: String?, arguments: Array<String>?): Int =
        throw UnsupportedOperationException()

    companion object {
        fun uri(packageName: String): Uri = Uri.parse("content://$packageName.diagnostics/${DiagnosticsBundle.FILE_NAME}")
    }
}
