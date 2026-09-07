import fs from 'node:fs';

const [directory] = process.argv.slice(2);
if (!directory) throw new Error('Usage: node analyze_grass_visibility.mjs <capture-directory>');
const read = name => JSON.parse(fs.readFileSync(`${directory}/${name}`, 'utf8').replace(/^\uFEFF/, ''));
const frames = read('vulkan.json').frames;
const source = frames.filter(f => f.effect_geometry_source_published_count > 0);
const missing = source.filter(f => !f.effect_geometry_executed_count);
const runs = [];
for (const frame of missing) {
    const last = runs.at(-1);
    if (last && last.end + 1 === frame.frame_index) last.end = frame.frame_index;
    else runs.push({ start: frame.frame_index, end: frame.frame_index });
}
const sum = (list, field) => list.reduce((total, f) => total + (f[field] ?? 0), 0);
const mixed = source.filter(f => f.pica_composition_noncontiguous_world_target_count > 0);
const logs = fs.readFileSync(`${directory}/stderr.log`, 'utf8').split(/\r?\n/)
    .filter(line => line.startsWith('OOT3D grass smoke: '));
const field = (line, key) => Number(line.match(new RegExp(`(?:^| )${key}=([\\d.]+)`))?.[1]);
const drawing = logs.filter(line => line.includes('status=drawing') && field(line, 'pending') === 0);
const allDrawing = logs.filter(line => line.includes('status=drawing'));
const steadyDrawing = drawing.filter(line => Number(line.match(/ upload=\d+\/\d+\/(\d+)/)?.[1]) === 0);
const firstReady = drawing[0];
const lastBuild = logs.at(-1)?.match(/ async_build=(\d+)\/(\d+)\/([\d.]+)/);
const distribution = values => {
    values = values.filter(Number.isFinite).sort((a, b) => a - b);
    return values.length ? { samples: values.length,
        mean: values.reduce((a, b) => a + b, 0) / values.length,
        p95: values[Math.min(values.length - 1, Math.floor(values.length * 0.95))] } : null;
};
const report = {
    scope: 'structural_framebuffer_verification_not_fps_benchmark',
    note: 'Published sources do not imply visible blades: inspect startup and culled ranges in framebuffer captures.',
    frames: frames.length,
    source_frames: source.length,
    drawing_frames: source.length - missing.length,
    mixed_composition_source_frames: mixed.length,
    mixed_composition_drawing_frames: mixed.filter(f => f.effect_geometry_executed_count > 0).length,
    source_without_draw_ranges: runs,
    failed: sum(frames, 'effect_geometry_failed_count'),
    schedule_rejected: sum(frames, 'effect_geometry_schedule_rejected_count'),
    input_unavailable: sum(frames, 'effect_geometry_input_unavailable_count'),
    composition_mismatches: sum(frames, 'pica_composition_execution_mismatch_count'),
    sampled_selection_ms: distribution(drawing.map(line => field(line, 'select_ms'))),
    // Smoke logs are periodic plus status/work changes, not a time-weighted
    // frame census. Include pending replacements in these workload samples.
    sampled_grass_cpu_ms: distribution(allDrawing.map(line => field(line, 'cpu_ms'))),
    sampled_visible_blades: distribution(allDrawing.map(line => field(line, 'blades'))),
    sampled_resident_anchors: distribution(allDrawing.map(line => field(line, 'anchors'))),
    sampled_draw_calls: distribution(allDrawing.map(line => field(line, 'draws'))),
    placement_builds: lastBuild ? Number(lastBuild[1]) : null,
    placement_failures: lastBuild ? Number(lastBuild[2]) : null,
    placement_worker_ms_sum: lastBuild ? Number(lastBuild[3]) : null,
    first_complete_placement_frame: firstReady ? field(firstReady, 'frame') : null,
    builds_after_first_complete_placement: firstReady && lastBuild ?
        Number(lastBuild[1]) - Number(firstReady.match(/ async_build=(\d+)\//)?.[1]) : null,
    sampled_steady_grass_cpu_ms: distribution(steadyDrawing.map(line => field(line, 'cpu_ms'))),
    sampled_steady_selection_ms: distribution(steadyDrawing.map(line => field(line, 'select_ms'))),
    sampled_candidate_cluster_fraction: distribution(drawing.map(line =>
        field(line, 'candidate_clusters') / field(line, 'clusters'))),
    grass_gpu_ms: distribution(source.filter(f => f.effect_geometry_executed_count > 0).map(f => f.gpu?.grass_ms)),
    status_transitions: logs.filter((line, i) => {
        const status = value => value?.match(/ status=(.*?) camera=/)?.[1];
        return i === 0 || status(line) !== status(logs[i - 1]);
    }),
};
fs.writeFileSync(`${directory}/grass-visibility-summary.json`, JSON.stringify(report, null, 2));
console.log(JSON.stringify(report, null, 2));
if (!source.length || report.failed || report.schedule_rejected || report.composition_mismatches) process.exitCode = 1;
