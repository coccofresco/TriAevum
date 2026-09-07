// Real product evidence: private configs, free camera enabled, C-stick timeline.
import fs from 'node:fs';
import assert from 'node:assert/strict';

const [gameplayFile, introFile] = process.argv.slice(2);
assert(gameplayFile && introFile, 'Usage: node verify_gameplay_camera.mjs gameplay/runtime.json intro/runtime.json');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const gameplay = read(gameplayFile);
const intro = read(introFile);
for (const run of [gameplay, intro]) {
    assert(run.a32_runtime_profile.topscreen_camera_enabled, 'Free camera was not enabled');
    assert(run.a32_runtime_profile.topscreen_camera_update_calls > 30, 'Product camera entries were not reached');
    assert(run.hid_input.timeline_enabled && run.hid_input.active_segment_host_frames > 30, 'Missing camera input');
    assert(run.benchmark_window.measured_frames > 100, 'The runtime did not progress beyond startup');
}
assert(gameplay.a32_runtime_profile.topscreen_camera_active_calls > 0, 'Gameplay camera never acquired control');
assert.equal(gameplay.a32_runtime_profile.topscreen_camera_last_ownership.game_mode, 0);
assert.equal(intro.a32_runtime_profile.topscreen_camera_active_calls, 0, 'Free camera took control of the intro');
assert.notEqual(intro.a32_runtime_profile.topscreen_camera_last_ownership.game_mode, 0);
console.log('Product gameplay camera routing and intro exclusion: passed');
