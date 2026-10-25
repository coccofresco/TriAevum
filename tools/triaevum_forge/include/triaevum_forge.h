/* Copyright TriAevum contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * TriAevum Forge ABI v1. Requests, events and results are UTF-8 JSON byte
 * spans; buffers Forge returns are released with TriAevumForgeFreeBuffer.
 * Events are polled, never delivered from Forge's worker threads. No C++
 * exception or Rust panic crosses this interface.
 *
 * Request: {"format":"triaevum_forge_request_v1","job":"<name>",...}
 * Events:  {"event":"stage"|"progress"|"succeeded"|"failed"|"cancelled",...}
 * Exactly one terminal event (succeeded, failed, cancelled) ends a job.
 */
#ifndef TRIAEVUM_FORGE_H
#define TRIAEVUM_FORGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRIAEVUM_FORGE_ABI_VERSION 1u

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

#ifdef __cplusplus
}
#endif

#endif
