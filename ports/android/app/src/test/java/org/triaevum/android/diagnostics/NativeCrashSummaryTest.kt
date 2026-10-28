package org.triaevum.android.diagnostics

import java.io.ByteArrayOutputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeCrashSummaryTest {
    /** A minimal protobuf writer for tombstone fixtures. */
    private class Proto {
        private val out = ByteArrayOutputStream()
        private fun varint(value: Long) {
            var v = value
            while (true) {
                if (v and 0x7fL.inv() == 0L) { out.write(v.toInt()); return }
                out.write(((v and 0x7f) or 0x80).toInt())
                v = v ushr 7
            }
        }
        fun int(field: Int, value: Long) = apply { varint((field shl 3).toLong()); varint(value) }
        fun bytes(field: Int, value: ByteArray) = apply {
            varint(((field shl 3) or 2).toLong()); varint(value.size.toLong()); out.write(value)
        }
        fun string(field: Int, value: String) = bytes(field, value.toByteArray())
        fun message(field: Int, value: Proto) = bytes(field, value.toBytes())
        fun fixed64(field: Int) = apply { varint(((field shl 3) or 1).toLong()); repeat(8) { out.write(0) } }
        fun toBytes(): ByteArray = out.toByteArray()
    }

    private fun frame(pc: Long, file: String, function: String = "", offset: Long = 0, buildId: String = "") =
        Proto().int(1, pc).int(2, 0x7f00000000L + pc).string(4, function).int(5, offset).string(6, file)
            .string(8, buildId)

    private val appPath = "/data/app/~~abc==/org.triaevum.android-xyz==/lib/arm64/libTriAevum.so"

    private fun tombstone(threads: List<Pair<Long, Proto>>, tid: Long, fault: Long? = 0L): ByteArray {
        val signal = Proto().int(1, 11).string(2, "SIGSEGV").int(3, 1).string(4, "SEGV_MAPERR")
        if (fault != null) signal.int(8, 1).int(9, fault)
        val root = Proto().int(1, 3).string(2, "samsung/device/fingerprint:16/user/keys")
            .int(5, 1234).int(6, tid).int(7, 10680).string(8, "u:r:untrusted_app:s0")
            .string(9, "org.triaevum.android").message(10, signal)
            .string(14, "abort message with /storage/emulated/0/Download/private.3ds")
        for ((id, thread) in threads) root.message(16, Proto().int(1, id).message(2, thread))
        root.message(17, Proto().string(7, "/storage/emulated/0/Download/private.3ds"))
        return root.toBytes()
    }

    @Test fun keepsTheCrashingThreadsFramesAndRedactsPaths() {
        val other = Proto().int(1, 1235).string(2, "worker").message(4, frame(0x10, appPath))
        val crashing = Proto().int(1, 1300).string(2, "SDLThread")
            .message(3, Proto().string(1, "x0").int(2, 0x1234)) // registers: never read
            .message(4, frame(0x4a2f10, appPath, "_ZN4Fast21GfxRenderingAPIVulkan10StartFrameEv", 0x44, "0123abcd"))
            .message(4, frame(0x1000, "/vendor/lib64/hw/vulkan.adreno.so", "qglinternal", 8, "ffee"))
            .message(4, frame(0x2000, "/data/app/~~abc==/org.triaevum.android-xyz==/base.apk!/lib/arm64-v8a/libSDL2.so"))
            .message(4, frame(0x3000, "/storage/emulated/0/Download/evil.so", "secret", 1, "beef"))
            .message(4, frame(0x4000, "/apex/com.android.runtime/lib64/bionic/libc.so", "abort"))
            .fixed64(6)
        val summary = NativeCrashSummary.parse(
            tombstone(listOf(1235L to other, 1300L to crashing), tid = 1300),
            setOf("libTriAevum.so", "libSDL2.so"),
        )!!
        assertEquals(11, summary.signal)
        assertEquals("SIGSEGV", summary.signalName)
        assertEquals("SEGV_MAPERR", summary.codeName)
        assertEquals(true, summary.faultInNullPage)
        assertEquals("SDLThread", summary.threadName)
        assertEquals(
            listOf("libTriAevum.so", "vulkan.adreno.so", "libSDL2.so", "<other>", "libc.so"),
            summary.frames.map { it.module },
        )
        assertEquals(0x4a2f10L, summary.frames[0].relativePc)
        assertEquals("_ZN4Fast21GfxRenderingAPIVulkan10StartFrameEv+68", summary.frames[0].symbol)
        assertEquals("0123abcd", summary.frames[0].buildId)
        assertNull(summary.frames[3].symbol)
        assertNull(summary.frames[3].buildId)
        val text = summary.format("Native crash")
        assertTrue(text, text.contains("#00 libTriAevum.so +0x4a2f10"))
        assertTrue(text, text.contains("fault address in the null page"))
        for (secret in listOf("/storage", "/data/app", "Download", "fingerprint", "abort message", "secret", "untrusted_app")) {
            assertFalse("$secret leaked: $text", text.contains(secret))
        }
    }

    @Test fun boundsFramesAndHandlesMissingFaultAddress() {
        val thread = Proto().string(2, "main")
        repeat(20) { thread.message(4, frame(it.toLong(), appPath)) }
        val summary = NativeCrashSummary.parse(tombstone(listOf(7L to thread), tid = 7, fault = null), setOf("libTriAevum.so"))!!
        assertEquals(NativeCrashSummary.MAX_FRAMES, summary.frames.size)
        assertNull(summary.faultInNullPage)
        val far = NativeCrashSummary.parse(tombstone(listOf(7L to thread), tid = 7, fault = 0x7fff0000L), setOf())!!
        assertEquals(false, far.faultInNullPage)
        assertEquals("<other>", far.frames[0].module)
    }

    @Test fun rejectsBytesThatAreNotATombstone() {
        assertNull(NativeCrashSummary.parse(ByteArray(0), emptySet()))
        assertNull(NativeCrashSummary.parse(byteArrayOf(0x0a, 0x7f, 0x01), emptySet()))
        assertNull(NativeCrashSummary.parse("not a protobuf at all".toByteArray(), emptySet()))
    }

    @Test fun moduleNamesNeverCarryPaths() {
        val app = setOf("libTriAevum.so")
        assertEquals("libTriAevum.so", NativeCrashSummary.module(appPath, app))
        assertEquals("libc.so", NativeCrashSummary.module("/apex/com.android.runtime/lib64/bionic/libc.so", app))
        assertEquals("<other>", NativeCrashSummary.module("/data/data/org.example/files/libTriAevumX.so", app))
        assertEquals("<other>", NativeCrashSummary.module("[anon:dalvik-main space]", app))
        assertEquals("<other>", NativeCrashSummary.module("", app))
    }
}
