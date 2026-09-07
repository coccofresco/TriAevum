# OOT3D frame-rate audit

`build_temporal_inventory.py` turns the committed OOT3D decompilation model
into a versioned list of temporal mutation candidates grouped by owner graph.
It never edits the decompilation checkout.

Example:

```powershell
python tools/oot3d/frame_rate_audit/build_temporal_inventory.py `
  --decomp-root I:\oot3decomp `
  --owner player `
  --output-dir native_game\frame_rate_audit\player
```

An optional `--runtime-trace <a32.jsonl>` adds execution hit counts. Automatic
classifications are triage hints only. Confirmed site domains belong in
`temporal_inventory_config.json` under `site_overrides`; source ports consume
only confirmed classifications.
