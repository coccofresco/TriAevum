package org.triaevum.android;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertTrue;
import org.junit.Test;

public final class SurfaceExtentPolicyTest {
    @Test public void preservesLandscapeAndPortraitAspect() {
        assertArrayEquals(new int[] {1560, 720}, SurfaceExtentPolicy.fit(2340, 1080, 720));
        assertArrayEquals(new int[] {720, 1560}, SurfaceExtentPolicy.fit(1080, 2340, 720));
        assertArrayEquals(new int[] {1280, 720}, SurfaceExtentPolicy.fit(1920, 1080, 720));
        assertArrayEquals(new int[] {960, 720}, SurfaceExtentPolicy.fit(1024, 768, 720));
    }

    @Test public void neverUpscalesAndAllowsNativeResolution() {
        assertArrayEquals(new int[] {640, 360}, SurfaceExtentPolicy.fit(640, 360, 720));
        assertArrayEquals(new int[] {2340, 1080}, SurfaceExtentPolicy.fit(2340, 1080, 0));
        assertArrayEquals(new int[] {1, 1}, SurfaceExtentPolicy.fit(0, 0, 720));
    }

    @Test public void aspectErrorIsAtMostHalfAPixel() {
        for (int width = 721; width <= 3000; width += 137) {
            int[] extent = SurfaceExtentPolicy.fit(width, 1081, 720);
            assertTrue(extent[0] <= width && extent[1] <= 1081);
            double scale = 720.0 / Math.min(width, 1081);
            assertTrue(Math.abs(extent[0] - width * scale) <= 0.5);
            assertTrue(Math.abs(extent[1] - 1081 * scale) <= 0.5);
        }
    }
}
