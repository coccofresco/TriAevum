package org.triaevum.android.forge

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.triaevum.android.forge.InstallationStatus.State

class InstallerPlannerTest {
    /** [canPlay] defaults to forge-core's answer for a state with the repair data merely gone. */
    private fun status(
        state: State,
        reason: String = "",
        details: String = "",
        canPlay: Boolean = state == State.READY || state == State.NEEDS_GAME_INPUT,
    ) = InstallationStatus(state, recipe = "oot3d-eur", reason = reason, details = details, canPlay = canPlay)

    private fun plan(
        status: InstallationStatus?,
        running: ForgeJob? = null,
        interrupted: ForgeJob? = null,
        canRetryRom: Boolean = false,
        refreshTried: Boolean = false,
    ) = InstallerPlanner.plan(true, status, running, interrupted, canRetryRom, refreshTried)

    @Test
    fun playOnlyForStatesThatCanPlay() {
        for (state in State.values()) {
            val plan = plan(status(state))
            assertEquals("Play for $state", state == State.READY || state == State.NEEDS_GAME_INPUT, plan.play)
        }
        // "Remove prepared game" deleted the title: forge-core says it cannot play, whatever the
        // details text says, and Play is not offered.
        assertFalse(plan(status(State.NEEDS_GAME_INPUT, details = "any text", canPlay = false)).play)
        assertFalse(plan(status(State.NOT_INSTALLED), interrupted = ForgeJob.INSTALL).play)
        assertTrue(plan(status(State.NEEDS_GAME_INPUT, canPlay = true), interrupted = ForgeJob.INSTALL).play)
    }

    @Test
    fun hdPackJobsShowTheirWorkAndNeverAnInterruption() {
        assertEquals(StateCard.WORKING, plan(status(State.READY), running = ForgeJob.HD_DOWNLOAD).card)
        assertFalse(plan(status(State.READY), running = ForgeJob.HD_IMPORT).play)
        // A download whose process died leaves the installation as it was.
        val after = plan(status(State.READY), interrupted = ForgeJob.HD_DOWNLOAD)
        assertEquals(StateCard.READY, after.card)
        assertTrue(after.play)
    }

    @Test
    fun cardPerState() {
        val expected = mapOf(
            State.NOT_INSTALLED to StateCard.NOT_INSTALLED,
            State.PREPARING to StateCard.PREPARING_ELSEWHERE,
            State.READY to StateCard.READY,
            State.UPDATE_AVAILABLE to StateCard.UPDATE,
            State.REPAIR_NEEDED to StateCard.REPAIR,
            State.NEEDS_GAME_INPUT to StateCard.NEEDS_ROM,
            State.INCOMPATIBLE to StateCard.INCOMPATIBLE,
            State.FAILED to StateCard.FAILED,
        )
        for ((state, card) in expected) assertEquals(card, plan(status(state)).card)
        assertEquals(StateAction.REPAIR, plan(status(State.REPAIR_NEEDED, "interrupted_activation")).action)
        assertEquals(StateAction.CHECK_AGAIN, plan(status(State.FAILED)).action)
        assertNull(plan(status(State.INCOMPATIBLE, "target_mismatch")).action)
    }

    @Test
    fun replacedApkStartsTheUpdateOnceAndHidesPlay() {
        val update = status(State.UPDATE_AVAILABLE)
        val first = plan(update)
        assertEquals(StateCard.UPDATE, first.card)
        assertEquals(StateAction.UPDATE, first.action)
        assertTrue(first.autoRefresh)
        assertFalse(first.play)
        val again = plan(update, refreshTried = true)
        assertFalse(again.autoRefresh)
        assertEquals(StateAction.UPDATE, again.action)
        // While the refresh runs, nothing else is offered; afterwards Ready shows Play.
        val working = plan(update, running = ForgeJob.REFRESH)
        assertEquals(StateCard.WORKING, working.card)
        assertFalse(working.play)
        assertFalse(working.select)
        assertTrue(plan(status(State.READY), refreshTried = true).play)
    }

    @Test
    fun interruptedInstallationOffersRetryOrReselect() {
        val kept = plan(status(State.READY), interrupted = ForgeJob.INSTALL, canRetryRom = true)
        assertEquals(StateCard.INTERRUPTED_KEPT, kept.card)
        assertEquals(StateAction.RETRY_INSTALL, kept.action)
        assertTrue(kept.play)
        assertTrue(kept.select)
        val nothing = plan(status(State.NOT_INSTALLED), interrupted = ForgeJob.INSTALL)
        assertEquals(StateCard.INTERRUPTED_NOTHING, nothing.card)
        assertNull(nothing.action)
        assertFalse(nothing.play)
        assertTrue(nothing.select)
        // Killed during activation: the shared state says what is needed (Repair).
        val activation = plan(status(State.REPAIR_NEEDED, "interrupted_activation"), interrupted = ForgeJob.INSTALL)
        assertEquals(StateCard.REPAIR, activation.card)
        assertEquals(StateAction.REPAIR, activation.action)
        assertFalse(activation.play)
    }

    @Test
    fun interruptedUpdateIsOfferedAgainWithoutStartingItself() {
        val plan = plan(status(State.UPDATE_AVAILABLE), interrupted = ForgeJob.REFRESH)
        assertEquals(StateCard.INTERRUPTED_UPDATE, plan.card)
        assertEquals(StateAction.UPDATE, plan.action)
        assertFalse(plan.autoRefresh)
        assertFalse(plan.play)
    }

    @Test
    fun restoreRecoveryComesFirst() {
        val plan = plan(status(State.REPAIR_NEEDED, "interrupted_restore"), interrupted = ForgeJob.INSTALL)
        assertEquals(StateCard.RESTORE_RECOVERING, plan.card)
        assertFalse(plan.play)
        assertNull(plan.action)
    }

    @Test
    fun jobAndInspectionStates() {
        assertEquals(StateCard.CHECKING, plan(null).card)
        assertFalse(plan(null).play)
        assertEquals(StateCard.WORKING, plan(status(State.READY), running = ForgeJob.INSTALL).card)
        assertFalse(plan(status(State.READY), running = ForgeJob.INSTALL).play)
        val developer = InstallerPlanner.plan(false, null, developerProfile = true)
        assertEquals(StateCard.NO_PACKAGE, developer.card)
        assertTrue(developer.play)
        assertFalse(InstallerPlanner.plan(false, null, developerProfile = false).play)
    }

    @Test
    fun jobCodes() {
        for (job in ForgeJob.values()) assertEquals(job, ForgeJob.of(job.code))
        assertNull(ForgeJob.of("rom_recognize"))
        assertEquals(State.FAILED, State.of("something_new"))
    }
}
