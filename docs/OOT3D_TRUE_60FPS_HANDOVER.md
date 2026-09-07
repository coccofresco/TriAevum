# OOT3D true 60 FPS gameplay handover

## Scopo

Questo e il punto unico da cui riprendere il percorso `Enhanced60` dopo la
sospensione del 27 luglio 2026. Il documento descrive lo stato realmente
implementato e verificato, collega ogni incremento al commit che lo ha
introdotto e indica il codice OOT3D decompilato/disassemblato usato come
autorita.

Non e una dichiarazione di completezza. Il checkpoint funzionale e:

```text
repository   I:\oot3dre_work\oot3d-native-renderer-integration
branch       renderer/oot3d-native-integration
checkpoint   30a1b92fa84eaa9271ffdc0d30bd65a5322a6a02
build tree   I:\oot3dre_work\build-native-renderer-integration-v2
```

Il commit che contiene questo handover e documentale e viene dopo il
checkpoint funzionale.

## Stato in breve

E disponibile una modalita opzionale `enhanced60` che:

- esegue 60 update gameplay reali al secondo;
- usa il rate ABI OOT3D nativo `1` invece di `2`;
- avanza la timeline authored di `0.5` frame per update;
- produce un draw OOT3D nuovo per ogni update;
- disattiva il replay/interpolatore PICA del percorso 30 Hz;
- serializza modalita e clock logico nei savestate;
- espone primitive C++ tipizzate, boundary ARM stretti, telemetria e ledger
  degli eventi;
- conserva a 30 Hz i timer e gli eventi discreti gia classificati;
- campiona a 60 Hz alcuni consumer continui di cutscene, camera, acqua e
  fisica attori.

Il catalogo corrente contiene `107` entry gameplay tipizzate. Il controllo
whole-AOT riporta:

```text
safe_entries=138816
excluded_boundaries=703
```

La feature non e ancora completa o pronta come modalita di gioco generale:
Player, `Actor_UpdateAll`, callback attori, cutscene, camera, ambiente, audio
e RNG conservano compatibility island A32 non classificate.

## Autorita e vincoli

Il comportamento implementato deriva dal binario OOT3D EUR Rev 0:

```text
code.bin  E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin
base      0x00100000
size      4567040 bytes
SHA-256   16A6B0AA4C4784680220A6F780F7F8A73CFB205557AA9F9F0E705179E0613220
```

Regole da mantenere:

1. Il binario, i formati e il control flow OOT3D sono autoritativi.
2. Il codice N64 puo corroborare nomi o architettura soltanto quando serve;
   non determina layout, costanti o timing OOT3D.
3. Non aggiungere gate perche un campo e intero: prima va identificato il suo
   owner completo e classificato come continuo, authored o event-driven.
4. I boundary tipizzati devono preservare memoria, registri, `CPSR/FPSCR`,
   continuazione e fallback non distruttivo.
5. Ogni nuovo boundary deve essere un observable exit whole-AOT.
6. `Native30Interpolated` e una baseline da non modificare.
7. Non riallineare o leggere nuovamente `I:\oot3decomp` finche l'utente non
   lo richiede esplicitamente. Lo snapshot citato nella strategia e storico,
   non una dipendenza live.

## Architettura implementata

### Contratto temporale

`tools/oot3d/native_game_runtime/oot3d_native_frame_rate.*` contiene:

- `GameplayTimingMode::{Native30Interpolated, Native30NoInterpolation,
  Enhanced60}`;
- `NativeFrameRateContract`;
- `NativeGameplayClock`;
- `Oot3dTimeContext`;
- adattamento stretto del rate guest a `GameState+0x110`.

In Enhanced60:

```text
SimulationRateHz       60
NativeUpdateRate       1
LogicalFrameDelta      0.5
VisualInterpolation    false
```

`tools/oot3d/typed_gameplay/oot3d_gameplay_time.h` fornisce
`TimeContext`, crossing dei frame logici e primitive fixed-width che evitano
overflow C++ indefinito conservando il wrap ARM.

### Separazione source/ABI

Il pattern applicato e:

1. semantica source-facing in `tools/oot3d/typed_gameplay/`;
2. adattamento dei soli wire layout/registri in
   `oot3d_typed_gameplay_bridge.*`;
3. codice OOT3D residuo lasciato eseguire prima e dopo il boundary;
4. fallback A32 se qualunque precondizione ABI non coincide;
5. statistiche pubblicate nel report runtime da
   `oot3d_native_a32_window.cpp`.

Le entry non vengono inglobate dal whole-AOT perche
`Oot3dTypedGameplayEntryPoints()` e
`Oot3dTypedGameplayObservableExitPoints()` le rendono confini osservabili.

### Diagnostica

- `oot3d_native_temporal_event_ledger.*` registra transizioni Player con
  simulation tick e mezzo frame logico.
- `oot3d_native_a32_timing_probe.*` espone campi Player e timer.
- `tools/oot3d/frame_rate_audit/` genera inventari temporali candidati.
- i savestate in `oot3d_native_a32_savestate.*` serializzano timing mode,
  simulation tick e previous/current logical frame.

## Copertura Player

L'owner principale resta `Player_Update@0x00250AD0`; non e stato promosso per
intero. Sono chiusi questi sottografi:

| Commit | Campo/owner OOT3D | Boundary o adattatore | Contratto |
| --- | --- | --- | --- |
| `a1ee51751` | floor type `+0x2489/+0x248B` | collisione `0x0032EEB4`, return `0x00251708/0x00251DDC` | identita immediata, eta al crossing |
| `0db18e5fc` | ledge `+0x2278/+0x2279` | writer `0x0032F6C8..0x0032F760`, consumer `0x0023C7AC` | tempo nativo allargato, wire saturato |
| `5bc4e6455` | fishing sum type `+0x2248` | `0x00251374 -> 0x00251384` | recupero negativo al crossing |
| `1ea70ae93` | underwater `+0x2224` | `0x00252038/0x0025205C -> 0x00252064` | reset immediato, incremento authored |
| `5a3b62237` | melee `+0x2228/+0x2229` | `0x002518B4 -> 0x002518D4` | signed step e clear combo nativi |
| `778105888` | melee tip combo `+0x2229` | `0x00313C48 -> 0x00313C74` | byte authored, VFP/collider per tick |
| `ec5c4a169` | damage-run `+0x227C` | `0x00250C30`, call `0x003C45F4` | countdown authored |
| `b1bc2d64d` | invincibility `+0x2488` | `0x00250BE4 -> 0x00250C30` | segno, hold action e collider nativi |
| `47ca2b1ff` | damage flicker `+0x227B` | `0x004BF6D8 -> 0x004BF6F8` | fase authored, draw per tick |
| `a673af142` | cooldown `+0x227A/+0x247E/+0x2482` e fairy `+0x249F` | `0x00250B50 -> 0x00250BB4/0x00250BE4` | tre authored, fairy rate-aware per tick |
| `8ade76c95` | respawn damage `+0x249E` | `0x00250B08 -> 0x00250B1C/0x00250B44` | stato signed, audio one-shot nativo |
| `5b4f01e5a` | random turn `+0x1220/+0x1222` | `0x0025344C/0x00253460`, RNG return `0x00253468` | un solo RNG per frame authored |
| `2f0c54352` | attention `+0x174F` | `0x0025101C -> 0x00251044` | wrap/saturazione authored |

Floor e ledge usano ancora
`NativeA32PlayerTemporalBridge`, una compatibility island esplicita. Gli
altri siti della tabella sono boundary diretti del dispatcher tipizzato.

Il timer body-shock `Player+0x227D` e stato classificato ma non modificato:
il writer usa gia `round(120/nativeUpdateRate)`, quindi il decremento a ogni
simulation tick e corretto.

## Copertura cutscene e camera

| Commit | Owner OOT3D | Boundary/entry | Stato |
| --- | --- | --- | --- |
| `a98b4e2df` | frame owner `0x00321F50` | `0x00322054`, epilogo `0x00321FB8` | frame/comandi solo al crossing; catch-up nativo invariato |
| `4e8702fc4` | cue attori `0x00361F00` | tail `0x00361F7C` | posizione/rotazione campionata a `N+0.5` |
| `88fdd1437` | CAAD/MADS/CMAD `0x0033CB90` | curve native guest | camera cutscene principale a mezzo frame |
| `9a2e417c4` | clock camera EnZl4 | `0x001E0474 -> 0x001E04E0` | sample actor-owned a mezzo frame, advance authored |
| `19debecb3` | `Camera_Update@0x002D84C4` | `0x002D86F0`, `0x002D89D0` | floor miss/interface delay authored |
| `d50ae90e4` | `Camera_CheckWater@0x002D06A0` | `0x002D0A80 -> 0x002D0A94` | timer acqua authored |
| `4659581c7` | consumer acqua | `0x002D8E70/0x002D8FB4/0x002D9120` | ampiezza visuale frazionaria |
| `89c78e66c` | `Quake_Calc@0x004787E8` e callback 1..6 | return `0x004788DC` | countdown/RNG authored, composizione per tick |
| `77ec39690` | `Camera_Normal1@0x00239FD8` | `0x0023A268 -> 0x0023A270` | primo countdown mode-specifico |
| `64ec07b46` | setting 1, 12 siti | tabella nel report dedicato | countdown di 10 callback |
| `8f9ff7e0b` | `Camera_Special5@0x0025B530` | `0x0025B6C8 -> 0x0025B6DC/0x0025B80C` | macchina positive/zero/negative |
| `fd7435d3c` | transizioni `Camera_Normal1` | `0x0023A340/0x0023A45C` | speed/rate countdown authored |

Il dispatcher `Cutscene_ProcessCommands@0x002C5BA0` resta nativo e viene
chiamato solo sui frame logici. I 127 command ID, gli effetti ambiente, audio,
spawn e transizioni non sono stati promossi globalmente. `Camera_Update` e
i callback setting diversi da quelli elencati restano incompleti.

## Copertura attori

### Kernel comune

`caf5909a7` promuove tre boundary di
`Actor_UpdateAll@0x00461344`:

| Campo | Boundary | Contratto |
| --- | ---: | --- |
| `ActorContext+0x02` freeze | `0x00461460` | decremento authored |
| `Actor+0x118` freeze | `0x00461730` | hold/skip intermedio, callback su `1 -> 0` |
| `Actor+0x11A/+0x19C` color/SFX | `0x00461784` | countdown unsigned/signed authored |

Spawn, ordine delle dodici categorie, lifecycle, collision registration,
callback indiretti e delete restano nativi.

### EnKo

- `76f2aaf76`: blink `EnKo+0x2B8/+0x2BA` a
  `0x001B6028`, RNG return `0x001B6074`, continuazione `0x001B6130`.
- `aab93630f`: `EnKo_UpdateTrackingAndAnimation@0x00171ED4` e stato
  classificato come rate-correct per composizione; non e stato aggiunto un
  gate locale. Tracking usa `Math_SmoothStepToS@0x00375A18`, animazione
  `SkelAnime_Update@0x0036B4EC`, eventi
  `Animation_OnFrameImpl@0x003736FC`.

Action callback, dialogo, writer fidget e lifecycle EnKo restano nativi.

### EnKanban

`EnKanban_Update@0x0022C284` e stato diviso senza gateare il callback intero:

| Commit | Stato | Boundary |
| --- | --- | ---: |
| `669f3207a` | fase `+0x1A8` e ripple one-shot | `0x0022C2C4`, `0x0022D1E0` |
| `b08b6c956` | countdown `+0x1B2/+0x1F2/+0x1F5` | `0x0022C31C/0x0022C32C/0x0022C374` |
| `972a1f677` | draw gate `+0x1EE/+0x1F0` | `0x0022C93C` |
| `007ec41c5` | lifetime/state `+0x1AA/+0x1AC` | `0x0022CF00` |
| `30a1b92fa` | oscillatori X/Y | `0x0022CA9C/0x0022CB48` |

L'ultimo commit riscalava esattamente il map discreto
`x += v; v += a` su due mezzi passi. `+0x1CE/+0x1D0` sono stati corretti
semanticamente: sono ampiezze residue di rimbalzo, non timer. Collisione,
RNG, audio, particelle e response one-shot restano nativi e possono attivarsi
su ogni tick.

## Mappa del codice

Entry point per riprendere:

```text
docs/OOT3D_TRUE_60FPS_GAMEPLAY_STRATEGY.md
docs/OOT3D_TRUE_60FPS_HANDOVER.md

tools/oot3d/native_game_runtime/oot3d_native_frame_rate.*
tools/oot3d/native_game_runtime/oot3d_native_a32_window.cpp
tools/oot3d/native_game_runtime/oot3d_typed_gameplay_bridge.*
tools/oot3d/native_game_runtime/oot3d_native_player_temporal_bridge.*
tools/oot3d/native_game_runtime/oot3d_native_a32_timing_probe.*
tools/oot3d/native_game_runtime/oot3d_native_temporal_event_ledger.*
tools/oot3d/native_game_runtime/oot3d_native_a32_savestate.*

tools/oot3d/typed_gameplay/oot3d_gameplay_time.*
tools/oot3d/typed_gameplay/oot3d_gameplay_player.*
tools/oot3d/typed_gameplay/oot3d_gameplay_cutscene.*
tools/oot3d/typed_gameplay/oot3d_gameplay_camera.*
tools/oot3d/typed_gameplay/oot3d_gameplay_camera_animation.*
tools/oot3d/typed_gameplay/oot3d_gameplay_quake.*
tools/oot3d/typed_gameplay/oot3d_gameplay_actor.*
```

Test principali:

```text
tools/oot3d/typed_gameplay/oot3d_gameplay_time_tests.cpp
tools/oot3d/native_game_runtime/oot3d_typed_gameplay_tests.cpp
tools/oot3d/native_game_runtime/oot3d_typed_player_gameplay_tests.cpp
tools/oot3d/native_game_runtime/oot3d_native_frame_rate_tests.cpp
tools/oot3d/native_game_runtime/oot3d_native_player_temporal_bridge_tests.cpp
tools/oot3d/native_game_runtime/oot3d_native_temporal_event_ledger_tests.cpp
tools/oot3d/native_game_runtime/oot3d_native_a32_savestate_tests.cpp
tools/oot3d/native_game_runtime/oot3d_native_mass_aot_tests.cpp
```

## Evidenze decompilate locali

Queste copie nel repository permettono di riprendere senza cercare di nuovo
le funzioni:

```text
Player owner:
tools/oot3d/decomp_support/analysis/direct_conversion_materialized/
  src_overlays_actors_ovl_player_actor_z_player.c_Player_9feb6cb4/
  00250ad0/00250ad0.direct.c

Player collision:
tools/oot3d/decomp_support/analysis/direct_conversion_materialized/
  src_overlays_actors_ovl_player_actor_z_player.c_Player_4258da7c/
  0032eeb4/0032eeb4.direct.c

Cutscene frame owner:
tools/oot3d/decomp_support/analysis/scene_cutscene_context_ghidra_export/
  decompiled/99075_00321f50_FUN_00321f50.c

Cutscene dispatcher and camera assets:
tools/oot3d/decomp_support/analysis/cutscene_camera_blob_helper_ghidra_export/
  decompiled/99003_002c5ba0_Cutscene_ProcessCommands.c
  decompiled/99009_0033cb90_FUN_0033cb90.c
  decompiled/99000_001e042c_EnZl4_Update.c
  disassembly_selected.txt

Camera_Update:
tools/oot3d/decomp_support/analysis/shadow_active_pointer_candidate_ghidra_export/
  decompiled/99017_002d84c4_Camera_Update.c

Actor_UpdateAll:
tools/oot3d/decomp_support/analysis/material_lighting_raw_field_consumer_ghidra_export/
  decompiled/99078_00461344_FUN_00461344.c

EnKo:
tools/oot3d/decomp_support/analysis/enko_tracking_timing_ghidra_export/
tools/oot3d/decomp_support/analysis/shadow_fragop_handle_payload_ghidra_export/
  decompiled/99006_001b5f14_EnKo_Update.c

EnKanban:
tools/oot3d/decomp_support/analysis/enkanban_timing_ghidra_export/
  decompiled/99000_0022c284_EnKanban_Update.c
  disassembly_selected.txt

Camera quake:
tools/oot3d/decomp_support/analysis/camera_quake_state_ghidra_export/
tools/oot3d/decomp_support/analysis/camera_quake_callbacks_ghidra_export/
tools/oot3d/decomp_support/analysis/camera_view_perturbation_ghidra_export/
  decompiled/99001_004787e8_FUN_004787e8.c
```

Report di verifica da leggere prima di modificare i relativi owner:

```text
actor_update_all_timing_verify.md
camera_quake_state_verify.md
camera_normal1_timing_verify.md
camera_mode_countdown_timing_verify.md
camera_special5_timing_verify.md
camera_setting1_discrete_mutator_inventory.md
enko_blink_timing_verify.md
enko_tracking_timing_verify.md
enkanban_phase_timing_verify.md
enkanban_state0_timing_verify.md
enkanban_draw_gate_timing_verify.md
enkanban_piece_lifetime_timing_verify.md
enkanban_oscillator_timing_verify.md
```

Sono tutti sotto `tools/oot3d/decomp_support/analysis/`.

## Cronologia commit

Intervallo completo del lavoro:

```text
5df00ff3ecc8c7f0b53c823ba7cdbc4fb79917ac
..
30a1b92fa84eaa9271ffdc0d30bd65a5322a6a02
```

### Fondazione

| Commit | Contenuto |
| --- | --- |
| `5df00ff3e` | strategia e definizione della feature |
| `2284a6de2` | modalita Enhanced60, clock, scheduler, savestate |
| `87bec97d6` | `TimeContext` e timer source-facing |
| `d19793831` | temporal event ledger |
| `b720571ab` | inventario automatico degli owner temporali |

### Player

| Commit | Contenuto |
| --- | --- |
| `a1ee51751` | floor timer |
| `0db18e5fc` | ledge timer e gate |
| `5bc4e6455` | stato pesca |
| `1ea70ae93` | underwater timer |
| `5a3b62237` | melee action timer |
| `778105888` | cadenza combo/collider melee |
| `ec5c4a169` | damage-run |
| `b1bc2d64d` | invincibility signed |
| `47ca2b1ff` | flicker/fog Player |
| `a673af142` | countdown comuni a cadenza mista |
| `8ade76c95` | respawn damage e audio one-shot |
| `5b4f01e5a` | random turn e RNG |
| `2f0c54352` | attention persistence |

### Cutscene e camera

| Commit | Contenuto |
| --- | --- |
| `a98b4e2df` | frame owner e command cadence |
| `4e8702fc4` | cue attori a mezzo frame |
| `88fdd1437` | camera CAAD/MADS/CMAD a mezzo frame |
| `9a2e417c4` | clock camera actor-owned |
| `19debecb3` | timer diretti `Camera_Update` |
| `d50ae90e4` | countdown camera acqua |
| `4659581c7` | distorsione acqua a mezzo frame |
| `89c78e66c` | lifecycle quake e RNG |
| `77ec39690` | primo countdown `Camera_Normal1` |
| `64ec07b46` | countdown setting gameplay |
| `8f9ff7e0b` | macchina timer `Camera_Special5` |
| `fd7435d3c` | transizioni rate/speed `Camera_Normal1` |

### Attori

| Commit | Contenuto |
| --- | --- |
| `caf5909a7` | kernel temporale `Actor_UpdateAll` |
| `76f2aaf76` | blink/RNG EnKo |
| `aab93630f` | classificazione tracking EnKo |
| `669f3207a` | fase/ripple EnKanban |
| `b08b6c956` | timer stato EnKanban |
| `972a1f677` | draw gate EnKanban |
| `007ec41c5` | lifetime/stato EnKanban |
| `30a1b92fa` | oscillatore fisico EnKanban |

## Verifica del checkpoint

Build usata:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location I:\oot3dre_work\build-native-renderer-integration-v2
cmake --build . --target oot3d_gameplay_time_tests oot3d_typed_gameplay_tests oot3d_native_mass_aot_tests --parallel 12
.\oot3d_gameplay_time_tests.exe
.\oot3d_typed_gameplay_tests.exe
.\oot3d_native_mass_aot_tests.exe
cmake --build . --target oot3d_native_game --parallel 12
```

Risultato al checkpoint `30a1b92fa`:

```text
oot3d_gameplay_time_tests       pass
oot3d_typed_gameplay_tests      pass
oot3d_native_mass_aot_tests     pass
oot3d_native_game build         pass
whole-AOT                       safe_entries=138816
whole-AOT                       excluded_boundaries=703
```

Non e stata eseguita una nuova run interattiva lunga dopo l'ultimo kernel.
Questo e intenzionale: i gate dell'azione erano ABI, semantica temporale,
whole-AOT e integrazione build. La prossima sessione deve ancora svolgere una
prova naturale dell'owner appena promosso se la scena scelta contiene
effettivamente EnKanban.

Artifact differenziali gia presenti:

```text
I:\oot3dre_work\native_game\frame_rate_audit\
  player_floor_timer_native30.json
  player_floor_timer_enhanced60.json
  player_ledge_timer_native30.json
  player_ledge_timer_enhanced60.json
```

`player_idle30_blocks.jsonl` nella stessa directory e un trace storico da
circa 1 GB; non e una dipendenza del runtime.

## Come avviare Enhanced60

Il parametro autoritativo e semantico:

```powershell
.\scripts\oot3d\Invoke-Oot3dNativeGame.ps1 `
  -SkipBuild `
  -Executable I:\oot3dre_work\build-native-renderer-integration-v2\oot3d_native_game.exe `
  -GameplayTiming enhanced60 `
  -Renderer nri
```

Il runtime accetta anche `--gameplay-timing enhanced60`. Il vecchio
`--simulation-rate 60` resta un alias compatibile, ma non va usato nei nuovi
script o documenti.

Per misurare prestazioni:

- build release;
- `PresentationRate free`;
- VSync/frame limit disattivati;
- warm-up escluso;
- audio misurato sia spento sia acceso;
- update, draw e presentazioni riportati separatamente.

Non contare caricamento o shader warm-up come gameplay steady-state.

## Lacune bloccanti residue

In ordine strategico:

1. Promuovere il vero owner Player `0x00250AD0`, eliminando i bridge floor e
   ledge e chiudendo locomotion, ladder, ledge, acqua, interazioni, animazione
   e collisione come un grafo.
2. Promuovere per intero `Actor_UpdateAll` e le famiglie attive nella scena
   target, inclusi lifecycle e callback indiretti.
3. Separare tutti i consumer continui e gli eventi one-shot del dispatcher
   cutscene, soprattutto ambiente, luce, spawn, BGM e SFX.
4. Chiudere `Camera_Update` e i callback setting non coperti.
5. Estendere il ledger a RNG e audio globali; oggi osserva soprattutto le
   transizioni Player.
6. Serializzare qualunque sidecar temporale futuro; non introdurne uno senza
   ABI savestate.
7. Verificare run lunghe e prestazioni NRI con almeno 60 FPS di headroom.
8. Eliminare tutte le compatibility island dal percorso dichiarato completo.

La modalita non va resa default finche questi punti non sono chiusi.

## Primo passo alla ripresa

1. Verificare che `30a1b92fa` sia antenato di `HEAD`.
2. Non toccare i file locali sporchi elencati sotto.
3. Ricostruire ed eseguire i tre test minimi del checkpoint.
4. Scegliere il prossimo owner dalla trace runtime della scena target, non
   dall'ordine dei file Ghidra.
5. Se EnKanban e esercitato, validare in una run naturale i contatori
   `typed_gameplay_enkanban_oscillator_*`; se non lo e, non continuare a
   espandere quell'attore per inerzia.
6. Per il nuovo owner: decompilazione focalizzata, tabella writer/consumer,
   classificazione temporale, boundary naturale, differenziale Native30,
   test Enhanced60 e commit singolo.

Non riprendere applicando un gate all'intero callback o cercando tutti i
`+1/-1` con una sostituzione testuale.

## Stato locale da preservare

Al momento dell'handover esistevano modifiche locali non appartenenti al
lavoro 60 FPS:

```text
M  config/topscreen_ui.example.json
M  config/topscreen_ui.restoration.example.json
?? oot3d_native_game.json
```

Non sono state aggiunte ai commit e non devono essere ripristinate, cancellate
o sovrascritte durante la ripresa.
