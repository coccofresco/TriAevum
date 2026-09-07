# Grass e outline: controlli e stabilita

Data: 2026-09-06. Worktree: `I:/oot3dre_work/triaevum-release`.
Baseline: progetto `914d48ef2`, renderer `7092f721`.
Implementazione renderer: `72562022`.

## Controlli F1

- **Grass / Generation**: densita massima 4096 fili/m2. La densita del preset
  utente non viene aumentata automaticamente. Budget, maschere, distanza e LOD
  restano attivi: 4096 non significa ignorare il budget di geometria.
- **Grass / Appearance / Blade shape**: curvatura 0-2, ricaduta della punta
  0-0.95, irregolarita 0-1, torsione 0-180 gradi. Per curve articolate usare
  piu segmenti; i controlli vicino/lontano sono ora riuniti in
  **Performance / Segments by distance**.
- La forma statica non dipende da vento, camera o tempo. La variazione usa il
  seme stabile del filo e le due facce incrociate condividono la stessa curva.
  Questi controlli aggiornano il record GPU, senza rigenerare la distribuzione.
- **Renderer / Toon / Outline**: spessore 0.5-12 pixel di riferimento a 1080p,
  morbidezza 0-1, colore e opacita. Le sensibilita di profondita e normali sono
  raggruppate in **Edge detection**, non mescolate allo stile del contorno.
- I nuovi parametri sono salvati nei profili JSON e nel preset grass. Le forme
  aggiunte sono nulle per i vecchi profili: non cambiano da sole l'aspetto.

## Cause corrette

### Durata dell'ancoraggio e durata del draw erano confuse

`GrassSceneBridge` conservava la trasformazione statica soltanto se la mesh
veniva ripubblicata entro due frame. `PruneBeforeFrame` eliminava anche tale
informazione. Una ricomparsa, o un intervallo fra presentazioni dello stesso
target, poteva quindi ricostruire l'ancoraggio usando una camera diversa e
richiedere nuovamente la distribuzione asincrona. Nel frattempo non c'era erba.

Ora le sole trasformazioni statiche hanno una cache separata, limitata a 4096
record LRU. Le mesh pubblicate continuano a essere potate e **il rendering
richiede ancora frame, namespace e framebuffer correnti**. Non si disegnano
vecchie mesh per nascondere l'assenza di un draw.

L'ancoraggio e invalidato da rimozione di istanza/room, reset del titolo/stato,
distruzione della geometria e cambio della geometria effettiva. Quest'ultimo
usa `AnchorVersion`, un'impronta di posizioni e indici calcolata solamente alla
decodifica: modifiche UV/materiale non sono scambiate per spostamenti della
superficie, mentre il riuso di un'identita per una nuova mesh non conserva una
vecchia trasformazione.

### Il culling ignorava parte della deformazione

Il volume per filo era una piccola sfera attorno al centro di un filo diritto.
Ora contiene anche curvatura, irregolarita e massima piega dinamica. Il volume
del cluster viene ampliato senza ricostruire gli anchor. L'altezza in unita
world non viene piu ridotta dalla scala della mesh di appoggio; viene incluso
anche l'offset dalla superficie.

## Outline condiviso

Scanout raster e compositing compute usano lo stesso kernel a otto direzioni,
con diagonali a pari raggio. Un solo insieme di campioni alimenta i rilevatori
di profondita e normali, invece di due kernel divergenti. Entrambe le
sensibilita a zero disattivano effettivamente il rilevamento.

Lo spessore viene convertito in pixel interni con `ToonOutlineRenderWidth`:
resta proporzionalmente uguale cambiando risoluzione, render scale o rotazione
del target 3DS. Morbidezza e opacita hanno ruoli distinti. Il marker di
esclusione grass e la condizione di profondita che conserva il contorno degli
oggetti davanti all'erba restano invariati. Ordine dell'effect graph e dominio
HUD non sono modificati.

## Codice

Percorsi relativi a `runtime/three_ds_recomp`:

- `include/fast/oot3d/grass_blade_shape.h`: profilo GLSL e limite di deformazione.
- `src/fast/oot3d/interactive_grass_pass.cpp`: record GPU, forma e culling.
- `src/fast/oot3d/grass_geometry_registry.cpp`: identita geometrica dell'anchor.
- `src/fast/oot3d/grass_scene_bridge.cpp`: ancoraggi persistenti e draw correnti.
- `src/fast/oot3d/grass_world_placement_cache.cpp`: bounds in unita world.
- `src/fast/oot3d/toon_outline_shader.cpp`: unico kernel outline.
- `src/fast/oot3d/pica_scanout_effects.cpp`, `scene_composite.cpp` e
  `scene_composite_pass.cpp`: adapter raster/compute, non copie del kernel.
- `graphics_settings*` e `grass_settings_panel.cpp`: proprietario dei valori,
  validazione, persistenza e controlli F1.

## Verifica ripetibile

La build incrementale usa `triaevum-direct-module-build`, target
`triaevum_public_runtime`, parallelismo 3; il modulo del gioco non e ricompilato.

Runner in `tools/triaevum_release/tests`:

- `run_grass_outline_tests.ps1`: test di fondazione, persistenza, cache,
  ancoraggi e compilazione shader SPIR-V. Riusa oggetti di test invariati.
- `run_f1_settings_smoke.ps1`: widget reali, compresa digitazione di 4096,
  curvatura/ricaduta/irregolarita/torsione e spessore/morbidezza outline.
- `benchmark_gameplay.mjs`: `config=...` e `overrides=...` applicano configurazioni
  esclusivamente a copie private; `captureStart`/`captureEvery` acquisiscono il
  framebuffer. `grassDiagnostics=true` registra la cache e i fili visibili.
- `grass_outline_shape_settings.json`: configurazione di stress, non preset
  utente o nuovo default estetico.

Evidenze private: `I:/oot3dre_work/grass-outline-verification/shape-static` e
`shape-motion`, a partire da `hudtest.oot3dsav`, con impostazioni dell'ultima
run utente e overlay di stress. 24 secondi fermi e 30 in movimento, densita
4096 e sei segmenti, screenshot nativi ogni 180 presentazioni. Dopo la
costruzione iniziale: due distribuzioni riusate, nessuna rigenerazione durante
il movimento; nessuna nuova assenza nei campioni diagnostici. Acquisizioni e
diagnostica rendono queste run **verifiche visive, non benchmark prestazionali**.

Le prove non garantiscono l'assenza di qualsiasi sparizione in ogni scena:
coprono le cause identificate e il percorso Kokiri esercitato. Il primo
caricamento asincrono e l'esclusione oltre la distanza configurata restano
comportamenti intenzionali.

Esito finale: **197 test superati** (inclusa compilazione degli shader) e
**1275 verifiche sui widget F1 reali**. Ripetizione finale dopo il controllo di
identita geometrica in `grass-outline-verification/final-motion`: ancora due
build iniziali, nessuna ricostruzione successiva durante il movimento.

Eseguibile di sviluppo: `I:/oot3dre_work/triaevum-direct-module-build/TriAevum.exe`.
Il pacchetto installato e il suo catalogo di release non sono sovrascritti.

## Estensione: segmenti indipendenti e fog nativa

Commit renderer: `93ba845f`.

### Segmenti per distanza

**F1 / Grass / Performance / Segments by distance** raccoglie ora i quattro
controlli: segmenti vicino (1-12), segmenti lontano (1-vicino), distanza di
inizio riduzione e distanza di raggiungimento del minimo. La distanza e quella
camera-filo in unita world, non una frazione legata alla densita o al draw range.
Fra le due soglie il conteggio intero diminuisce progressivamente; oltre la
seconda rimane al minimo fino al normale culling. Ridurre i segmenti non elimina
il filo e non ricostruisce la sua distribuzione.

JSON: `Grass.Performance.SegmentLodStartDistance` e `SegmentLodEndDistance`;
i conteggi conservano le chiavi esistenti `Appearance.BladeSegments` e
`Performance.FarBladeSegments`. Le configurazioni precedenti senza nuove chiavi
ricavano le distanze da `DrawDistance * LodStartFraction/LodEndFraction`, cosi
non cambiano aspetto da sole. La persistenza copre anche i preset salvati.

### Fog dell'outline

Il frontend pubblica la semantica `NativeFogFactor` insieme ai suoi hook
tipizzati (schema 7). La variante strumentata copia in `FogGuide` il colore e
il fattore gia calcolati dal PICA nativo usando LUT, profondita, w-buffer e flip.
Non esiste una curva fog parallela, una distanza inventata o un colore globale
che sostituisca quello del materiale. Senza fog il valore e neutro.

`FogGuide` e un output ausiliario dichiarato dal grafo, attachment 5. NRI/Vulkan
ne gestisce allocazione, clear neutro, MSAA, binding e lifetime. Il percorso
strumentato ha sei attachment; quello canonico senza estensioni rimane a uno.
Solo draw world che scrivono depth pubblicano la guida: HUD, overlay senza
depth-write e geometria grass non la sovrascrivono. Il formato savestate non
cambia: la guida derivata viene inizializzata neutra e ripopolata dai draw.

Compositing compute e scanout raster applicano la stessa fog al colore scelto
per il contorno, nello spazio colore nativo, prima dell'eventuale conversione
lineare del compositore. Ordine del grafo e separazione HUD restano vincolanti.
Il marker categoriale di esclusione grass ora e campionato senza filtraggio
bilineare; questo non cambia il filtraggio del colore della scena.

Codice aggiuntivo: `grass_visibility.*`, `pica_shader_instrumentation.*`,
`pica_shader_pipeline_cache.cpp`, `pica_attachment_contract.h`,
`display_effect_plan.cpp`, `nri_pica_render_target_*`, `gfx_vulkan_pica.cpp`,
`scene_composite*` e `pica_scanout_effects.cpp`. Nel progetto principale:
`tools/oot3d/native_pica_frontend/oot3d_native_pica_fragment_shader_gen.cpp`.

### Evidenze della nuova tranche

- **288 test renderer passati**, ora includendo grafo effetti, cache pipeline,
  manifest e compilazione SPIR-V delle varianti effettive.
- **1388 verifiche sui widget F1**: conteggi 8/2, soglie 120/650 e persistenza.
- Run Kokiri da `hudtest.oot3dsav`, framebuffer con TAA e MSAA 4x, processi
  terminati normalmente. Evidenze: `native-fog-point-guides` e `native-fog-msaa`
  sotto `I:/oot3dre_work/grass-outline-verification/`.
- Inventario MSAA: 41 pipeline osservate, 20 con output fog; 14 shader effettivi
  copiano il fattore nativo, 6 scrivono la guida neutra. Le distanze 120/650
  risultano caricate dal runtime. Le prove con catture non sono benchmark.
- Run `fog-canonical`: un solo attachment, maschera ausiliaria zero, nessuna
  richiesta di strumentazione, nessuna lettura mancante dal grafo con tutte le
  estensioni disattivate.
- Residuo osservato: con MSAA compaiono ancora contorni su alcuni bordi grass.
  Il resolve multisample media anche il marker categoriale; il campionamento
  puntuale corregge il successivo filtraggio, non questa mescolanza. Occorre
  un resolve delle guide che conservi le classi, non allargare arbitrariamente
  le soglie di esclusione. Non dichiarare risolto questo caso MSAA.
