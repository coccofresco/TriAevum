import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';

const output = process.argv[2];
const { frames } = JSON.parse(fs.readFileSync(path.join(output, 'vulkan.json'), 'utf8'));
const invocation = JSON.parse(fs.readFileSync(path.join(output, 'invocation.json'), 'utf8'));
if (invocation.options.overrides) {
    const requested = JSON.parse(fs.readFileSync(invocation.options.overrides, 'utf8')).Graphics?.AA;
    if (requested?.Mode === 'MSAA') {
        assert(frames.some(f => f.msaa_samples === requested.MsaaSamples),
            'requested MSAA was not exercised by this run');
    }
}
const errors = ['vulkan_validation_error_count', 'nri_validation_error_count',
    'effect_geometry_failed_count', 'display_effect_graph_failed_pass_count'];
for (const frame of frames) {
    for (const field of errors) assert.equal(frame[field], 0, `frame ${frame.frame_index}: ${field}`);
}
if (process.argv[3] === 'stress') {
    const on = frames.filter(f => f.toon_draw_count > 0).length;
    assert(on > 10, 'no enhanced frames observed');
    // Native draw count naming is backend-owned; nonempty render scopes also prove presentation.
    const native = frames.filter(f => f.toon_draw_count === 0 && f.display_transfer_count > 0).length;
    assert(native > 10, 'no native presentation frames observed');
    fs.writeFileSync(path.join(output, 'hotkeys-verification.json'), JSON.stringify({
        frames: frames.length, enhancedFrames: on, nativeFrames: native, validationErrors: 0,
        targetAllocations: frames.reduce((sum, f) => sum + f.nri_pica_owned_render_target_count, 0),
        targetImageAllocations: frames.reduce((sum, f) => sum + f.nri_pica_owned_render_target_image_count, 0),
        mode: 'repeated physical F2 presses during bounded playback'
    }, null, 2));
    console.log(`F2 stress passed: ${frames.length} frames, ${on} enhanced, ${native} native, zero GPU/graph errors.`);
    process.exit(0);
}
const fields = ['effect_geometry_executed_count', 'toon_draw_count',
    'pica_material_toon_draw_count', 'cacao_pass_count', 'hiz_reflection_count', 'fidelityfx_sssr_count'];
function interval(name, from, to, off) {
    const samples = frames.filter(f => f.frame_index >= from && f.frame_index <= to);
    assert.equal(samples.length, to - from + 1, `missing ${name} frames`);
    const passes = Object.fromEntries(fields.map(field =>
        [field, samples.reduce((sum, f) => sum + f[field], 0)]));
    if (off) for (const [field, count] of Object.entries(passes)) {
        assert.equal(count, 0, `${name}: ${field} still executed during F2 override`);
    }
    return { name, from, to, passes };
}
const segments = [interval('configured', 100, 160, false),
    interval('override_held_with_repeat', 240, 330, true),
    interval('restored_by_short_tap', 450, 510, false),
    interval('override_with_f1_open', 720, 780, true),
    interval('restored_f1_closed', 900, 960, false)];
const baseline = segments[0].passes;
// This stationary checkpoint has already allocated its native targets before
// frame 100. Hotkeys must not retire/recreate those targets or transfer images.
for (const frame of frames.filter(f => f.frame_index >= 100 && f.frame_index <= 960)) {
    assert.equal(frame.nri_pica_owned_render_target_count, 0,
        `frame ${frame.frame_index}: effect toggle recreated native targets`);
}
assert(baseline.effect_geometry_executed_count > 0 && baseline.toon_draw_count > 0 &&
    baseline.cacao_pass_count > 0, 'use a scene/profile with Grass, Toon and CACAO active');
for (const segment of [segments[2], segments[4]]) {
    for (const field of fields) {
        if (baseline[field] > 0) assert(segment.passes[field] > 0, `${segment.name}: ${field} not restored`);
    }
}
fs.writeFileSync(path.join(output, 'hotkeys-verification.json'), JSON.stringify({
    frames: frames.length, validationErrors: 0, segments,
    note: 'Framebuffer captures verify F1 hidden/open and its visible F2 status; this is not a performance benchmark.'
}, null, 2));
console.log('F2 render verification passed: configured/off/restored/off/restored, zero GPU validation or effect-graph errors.');
