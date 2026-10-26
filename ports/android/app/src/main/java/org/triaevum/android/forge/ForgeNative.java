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

    /**
     * The installation state for {"format":"triaevum_forge_inspect_v1","package_root":...,
     * "android":{"native_library_dir":...,"profile_root":...}}: tagged JSON
     * {"format":"triaevum_installation_state_v1","state":...}. Read-only, but it hashes installed
     * files: call it off the main thread.
     */
    public static native byte[] inspect(byte[] request);

    /**
     * The explanation {"format":"triaevum_explanation_v1","id","title","cause","next_action",
     * "primary_action","details",...} of a failed or cancelled event, an error object or a notice;
     * JSON null for anything else.
     */
    public static native byte[] explain(byte[] input);
}
