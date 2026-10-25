package org.triaevum.android.forge;

/**
 * The Rust Forge engine (libtriaevum_forge.so). Requests and events are UTF-8 JSON: requests
 * {"format":"triaevum_forge_request_v1","job":...}, events {"event":"stage"|"progress"|
 * "succeeded"|"failed"|"cancelled",...}. A job handle stays valid until destroyJob, which also
 * cancels and joins it. A request may carry a detached file descriptor ("fd"); Forge owns and
 * closes it from then on.
 */
public final class ForgeNative {
    static {
        System.loadLibrary("triaevum_forge");
    }

    private ForgeNative() {}

    public static native byte[] capabilities();

    /** @throws IllegalArgumentException with a failed event when the request is refused. */
    public static native long startJob(byte[] request);

    /** The next event, waiting up to timeoutMs; null when none arrived. */
    public static native byte[] pollEvent(long job, int timeoutMs);

    public static native void cancelJob(long job);

    public static native void destroyJob(long job);
}
