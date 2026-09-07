// View 7: Grass-independent contour detection. View 5: depth-occluded contours.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';

function pixels(file) {
    const data = fs.readFileSync(file);
    assert.equal(data.toString('ascii', 0, 2), 'BM');
    assert.equal(data.readUInt32LE(30), 0, 'Only uncompressed framebuffer BMPs');
    const offset = data.readUInt32LE(10);
    const width = data.readInt32LE(18);
    const height = Math.abs(data.readInt32LE(22));
    const bytes = data.readUInt16LE(28) / 8;
    assert.ok(bytes === 3 || bytes === 4);
    const stride = Math.ceil(width * bytes / 4) * 4;
    assert.ok(data.length >= offset + stride * height);
    const rgb = new Uint8Array(width * height * 3);
    for (let y = 0; y < height; ++y) {
        const row = data.readInt32LE(22) > 0 ? height - y - 1 : y;
        for (let x = 0; x < width; ++x) {
            const source = offset + row * stride + x * bytes;
            rgb.set(data.subarray(source, source + 3), (y * width + x) * 3);
        }
    }
    return {width, height, rgb};
}

const [first, second] = process.argv.slice(2);
assert.ok(first && second, 'Usage: node compare_outline_masks.mjs grass-on.bmp grass-off.bmp');
const pixelsOnly = process.argv.includes('--pixels-only');
const occlusion = process.argv.includes('--occlusion');
if (!pixelsOnly) {
    const json = file => JSON.parse(fs.readFileSync(file, 'utf8'));
    const runs = [first, second].map(file => {
        const root = path.dirname(file), capture = json(file + '.json');
        const invocation = json(path.join(root, 'invocation.json'));
        const config = json(path.join(root, 'config.json'));
        const frame = json(path.join(root, 'vulkan.json')).frames.find(f => f.frame_index === capture.host_frame);
        assert.ok(frame, 'Retain Vulkan diagnostics for the captured frame');
        assert.equal(invocation.diagnosticGuideView, occlusion ? 5 : 7,
            'Capture final visibility (--occlusion), or raw native contours');
        assert.ok(invocation.args.includes('--throughput-benchmark'), 'Use deterministic guest work');
        assert.equal(config.Graphics.Effects.Toon.OutlineEnabled, true);
        return {invocation, config, frame, capture};
    });
    const [on, off] = runs;
    assert.ok(on.invocation.executableSha256, 'Executable identity is required');
    assert.equal(on.invocation.executableSha256, off.invocation.executableSha256);
    assert.ok(on.invocation.options.state, 'Use an exact checkpoint');
    assert.equal(on.invocation.options.state, off.invocation.options.state);
    assert.equal(on.capture.host_frame, off.capture.host_frame);
    assert.deepEqual(on.capture.temporal_sample, off.capture.temporal_sample);
    assert.notEqual(on.config.Graphics.Grass.Quality, 'Off');
    assert.equal(off.config.Graphics.Grass.Quality, 'Off');
    const onGraphics = structuredClone(on.config.Graphics);
    const offGraphics = structuredClone(off.config.Graphics);
    delete onGraphics.Grass.Quality;
    delete offGraphics.Grass.Quality;
    assert.deepEqual(onGraphics, offGraphics, 'Only Grass enablement may differ between paired runs');
    assert.ok(on.frame.effect_geometry_executed_count > 0, 'Grass must actually draw, not merely be enabled');
    assert.equal(off.frame.effect_geometry_executed_count, 0);
}
const a = pixels(first), b = pixels(second);
assert.equal(a.width, b.width);
assert.equal(a.height, b.height);
let changed = 0, increased = 0, maximumDelta = 0, absoluteDelta = 0, activeA = 0, activeB = 0;
const bounds = {left: a.width, top: a.height, right: -1, bottom: -1};
for (let i = 0; i < a.rgb.length; i += 3) {
    let pixelDelta = 0;
    for (let c = 0; c < 3; ++c) {
        const delta = Math.abs(a.rgb[i + c] - b.rgb[i + c]);
        absoluteDelta += delta;
        pixelDelta = Math.max(pixelDelta, delta);
    }
    changed += pixelDelta > 0;
    increased += a.rgb[i] > b.rgb[i] || a.rgb[i + 1] > b.rgb[i + 1] || a.rgb[i + 2] > b.rgb[i + 2];
    if (pixelDelta > 0) {
        const x = (i / 3) % a.width, y = Math.floor(i / 3 / a.width);
        bounds.left = Math.min(bounds.left, x); bounds.right = Math.max(bounds.right, x);
        bounds.top = Math.min(bounds.top, y); bounds.bottom = Math.max(bounds.bottom, y);
    }
    maximumDelta = Math.max(maximumDelta, pixelDelta);
    activeA += a.rgb[i] > 0;
    activeB += b.rgb[i] > 0;
}
console.log(JSON.stringify({first, second, verification: pixelsOnly ? 'pixels_only' :
    occlusion ? 'grass_occlusion' : 'native_contour_isolation',
    width: a.width, height: a.height,
    activeA, activeB, changedPixels: changed, increasedPixels: increased, maximumDelta,
    changedBounds: changed ? bounds : null,
    meanAbsoluteChannelDelta: absoluteDelta / a.rgb.length}, null, 2));
// Empty masks are not proof of correct isolation.
process.exitCode = (occlusion ? changed > 0 && increased === 0 : changed === 0) &&
    activeA > 0 && activeB > 0 ? 0 : 1;
