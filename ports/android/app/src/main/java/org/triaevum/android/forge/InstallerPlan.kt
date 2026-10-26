package org.triaevum.android.forge

import org.triaevum.android.forge.InstallationStatus.State

/** The Forge jobs the installation service runs for this screen. */
enum class ForgeJob(val code: String) {
    INSTALL("install"),
    REFRESH("refresh"),
    REPAIR("repair"),
    /** The HD interface pack: the user's download, or the import of an archive they picked. */
    HD_DOWNLOAD("ui_pack_download"),
    HD_IMPORT("ui_pack_import"),
    ;

    /** A job that changes only the stored HD pack, never the installed game. */
    val hdPack: Boolean get() = this == HD_DOWNLOAD || this == HD_IMPORT

    companion object {
        fun of(code: String?): ForgeJob? = values().firstOrNull { it.code == code }
    }
}

/** The installer's state card: one per installation state, plus the job and interruption cases. */
enum class StateCard {
    /** The state is being inspected. */
    CHECKING,
    /** A developer APK without a Forge package. */
    NO_PACKAGE,
    NOT_INSTALLED,
    /** This app's installation service runs a job. */
    WORKING,
    /** Another Forge holds the activation lock. */
    PREPARING_ELSEWHERE,
    READY,
    UPDATE,
    REPAIR,
    /** An interrupted restore of a backup is being undone before anything else. */
    RESTORE_RECOVERING,
    NEEDS_ROM,
    INCOMPATIBLE,
    FAILED,
    /** The previous installation's process ended mid-job; the installed game is unchanged. */
    INTERRUPTED_KEPT,
    /** The previous installation's process ended mid-job; nothing is installed. */
    INTERRUPTED_NOTHING,
    /** The previous update or repair's process ended mid-job. */
    INTERRUPTED_UPDATE,
}

/** The state card's button. */
enum class StateAction { UPDATE, REPAIR, RETRY_INSTALL, CHECK_AGAIN }

/** What the installer shows: the card, its button, and whether Play and Select ROM are offered. */
data class InstallerPlan(
    val card: StateCard,
    val action: StateAction? = null,
    val play: Boolean = false,
    val select: Boolean = false,
    /** Start the update without a tap (an APK replacement found at start, tried once per screen). */
    val autoRefresh: Boolean = false,
)

/**
 * Decides what the installer shows from the shared installation state alone (UX plan WP-09):
 * no flags of its own beyond the running job and an interruption reported once. Play is offered
 * only for a state that can play ([InstallationStatus.canPlay]), never while a job runs, an
 * update is pending or a restore is being undone.
 */
object InstallerPlanner {
    fun plan(
        hasPackage: Boolean,
        status: InstallationStatus?,
        running: ForgeJob? = null,
        interrupted: ForgeJob? = null,
        canRetryRom: Boolean = false,
        refreshTried: Boolean = false,
        developerProfile: Boolean = false,
    ): InstallerPlan {
        if (!hasPackage) return InstallerPlan(StateCard.NO_PACKAGE, play = developerProfile && running == null)
        if (running != null) return InstallerPlan(StateCard.WORKING)
        if (status == null) return InstallerPlan(StateCard.CHECKING)
        if (status.state == State.REPAIR_NEEDED && status.reason == "interrupted_restore") {
            return InstallerPlan(StateCard.RESTORE_RECOVERING)
        }
        val retry = if (canRetryRom) StateAction.RETRY_INSTALL else null
        when (interrupted) {
            ForgeJob.INSTALL -> when (status.state) {
                State.READY -> return InstallerPlan(StateCard.INTERRUPTED_KEPT, retry, play = true, select = true)
                State.NOT_INSTALLED, State.NEEDS_GAME_INPUT ->
                    return InstallerPlan(StateCard.INTERRUPTED_NOTHING, retry, play = status.canPlay, select = true)
                else -> {}
            }
            ForgeJob.REFRESH, ForgeJob.REPAIR -> if (status.state == State.UPDATE_AVAILABLE) {
                return InstallerPlan(StateCard.INTERRUPTED_UPDATE, StateAction.UPDATE, select = true)
            }
            // An interrupted HD pack job left the installation as it was; a partial file never counts as the pack.
            ForgeJob.HD_DOWNLOAD, ForgeJob.HD_IMPORT, null -> {}
        }
        return when (status.state) {
            State.NOT_INSTALLED -> InstallerPlan(StateCard.NOT_INSTALLED, select = true)
            State.PREPARING -> InstallerPlan(StateCard.PREPARING_ELSEWHERE, StateAction.CHECK_AGAIN)
            State.READY -> InstallerPlan(StateCard.READY, play = true, select = true)
            State.UPDATE_AVAILABLE ->
                InstallerPlan(StateCard.UPDATE, StateAction.UPDATE, select = true, autoRefresh = !refreshTried)
            State.REPAIR_NEEDED -> InstallerPlan(StateCard.REPAIR, StateAction.REPAIR, select = true)
            State.NEEDS_GAME_INPUT -> InstallerPlan(StateCard.NEEDS_ROM, play = status.canPlay, select = true)
            State.INCOMPATIBLE -> InstallerPlan(StateCard.INCOMPATIBLE, select = true)
            State.FAILED -> InstallerPlan(StateCard.FAILED, StateAction.CHECK_AGAIN, select = true)
        }
    }
}
