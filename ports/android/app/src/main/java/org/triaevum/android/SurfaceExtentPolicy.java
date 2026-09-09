package org.triaevum.android;

/** Output-buffer budget, independent of game projection and internal render scale. */
final class SurfaceExtentPolicy {
    private SurfaceExtentPolicy() {}

    static int[] fit(int width, int height, int maximumShortEdge) {
        width = Math.max(1, width);
        height = Math.max(1, height);
        int shortEdge = Math.min(width, height);
        if (maximumShortEdge <= 0 || shortEdge <= maximumShortEdge) {
            return new int[] { width, height };
        }
        double scale = (double) maximumShortEdge / shortEdge;
        return new int[] {
            Math.max(1, (int) Math.round(width * scale)),
            Math.max(1, (int) Math.round(height * scale))
        };
    }
}
