import fs from 'node:fs';

const [directory] = process.argv.slice(2);
if (!directory) throw new Error('Usage: node analyze_camera_cuts.mjs <capture-directory>');
const lines = fs.readFileSync(`${directory}/stderr.log`, 'utf8').split(/\r?\n/);
const cuts = lines.flatMap(line => {
    const match = /\[visual-camera-cut\] previous=(\d+) current=(\d+) camera_draws=(\d+) cut_draws=(\d+) coverage=([\d.]+)/.exec(line);
    if (!match || !Number(match[4]) || Number(match[4]) * 2 < Number(match[3])) return [];
    return [{ previous: Number(match[1]), current: Number(match[2]),
        world_draws: Number(match[3]), discontinuous_draws: Number(match[4]), coverage: Number(match[5]) }];
});
const photos = fs.readdirSync(directory).filter(name => name.endsWith('.bmp.json')).map(name => ({
    file: name,
    ...JSON.parse(fs.readFileSync(`${directory}/${name}`, 'utf8')),
})).sort((a,b) => a.host_frame - b.host_frame);
const results = cuts.map(cut => {
    const photo = photos.find(p => p.temporal_sample?.current_source === cut.current && p.temporal_sample.history_reset);
    const sample = photo?.temporal_sample;
    const crossing = photos.filter(p => p.temporal_sample?.synthetic &&
        p.temporal_sample.previous_source < cut.current && p.temporal_sample.current_source >= cut.current);
    return { ...cut, capture: photo?.file ?? null,
        authoritative: !!sample && sample.previous_source === cut.current && sample.alpha === 1 && !sample.synthetic,
        grass_blades: photo?.grass?.visible_blades ?? null,
        synthetic_crossings: crossing.map(p => p.file) };
});
const report = {
    scope: 'captured_transition_contracts_not_pixel_parity_or_fps',
    last_captured_source: Math.max(0, ...photos.map(p => p.temporal_sample?.current_source ?? 0)),
    cuts: results,
    uncaptured_cuts: results.filter(r => !r.capture).length,
    invalid_captured_cuts: results.filter(r => r.capture && !r.authoritative).length,
    synthetic_crossings: results.reduce((n,r) => n + r.synthetic_crossings.length, 0),
};
fs.writeFileSync(`${directory}/camera-cut-summary.json`, JSON.stringify(report, null, 2));
console.log(JSON.stringify(report, null, 2));
if (!results.length || report.uncaptured_cuts || report.invalid_captured_cuts || report.synthetic_crossings)
    process.exitCode = 1;
