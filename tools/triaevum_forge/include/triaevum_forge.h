/* Copyright TriAevum contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * TriAevum Forge ABI v4. Requests, events and results are UTF-8 JSON byte
 * spans; buffers Forge returns are released with TriAevumForgeFreeBuffer.
 * Events are polled, never delivered from Forge's worker threads. No C++
 * exception or Rust panic crosses this interface.
 *
 * Request: {"format":"triaevum_forge_request_v1","job":"<name>",...}
 * Events:  {"event":"stage"|"report"|"progress"|"succeeded"|"failed"|"cancelled",...}
 * Exactly one terminal event (succeeded, failed, cancelled) ends a job.
 *
 * Failed: {"event":"failed","error":{"code","stage","message","os_code",
 *          "retryable","operation"?,"subject"?,"reason"?}}. "code" is one of
 * the TriAevumForgeErrorKind names below; "operation", "subject" and
 * "reason" appear only when known, and a host ignores codes it does not
 * know. Succeeded: {"event":"succeeded","result":{...},"notices"?:[...]},
 * each notice {"outcome","details"} a non-error outcome to explain.
 *
 * Version history: v2 adds TriAevumForgeInspect, TriAevumForgeExplain and
 * the optional event fields above; v3 adds TriAevumForgeInventory; v4 adds
 * TriAevumForgeDiagnostics. Every earlier declaration is unchanged, so a host
 * written for an earlier version works with a v4 library.
 */
#ifndef TRIAEVUM_FORGE_H
#define TRIAEVUM_FORGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRIAEVUM_FORGE_ABI_VERSION 4u

typedef struct TriAevumForgeBuffer {
    uint8_t *data;
    size_t len;
} TriAevumForgeBuffer;

typedef enum TriAevumForgeStatus {
    TRIAEVUM_FORGE_OK = 0,
    TRIAEVUM_FORGE_INVALID_ARGUMENT = 1,
    TRIAEVUM_FORGE_NO_EVENT = 2,
    TRIAEVUM_FORGE_BUSY = 3,
    TRIAEVUM_FORGE_FAILED = 4
} TriAevumForgeStatus;

/* The error families of "error.code" (stable numeric values). */
typedef enum TriAevumForgeErrorKind {
    TRIAEVUM_FORGE_ERROR_INVALID_INPUT = 1,
    TRIAEVUM_FORGE_ERROR_ENCRYPTED_INPUT = 2,
    TRIAEVUM_FORGE_ERROR_MISSING_KEY = 3,
    TRIAEVUM_FORGE_ERROR_UNSUPPORTED_FORMAT = 4,
    TRIAEVUM_FORGE_ERROR_UNSUPPORTED_REVISION = 5,
    TRIAEVUM_FORGE_ERROR_INCOMPATIBLE_TARGET = 6,
    TRIAEVUM_FORGE_ERROR_ARTIFACT_INTEGRITY = 7,
    TRIAEVUM_FORGE_ERROR_INVALID_SETTINGS = 8,
    TRIAEVUM_FORGE_ERROR_STORAGE_FULL = 9,
    TRIAEVUM_FORGE_ERROR_PERMISSION_DENIED = 10,
    TRIAEVUM_FORGE_ERROR_BUSY = 11,
    TRIAEVUM_FORGE_ERROR_RECOVERY_REQUIRED = 12,
    TRIAEVUM_FORGE_ERROR_NATIVE_PREFLIGHT = 13,
    TRIAEVUM_FORGE_ERROR_PREPARATION_FAILED = 14,
    TRIAEVUM_FORGE_ERROR_CANCELLED = 15,
    TRIAEVUM_FORGE_ERROR_INTERNAL = 16
} TriAevumForgeErrorKind;

typedef struct TriAevumForgeJob TriAevumForgeJob;

uint32_t TriAevumForgeGetAbiVersion(void);

/* {"format":"triaevum_forge_capabilities_v1","target":...,"jobs":[...]} */
TriAevumForgeBuffer TriAevumForgeGetCapabilities(void);

void TriAevumForgeFreeBuffer(TriAevumForgeBuffer buffer);

/* On failure no job is created and *error_out (if given) holds a failed
 * event. A request may pass a duplicated file descriptor ("fd"); Forge then
 * owns and closes it, also when starting the job fails after parsing. */
TriAevumForgeStatus TriAevumForgeStartJob(const uint8_t *request, size_t request_len,
                                          TriAevumForgeJob **job_out,
                                          TriAevumForgeBuffer *error_out);

/* Waits up to timeout_ms (0: no wait). TRIAEVUM_FORGE_NO_EVENT if none. */
TriAevumForgeStatus TriAevumForgePollEvent(TriAevumForgeJob *job, uint32_t timeout_ms,
                                           TriAevumForgeBuffer *event_out);

TriAevumForgeStatus TriAevumForgeCancelJob(TriAevumForgeJob *job);

/* Waits for the job to end; its remaining events stay pollable. */
TriAevumForgeStatus TriAevumForgeJoinJob(TriAevumForgeJob *job);

/* Cancels, joins and frees the job. */
void TriAevumForgeDestroyJob(TriAevumForgeJob *job);

/* v2. The installation state, read-only (it may hash installed files; call
 * it off a UI thread). Request:
 * {"format":"triaevum_forge_inspect_v1","package_root"?,"data_root"?,
 *  "android"?:{"native_library_dir","profile_root"},"verify_sources"?}
 * Result: {"format":"triaevum_installation_state_v1","state":S,...} with S
 * not_installed, preparing, ready, update_available, repair_needed,
 * needs_game_input, incompatible or failed, plus "explanation" (as below)
 * for the states that need the user. Never fails: an unusable request is a
 * "failed" state. */
TriAevumForgeBuffer TriAevumForgeInspect(const uint8_t *request, size_t request_len);

/* v2. The explanation of a failed or cancelled event, an event's "error"
 * object or a notice: {"format":"triaevum_explanation_v1","id","severity",
 * "title","cause","next_action","primary_action","details","error"?}.
 * Texts are keyed "<id>.title", "<id>.cause", "<id>.next_action". JSON null
 * for any other input. */
TriAevumForgeBuffer TriAevumForgeExplain(const uint8_t *input, size_t input_len);

/* v3. Where the installation keeps its files, read-only (it measures
 * folders; call it off a UI thread). Request:
 * {"format":"triaevum_forge_inventory_v1","package_root"?,"data_root"?,
 *  "android"?:{"native_library_dir","profile_root","app_data_dir"?,
 *  "package_name"?},"sizes"?}
 * Result: {"format":"triaevum_inventory_v1","status":"ok","platform",
 * "roots":{"package","activation","data","state"},"activity":{
 * "game_running","forge_running","explanation"?},"entries":[{"category",
 * "logical_name","parent","path","kind","origin","active","present",
 * "managed","size","files","backup"}]}; origin is default, explicit or
 * legacy, backup include, optional or exclude. Never fails: an unusable
 * request is {"format",...,"status":"failed","explanation":{...}}. */
TriAevumForgeBuffer TriAevumForgeInventory(const uint8_t *request, size_t request_len);

/* v4. Diagnostics of an installation (reads logs; call it off a UI thread).
 * Request: {"format":"triaevum_forge_diagnostics_v1","package_root"?,
 *  "data_root"?,"android"?:{as for the inventory},"bundle"?:"<zip path>"}
 * Result: {"format":"triaevum_diagnostics_v1","status":"ok","text",
 * "bundle_plan":{"members":[{"name","size"}],"omitted":[{"name","reason"}]},
 * "last_game_run","game_exit","recovery","logs":{"game","forge"},
 * "bundle"?:{"path","bytes","members"}}: "text" is the redacted report
 * (at most 16 KB), "game_exit" and "recovery" explanations or null. With
 * "bundle" the redacted support bundle is written there atomically. Never
 * fails: an unusable request is "status":"failed" with its "explanation". */
TriAevumForgeBuffer TriAevumForgeDiagnostics(const uint8_t *request, size_t request_len);

#ifdef __cplusplus
}
#endif

#endif
