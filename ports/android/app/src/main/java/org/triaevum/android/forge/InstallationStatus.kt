package org.triaevum.android.forge

import java.util.Locale
import org.json.JSONObject

/**
 * An explanation of Forge's shared catalogue (forge-core `explain.rs`, UX plan Appendix B): the
 * installer shows these texts instead of raw error messages.
 */
data class Explanation(
    val id: String,
    val title: String,
    val cause: String,
    val nextAction: String,
    val primaryAction: String,
    val details: String,
) {
    companion object {
        /** An explanation object (`triaevum_explanation_v1`), or null for anything else. */
        fun from(value: JSONObject?): Explanation? {
            if (value == null || value.optString("format") != "triaevum_explanation_v1") return null
            return Explanation(
                id = value.optString("id"),
                title = value.optString("title"),
                cause = value.optString("cause"),
                nextAction = value.optString("next_action"),
                primaryAction = value.optString("primary_action"),
                details = value.optString("details"),
            )
        }

        /**
         * [ForgeNative.explain] of a failed or cancelled event, an error object or a notice, in
         * the app's language ([request]).
         */
        fun of(input: JSONObject): Explanation? = try {
            val bytes = ForgeNative.explain(request(input)) ?: return null
            val text = String(bytes).trim()
            if (text == "null" || text.isEmpty()) null else from(JSONObject(text))
        } catch (_: Exception) {
            null
        }

        /**
         * The explain request: [input] with the app's language, which Forge uses when its
         * catalogue has that language (French, German, Spanish, Italian) and English otherwise,
         * like the app's own string resources.
         */
        fun request(input: JSONObject): ByteArray =
            JSONObject(input.toString()).put("language", Locale.getDefault().language).toString().toByteArray()
    }
}

/**
 * The shared installation state (UX plan WP-01, forge-core `state.rs`) as the Android installer
 * reads it from [ForgeNative.inspect]: the same answer the desktop Forge and the CLI's `status`
 * give for the same files. The installer keeps no state flags of its own beyond the job it runs.
 */
data class InstallationStatus(
    val state: State,
    val recipe: String = "",
    val packageVersion: String? = null,
    val language: String? = null,
    /** The repair or incompatibility reason's kind ("interrupted_activation", "target_mismatch", ...). */
    val reason: String = "",
    val details: String = "",
    val installedVersion: String? = null,
    val presentVersion: String? = null,
    val explanation: Explanation? = null,
    /**
     * Whether the installed title can start as it is: the inspection's `can_play`, computed by
     * forge-core (`InstallationState::can_play`: ready, or needing the ROM only because the data
     * kept for repairs is gone; not after "Remove prepared game", which deleted the title).
     */
    val canPlay: Boolean = false,
) {
    enum class State(val code: String) {
        NOT_INSTALLED("not_installed"),
        PREPARING("preparing"),
        READY("ready"),
        UPDATE_AVAILABLE("update_available"),
        REPAIR_NEEDED("repair_needed"),
        NEEDS_GAME_INPUT("needs_game_input"),
        INCOMPATIBLE("incompatible"),
        FAILED("failed"),
        ;

        companion object {
            fun of(code: String): State = values().firstOrNull { it.code == code } ?: FAILED
        }
    }

    companion object {
        const val FORMAT = "triaevum_installation_state_v1"

        /** Parses [ForgeNative.inspect]'s answer; anything unreadable is [State.FAILED]. */
        fun parse(text: String): InstallationStatus = try {
            val value = JSONObject(text)
            if (value.optString("format") != FORMAT) throw IllegalArgumentException("format")
            fun optText(key: String): String? = if (value.isNull(key)) null else value.optString(key)
            val reason = value.optJSONObject("reason")?.optString("kind").orEmpty()
            InstallationStatus(
                state = State.of(value.optString("state")),
                recipe = value.optString("recipe"),
                packageVersion = optText("package_version"),
                language = optText("language"),
                reason = reason,
                details = value.optString("details"),
                installedVersion = value.optJSONObject("installed_package_identity")?.let {
                    if (it.isNull("version")) null else it.optString("version")
                },
                presentVersion = value.optJSONObject("present_package_identity")?.let {
                    if (it.isNull("version")) null else it.optString("version")
                },
                explanation = Explanation.from(value.optJSONObject("explanation")),
                canPlay = value.optBoolean("can_play", false),
            )
        } catch (_: Exception) {
            InstallationStatus(State.FAILED, details = text.take(300))
        }
    }
}
