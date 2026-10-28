package org.triaevum.android.diagnostics

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class StartupReportTest {
    @get:Rule val temp = TemporaryFolder()

    private val device = DeviceSummary(
        app = "0.1 (1)",
        device = "Maker Phone (phone)",
        android = "15 (API 35)",
        abis = "arm64-v8a",
        soc = "Vendor SoC",
        memory = "8000 MiB total, 4000 MiB available",
        gpu = null,
        vulkan = listOf("version 1.3.0"),
    )

    @Test fun stagesAreRecordedPerSessionAndRotated() {
        var now = 1_000L
        val log = StageLog(File(temp.root, "diagnostics/game-stages.txt")) { now }
        log.begin(41)
        now = 1_500L
        log.record("activity_created")
        now = 2_250L
        log.record("surface_ready 1560x720\nignored")
        log.begin(42)
        log.record("activity_created")

        val sessions = log.sessions()
        assertEquals(listOf(42, 41), sessions.map { it.pid })
        val first = sessions[1]
        assertEquals(1_000L, first.startedAt)
        assertEquals(listOf("activity_created", "surface_ready 1560x720 ignored"), first.stages.map { it.name })
        assertEquals(2_250L, first.stages.last().time)
    }

    @Test fun stagesAreBoundedAndCleaned() {
        val log = StageLog(File(temp.root, "stages.txt")) { 5L }
        log.begin(1)
        repeat(StageLog.MAX_STAGES + 10) { log.record("stage $it") }
        assertEquals(StageLog.MAX_STAGES, log.sessions().first().stages.size)
        assertEquals(StageLog.MAX_STAGE_LENGTH, StageLog.clean("x".repeat(500)).length)
        assertEquals("a b", StageLog.clean(" a\u0000b "))
    }

    @Test fun unreadableStageFilesAreIgnored() {
        val file = File(temp.root, "other.txt").apply { writeText("not a stage file\n") }
        assertNull(StageLog.read(file))
        assertNull(StageLog.read(File(temp.root, "missing.txt")))
    }

    @Test fun reportMatchesTheExitWithItsSession() {
        val session = StageSession(
            pid = 7,
            startedAt = 10_000,
            stages = listOf(Stage(10_100, "libraries_loaded"), Stage(12_000, "surface_ready 1560x720")),
        )
        val exit = ExitRecord(
            process = "org.triaevum.android", pid = 7, timestamp = 15_000, reason = ExitReasons.CRASH_NATIVE,
            status = 11, description = "crash", pssKb = 2048, rssKb = 4096,
        )
        val report = StartupReport.format(
            device = device,
            exits = listOf(exit),
            sessions = listOf(session),
            javaFailure = null,
            installState = "succeeded at 1970-01-01T00:00:00Z",
            logTail = listOf("line one", "tab\tand\u0001control"),
            generatedAt = 20_000,
        )
        assertTrue(report.startsWith("TriAevum Android diagnostics report\n"))
        assertTrue(report.contains("Device: Maker Phone (phone)\n"))
        assertTrue(report.contains("GPU: unavailable\n"))
        assertTrue(report.contains("Vulkan: version 1.3.0\n"))
        assertTrue(report.contains("org.triaevum.android pid 7: native crash, status 11, pss 2 MiB, rss 4 MiB (crash)"))
        assertTrue(report.contains("Session pid 7 started 1970-01-01T00:00:10Z, ended: native crash\n"))
        assertTrue(report.contains("  +2.000 s  surface_ready 1560x720\n"))
        assertTrue(report.contains("  Last stage: surface_ready 1560x720\n"))
        assertTrue(report.contains("tab\tand control\n"))
        assertFalse(report.contains("== Java failure =="))
        assertEquals("native crash, last stage: surface_ready 1560x720", StartupReport.summary(exit, listOf(session)))
    }

    @Test fun emptyReportSaysWhatIsMissing() {
        val report = StartupReport.format(device, emptyList(), emptyList(), null, null, emptyList(), 0)
        assertTrue(report.contains("No exit records"))
        assertTrue(report.contains("No game session recorded."))
        assertTrue(report.contains("== Log tail ==\nUnavailable.\n"))
        assertFalse(report.contains("== Last native crash =="))
        val crash = StartupReport.format(device, emptyList(), emptyList(), null, null, emptyList(), 0,
            nativeCrash = "Native crash of org.triaevum.android pid 7\n  #00 libTriAevum.so +0x10\u0001\n")
        assertTrue(crash, crash.contains("== Last native crash ==\nNative crash of org.triaevum.android pid 7\n" +
            "  #00 libTriAevum.so +0x10 \n"))
    }

    @Test fun reportIsBounded() {
        val lines = List(5_000) { "x".repeat(1_000) }
        val report = StartupReport.format(device, emptyList(), emptyList(), null, null, lines, 0)
        assertTrue(report.length <= StartupReport.MAX_REPORT_CHARS + 20)
    }

    @Test fun abnormalReasonsAndVulkanVersions() {
        assertTrue(ExitReasons.abnormal(ExitReasons.CRASH_NATIVE))
        assertTrue(ExitReasons.abnormal(ExitReasons.ANR))
        assertTrue(ExitReasons.abnormal(ExitReasons.LOW_MEMORY))
        assertFalse(ExitReasons.abnormal(ExitReasons.EXIT_SELF))
        assertFalse(ExitReasons.abnormal(ExitReasons.USER_REQUESTED))
        assertEquals("1.1.0", StartupReport.vulkanVersion(4198400))
        assertEquals("1.3.0", StartupReport.vulkanVersion((1 shl 22) or (3 shl 12)))
    }

    @Test fun javaFailuresKeepStructureWithoutMessages() {
        val error = IllegalStateException("secret /storage/path", RuntimeException("inner secret"))
        val text = StartupReport.throwable(error)
        assertTrue(text.startsWith("java.lang.IllegalStateException\n"))
        assertTrue(text.contains("Caused by: java.lang.RuntimeException\n"))
        assertTrue(text.contains("  at org.triaevum.android.diagnostics.StartupReportTest."))
        assertFalse(text.contains("secret"))
    }
}
