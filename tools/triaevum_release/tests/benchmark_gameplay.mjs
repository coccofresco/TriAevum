// Private config/save copies, fixed guest work, warmup excluded, no frame cap.
import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import assert from 'node:assert/strict';

const opts = Object.fromEntries(process.argv.slice(2).map(arg => {
    const split = arg.indexOf('=');
    return [arg.slice(0, split), arg.slice(split + 1)];
}));
for (const key of ['exe', 'profile', 'output']) {
    if (!opts[key]) throw new Error(`Required: ${key}=path`);
}
if (!opts.state && opts.boot !== 'true') throw new Error('Required: state=path or boot=true');
if (opts.state && opts.boot === 'true') throw new Error('Choose either state=path or boot=true');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const write = (file, data) => fs.writeFileSync(file, JSON.stringify(data, null, 2));
const output = path.resolve(opts.output);
fs.mkdirSync(path.dirname(output), { recursive: true });
fs.mkdirSync(output); // Do not overwrite a previous measurement.
if (opts.saveAt) {
    const disk = fs.statfsSync(output, { bigint: true });
    if (disk.bavail * disk.bsize < 512n * 1024n * 1024n)
        throw new Error('Checkpoint verification needs at least 512 MiB free in the output volume');
}
const base = path.dirname(path.resolve(opts.profile));
const args = read(opts.profile).arguments.map(a => a.replaceAll('${profile_dir}', base));
const get = name => args[args.indexOf(name) + 1];
const set = (name, value) => {
    const index = args.indexOf(name);
    if (index < 0) args.push(name, String(value));
    else args[index + 1] = String(value);
};
const config = read(opts.config ?? get('--config'));
if (opts.overrides) {
    const merge = (target, source) => {
        for (const [key, value] of Object.entries(source)) {
            if (value && typeof value === 'object' && !Array.isArray(value))
                merge(target[key] ??= {}, value);
            else target[key] = value;
        }
    };
    merge(config, read(opts.overrides));
}
config.Graphics.Presentation.VSync = opts.mode === 'play' && opts.vsync === 'true';
if (opts.uncapped === 'true') {
    config.Graphics.FrameRate.Mode = 'Uncapped';
    config.Graphics.Presentation.VSync = false;
}
if (opts.interpolation === 'false') {
    if (opts.uncapped === 'true') throw new Error('Uncapped visual mode enables interpolation; use native throughput instead');
    config.Graphics.FrameRate.Mode = 'Original30';
    args.push('--disable-visual-interpolation');
}
config.CVars ??= {};
config.CVars.gOpenWindows ??= {};
// Seed previous-session state only. F1 starts closed; hotkey tests explicitly open it.
config.CVars.gOpenWindows.Oot3dGraphics = opts.menu === 'true' ? 1 : 0;
write(path.join(output, 'config.json'), config);
fs.copyFileSync(opts.topscreen ?? get('--topscreen-config'), path.join(output, 'topscreen.json'));
fs.cpSync(get('--save-data'), path.join(output, 'savedata'), { recursive: true });
set('--config', path.join(output, 'config.json'));
set('--topscreen-config', path.join(output, 'topscreen.json'));
set('--save-data', path.join(output, 'savedata'));
set('--output', path.join(output, 'runtime.json'));
if (opts.plugin) set('--title-plugin', path.resolve(opts.plugin));
if (opts.resources) set('--resource-root', path.resolve(opts.resources));
if (opts.state) set('--load-state', path.resolve(opts.state));
else {
    const index = args.indexOf('--load-state');
    if (index >= 0) args.splice(index, 2);
}
set('--frames', opts.frames ?? 360);
set('--benchmark-warmup-frames', opts.warmup ?? 60);
set('--max-seconds', opts.seconds ?? Number(opts.timeout ?? 120) - 10);
if (opts.input) set('--input-timeline', path.resolve(opts.input));
if (opts.saveAt) {
    set('--save-state', path.join(output, 'checkpoint.oot3dsav'));
    set('--save-state-frame', opts.saveAt);
}
if (opts.semanticTrace === 'true') set('--pica-semantic-trace', path.join(output, 'pica.jsonl'));
if (opts.profileRuntime === 'true') args.push('--profile-a32-runtime');
if (opts.capture === 'true') {
    set('--screenshot', path.join(output, 'framebuffer.bmp'));
    set('--screenshot-start-frame', opts.captureStart ?? Math.max(1, Number(opts.frames ?? 360) - 1));
    if (opts.captureEvery) {
        args.push('--screenshot-sequence');
        set('--screenshot-interval', opts.captureEvery);
    }
}
if (opts.mode !== 'play') args.push('--throughput-benchmark');
const env = { ...process.env, OOT3D_VULKAN_VALIDATION: opts.validation === 'true' ? '1' : '0',
    VK_LOADER_LAYERS_DISABLE: '~implicit~' };
if (opts.grassDiagnostics === 'true') env.OOT3D_GRASS_DIAGNOSTICS = '1';
if (opts.grassTransitions === 'true') {
    if (opts.capture !== 'true' || !opts.captureEvery) throw new Error('grassTransitions requires capture=true and captureEvery');
    env.OOT3D_SCREENSHOT_GRASS_TRANSITIONS = '1';
}
if (opts.implicitLayers === 'true') delete env.VK_LOADER_LAYERS_DISABLE;
for (const key of Object.keys(env)) {
    if (/^OOT3D_VULKAN_DIAGNOSTICS|^TRIAEVUM_CACAO_DIAGNOSTIC/.test(key)) delete env[key];
}
if (opts.diagnostics === 'true') {
    env.OOT3D_VULKAN_DIAGNOSTICS_PATH = path.join(output, 'vulkan.json');
    env.OOT3D_VULKAN_DIAGNOSTICS_MAX_FRAMES = String(Math.max(240,
        Number(opts.diagnosticFrames ?? (Number(get('--frames')) || 12000))));
}
write(path.join(output, 'invocation.json'), { executable: path.resolve(opts.exe),
    executableSha256: createHash('sha256').update(fs.readFileSync(opts.exe)).digest('hex'),
    titlePluginSha256: args.includes('--title-plugin')
        ? createHash('sha256').update(fs.readFileSync(get('--title-plugin'))).digest('hex') : null,
    diagnosticGuideView: Number(env.TRIAEVUM_GUIDE_DIAGNOSTIC_VIEW ?? 0), args, options: opts });
const stdout = fs.openSync(path.join(output, 'stdout.log'), 'w');
const stderr = fs.openSync(path.join(output, 'stderr.log'), 'w');
const child = spawn(path.resolve(opts.exe), args, {
    cwd: output, env, windowsHide: true, stdio: ['ignore', stdout, stderr],
});
let timedOut = false;
const timer = setTimeout(() => { timedOut = true; child.kill(); }, Number(opts.timeout ?? 120) * 1000);
child.on('error', error => { clearTimeout(timer); throw error; });
child.on('exit', code => {
    clearTimeout(timer);
    fs.closeSync(stdout);
    fs.closeSync(stderr);
    if (code !== 0 || timedOut) {
        console.error(`Runtime exited ${code}, timeout=${timedOut}; inspect ${output}`);
        process.exitCode = 1;
        return;
    }
    const result = read(path.join(output, 'runtime.json'));
    if (opts.saveAt) assert(fs.existsSync(path.join(output, 'checkpoint.oot3dsav')),
        'Runtime exited without writing the requested checkpoint; inspect stderr.log');
    const report = {};
    report.frame_accounting = {
        gameUpdates: result.frame_rate?.game_state_updates_observed,
        presentations: result.presentation_frames,
        interpolationActive: result.frame_rate?.visual_interpolation_active,
        interpolatedLists: result.visual_interpolation?.execution_scheduler?.interpolated_frame_lists,
        repeatedLists: result.visual_interpolation?.execution_scheduler?.repeated_frame_lists,
        directCurrentFrames: result.visual_interpolation?.direct_current_frames_presented,
    };
    if (opts.interpolation === 'false') {
        assert.equal(report.frame_accounting.interpolationActive, false, 'native baseline still interpolates');
        assert.equal(report.frame_accounting.interpolatedLists, 0, 'native baseline executed interpolated lists');
        assert.equal(report.frame_accounting.repeatedLists, 0, 'native baseline executed repeated lists');
        assert(report.frame_accounting.gameUpdates > 0, 'no real game updates measured');
        if (opts.mode !== 'play' && opts.state) {
            assert(Math.abs(report.frame_accounting.gameUpdates - report.frame_accounting.presentations) <= 1,
                'native throughput fixture did not execute one real update per presentation');
        }
    }
    const instrumented = opts.semanticTrace === 'true' || opts.profileRuntime === 'true'
        || opts.validation === 'true' || Boolean(opts.saveAt);
    report.measurement_scope = opts.capture === 'true' ? 'framebuffer_verification_not_benchmark'
        : instrumented ? 'instrumented_verification_not_benchmark'
        : opts.mode === 'play' ? 'wall_clock_playback_not_uncapped_throughput'
        : opts.interpolation === 'false' ? 'uncapped_native_no_interpolation_throughput'
        : 'uncapped_native_updates_with_visual_pipeline';
    function collect(value, prefix = '') {
        for (const [key, entry] of Object.entries(value)) {
            if (['benchmark_window', 'phase_timing'].includes(key)) report[prefix + key] = entry;
            else if (entry && typeof entry === 'object' && !Array.isArray(entry)) collect(entry, `${prefix}${key}.`);
        }
    }
    collect(result);
    write(path.join(output, 'summary.json'), report);
    console.log(JSON.stringify(report, null, 2));
});
