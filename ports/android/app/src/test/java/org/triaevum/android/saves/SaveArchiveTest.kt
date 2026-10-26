package org.triaevum.android.saves

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class SaveArchiveTest {
    @get:Rule val temp = TemporaryFolder()

    private val state = SaveArchive.SAVESTATE_MAGIC + ByteArray(64) { it.toByte() }

    private fun layout(name: String): SaveLayout = SaveLayout.standard(temp.newFolder(name))

    private fun write(file: File, bytes: ByteArray) {
        file.parentFile?.mkdirs()
        file.writeBytes(bytes)
    }

    private fun zip(vararg entries: Pair<String, ByteArray>): ByteArray {
        val out = ByteArrayOutputStream()
        ZipOutputStream(out).use { zip ->
            for ((name, bytes) in entries) {
                zip.putNextEntry(ZipEntry(name))
                zip.write(bytes)
                zip.closeEntry()
            }
        }
        return out.toByteArray()
    }

    private fun export(layout: SaveLayout, states: Boolean): ByteArray {
        val out = ByteArrayOutputStream()
        SaveArchive.write(SaveArchive.collect(layout, states), out)
        return out.toByteArray()
    }

    private fun expect(problem: SaveProblem, block: () -> Unit) {
        try {
            block()
            fail("expected $problem")
        } catch (error: SaveArchiveException) {
            assertEquals(problem, error.problem)
        }
    }

    @Test fun exportThenImportRestoresSavesAndStates() {
        val source = layout("source")
        write(File(source.saveData, "save00.bin"), ByteArray(5340) { 1 })
        write(File(source.saveData, "system.dat"), ByteArray(34) { 2 })
        write(File(source.saveData, "crashes/dump.dmp"), ByteArray(10))
        write(File(source.saveStates, "quick.oot3dsav"), state)
        write(File(source.saveStates, "notes.txt"), ByteArray(3))
        val archive = export(source, states = true)

        val names = ZipInputStream(ByteArrayInputStream(archive)).use { zip ->
            generateSequence { zip.nextEntry?.name }.toList()
        }
        assertEquals(
            listOf("savedata/save00.bin", "savedata/system.dat", "savestates/quick.oot3dsav", SaveArchive.MANIFEST),
            names,
        )

        val target = layout("target")
        write(File(target.saveData, "save01.bin"), ByteArray(5340) { 9 })
        val plan = SaveArchive.stage(ByteArrayInputStream(archive), target)
        assertTrue(plan.verifiedByManifest)
        assertEquals(2, plan.saveFiles)
        assertEquals(1, plan.saveStates)
        assertTrue(SaveArchive.replacements(plan, target).isEmpty())
        val result = SaveArchive.commit(plan, target)
        assertEquals(3, result.installed)
        assertEquals(0, result.replaced)
        assertNull(result.backup)
        assertArrayEquals(ByteArray(5340) { 1 }, File(target.saveData, "save00.bin").readBytes())
        assertArrayEquals(state, File(target.saveStates, "quick.oot3dsav").readBytes())
        // Other slots stay.
        assertArrayEquals(ByteArray(5340) { 9 }, File(target.saveData, "save01.bin").readBytes())
        assertFalse(plan.staging.exists())
    }

    @Test fun exportWithoutStatesLeavesThemOut() {
        val source = layout("source")
        write(File(source.saveData, "save00.bin"), ByteArray(16))
        write(File(source.saveStates, "quick.oot3dsav"), state)
        val set = SaveArchive.collect(source, includeStates = false)
        assertEquals(1, set.saveFiles)
        assertEquals(0, set.saveStates)
    }

    @Test fun nothingToExport() {
        val source = layout("source")
        expect(SaveProblem.NOTHING_TO_EXPORT) { export(source, states = true) }
    }

    @Test fun replacedFilesMoveToABackup() {
        val target = layout("target")
        write(File(target.saveData, "save00.bin"), ByteArray(8) { 7 })
        val plan = SaveArchive.stage(ByteArrayInputStream(zip("save00.bin" to ByteArray(8) { 3 })), target)
        assertFalse(plan.verifiedByManifest)
        assertEquals(listOf("save00.bin"), SaveArchive.replacements(plan, target).map { it.relative })
        val result = SaveArchive.commit(plan, target, now = 0L)
        assertEquals(1, result.replaced)
        val backup = assertNotNullFile(result.backup)
        assertArrayEquals(ByteArray(8) { 7 }, File(backup, "save00.bin").readBytes())
        assertArrayEquals(ByteArray(8) { 3 }, File(target.saveData, "save00.bin").readBytes())
    }

    private fun assertNotNullFile(file: File?): File {
        assertNotNull(file)
        return file!!
    }

    @Test fun emulatorFolderArchiveIsAccepted() {
        val target = layout("target")
        val plan = SaveArchive.stage(
            ByteArrayInputStream(zip("00000001/" to ByteArray(0), "00000001/save02.bin" to ByteArray(4), "00000001/system.dat" to ByteArray(2))),
            target,
        )
        assertEquals(listOf("save02.bin", "system.dat"), plan.files.map { it.relative })
        SaveArchive.discard(plan)
    }

    @Test fun tamperedManifestIsRefused() {
        val source = layout("source")
        write(File(source.saveData, "save00.bin"), ByteArray(32) { 5 })
        val good = export(source, states = false)
        // Same entries, but the save's bytes differ from the manifest's hash.
        val manifest = ZipInputStream(ByteArrayInputStream(good)).use { zip ->
            var text = ""
            while (true) {
                val entry = zip.nextEntry ?: break
                if (entry.name == SaveArchive.MANIFEST) text = zip.readBytes().decodeToString()
            }
            text
        }
        val bad = zip("savedata/save00.bin" to ByteArray(32) { 6 }, SaveArchive.MANIFEST to manifest.encodeToByteArray())
        val target = layout("target")
        expect(SaveProblem.DAMAGED) { SaveArchive.stage(ByteArrayInputStream(bad), target) }
        // A file the manifest does not list is refused too.
        val extra = zip(
            "savedata/save00.bin" to ByteArray(32) { 5 },
            "savedata/save01.bin" to ByteArray(32) { 5 },
            SaveArchive.MANIFEST to manifest.encodeToByteArray(),
        )
        expect(SaveProblem.DAMAGED) { SaveArchive.stage(ByteArrayInputStream(extra), target) }
        assertTrue(target.dataRoot.listFiles().orEmpty().none { it.name.startsWith(".save-import-") })
    }

    @Test fun unsafeAndUnsupportedPathsAreRefused() {
        val target = layout("target")
        expect(SaveProblem.UNSAFE_PATH) { SaveArchive.stage(ByteArrayInputStream(zip("savedata/../x.bin" to ByteArray(1))), target) }
        expect(SaveProblem.UNSAFE_PATH) { SaveArchive.stage(ByteArrayInputStream(zip("/save00.bin" to ByteArray(1))), target) }
        expect(SaveProblem.UNSAFE_PATH) { SaveArchive.stage(ByteArrayInputStream(zip("savedata\\save00.bin" to ByteArray(1))), target) }
        expect(SaveProblem.UNSUPPORTED_FILE) { SaveArchive.stage(ByteArrayInputStream(zip("readme.txt" to ByteArray(1))), target) }
        expect(SaveProblem.UNSUPPORTED_FILE) { SaveArchive.stage(ByteArrayInputStream(zip("savestates/a/b.oot3dsav" to state)), target) }
        expect(SaveProblem.UNSUPPORTED_FILE) { SaveArchive.stage(ByteArrayInputStream(zip("savedata/crashes/x" to ByteArray(1))), target) }
        expect(SaveProblem.DUPLICATE) {
            SaveArchive.stage(ByteArrayInputStream(zip("save00.bin" to ByteArray(1), "x/save00.bin" to ByteArray(1))), target)
        }
    }

    @Test fun saveStatesNeedTheRuntimeMagic() {
        val target = layout("target")
        expect(SaveProblem.NOT_A_SAVE_STATE) {
            SaveArchive.stage(ByteArrayInputStream(zip("savestates/fake.oot3dsav" to ByteArray(64))), target)
        }
        val plan = SaveArchive.stage(ByteArrayInputStream(zip("quick.oot3dsav" to state)), target)
        assertEquals(SaveKind.SAVE_STATE, plan.files.single().kind)
        SaveArchive.discard(plan)
    }

    @Test fun sizesAreBoundedWhileReading() {
        val target = layout("target")
        val big = ByteArray((SaveArchive.MAX_SAVE_FILE_BYTES + 1).toInt())
        expect(SaveProblem.TOO_LARGE) { SaveArchive.stage(ByteArrayInputStream(zip("savedata/save00.bin" to big)), target) }
    }

    @Test fun otherFilesAreNotArchives() {
        val target = layout("target")
        expect(SaveProblem.NOT_AN_ARCHIVE) { SaveArchive.stage(ByteArrayInputStream(ByteArray(100) { 0x41 }), target) }
        expect(SaveProblem.EMPTY) { SaveArchive.stage(ByteArrayInputStream(zip("folder/" to ByteArray(0))), target) }
    }

    @Test fun backupsArePrunedAndStaleStagingRemoved() {
        val target = layout("target")
        for (index in 1..7) File(target.backups, "2026010$index-000000").mkdirs()
        SaveArchive.pruneBackups(target, 5)
        assertEquals(
            (3..7).map { "2026010$it-000000" },
            target.backups.listFiles().orEmpty().map { it.name }.sorted(),
        )
        File(target.dataRoot, ".save-import-old").mkdirs()
        SaveArchive.cleanStaleStaging(target)
        assertFalse(File(target.dataRoot, ".save-import-old").exists())
    }

    @Test fun pathComponentsAreChecked() {
        assertTrue(SaveArchive.safeComponent("save00.bin"))
        assertTrue(SaveArchive.safeComponent("state 2026-10-26.oot3dsav"))
        assertFalse(SaveArchive.safeComponent(".."))
        assertFalse(SaveArchive.safeComponent("a:b"))
        assertFalse(SaveArchive.safeComponent("line\nbreak"))
        assertFalse(SaveArchive.safeComponent(""))
    }
}
