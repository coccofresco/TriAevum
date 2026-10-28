package org.triaevum.android

import android.app.Activity
import android.app.ActivityManager
import android.content.Intent

/**
 * Opens the game from the installer. The game runs once per process (SDL activity, title library,
 * guest memory): when its activity is already running, it comes to the front instead of a second
 * instance being started over it. Idea from Ocarina ReTuned's GameSession, reimplemented.
 */
object GameSession {
    /** Flags of the game intent: an existing game activity in this task is reordered, not duplicated. */
    const val FLAGS = Intent.FLAG_ACTIVITY_REORDER_TO_FRONT or Intent.FLAG_ACTIVITY_SINGLE_TOP

    private val GAME = TriAevumActivity::class.java.name

    fun open(activity: Activity) {
        // A game started in a task of its own (a developer launch, or a task the system split
        // off): bring that task forward.
        try {
            val tasks = activity.getSystemService(ActivityManager::class.java).appTasks
            for (task in tasks) {
                val info = task.taskInfo
                if (info.taskId == activity.taskId) continue
                if (info.baseActivity?.className == GAME || info.topActivity?.className == GAME) {
                    task.moveToFront()
                    return
                }
            }
        } catch (_: RuntimeException) {
            // A task that ended meanwhile: start the game as usual.
        }
        activity.startActivity(Intent(activity, TriAevumActivity::class.java).addFlags(FLAGS))
    }
}
