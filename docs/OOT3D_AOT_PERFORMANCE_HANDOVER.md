# OOT3D AOT Performance Handover

> **Historical snapshot (2026-07-20).** The counts, interpreter comparisons,
> incremental shard layout and Windows baselines below describe the previous
> runtime. The current zero-residual product has 12,419 generated functions,
> three host boundaries and affinity sharding. Use
> [OOT3D cross-platform performance status](OOT3D_CROSS_PLATFORM_PERFORMANCE_STATUS.md)
> for the current contract, Linux evidence and required Windows rebuild/A-B
> procedure. Do not reuse the historical prebuilt `.lib` with the current ABI.

## Scopo

Questo documento consegna lo stato operativo del percorso AOT usato dal runtime
nativo OOT3D e le informazioni necessarie per migliorarne le prestazioni senza
perdere correttezza, completezza grafica, audio o riproducibilita.

Il target corrente e il gioco nativo con backend Vulkan, audio attivo,
presentazione 16:9 e interpolazione visiva attiva. Una modifica e utile solo se
riduce il tempo reale del prodotto completo. Ridurre un contatore interno o
accelerare una micro-fixture non e un risultato sufficiente.

Snapshot di riferimento:

- repository: `I:\oot3dre-vulkan`;
- branch: `oot3d-vulkan`;
- commit AOT principale: `8ee54835c` (`Validate and accelerate whole-program AOT`);
- commit build incrementale: `986531094` (`Regenerate whole AOT shards incrementally`);
- commit build bounded: `c1e0bde7a` (`Make whole AOT rebuild explicit and bounded`);
- data handover: 20 luglio 2026;
- backend di produzione per questo lavoro: Vulkan;
- scena benchmark: Foresta Kokiri, checkpoint dopo il caricamento della scena.

Non riallineare o leggere `I:\Zelda3drecomp` finche l'utente non lo chiede
esplicitamente. Il lavoro prestazionale descritto qui e autosufficiente e usa
soltanto repository, artifact e input gia disponibili.

## Risultato Attuale

Il percorso whole-AOT corrente contiene:

| Voce | Valore |
| --- | ---: |
| Funzioni selezionate | 12.196 |
| Istruzioni A32 emesse | 909.577 |
| Shard C++ | 256 |
| Strategia shard | `incremental` |
| Confini esterni conservati | 139 |
| Conflitti di ownership risolti | 357 |
| Sostituzioni host manuali | 9 |
| Uscite whole-AOT non supportate nella run Kokiri | 0 |

La selezione tracciata e
`tools/oot3d/native_a32_runtime/whole_aot_functions.json`. Usa modalita
`all_lowerable`: il profilo ordina e documenta il lavoro, ma non limita la
selezione alle sole funzioni osservate. L'ultimo pass di selezione aveva 39
rifiuti di lowering prima dell'emissione; per questo il manifest C++ finale non
mostra reject: le unita non emettibili sono gia state escluse.

Il profilo sorgente stabile e
`tools/oot3d/native_a32_runtime/profiles/navi_kokiri_main_forest_source_coverage.json`:

- 310.307 campioni totali;
- 303.449 campioni selezionati dal profilo;
- 277.444 campioni mappati su 838 funzioni;
- 26.005 campioni non mappati;
- SHA-256 `4D685FE2DA4B8484D491D7235CF05F16151719DBA1C9BD0494496B055D4E9089`.

Il report del selettore indicava circa 85,428% di copertura effettiva dopo
ownership, lowerability e risoluzione delle sovrapposizioni. Non confondere
questo valore con il rapporto grezzo `mapped_samples/profile_total_samples`.

## Baseline Prestazionali

Tutte le righe seguenti usano 480 refresh, lo stesso checkpoint Kokiri, Vulkan,
audio e rendering completi. `FPS` e `480 / host_loop_seconds`.

| Configurazione | Host loop | FPS | Guest | PCM FNV-1a |
| --- | ---: | ---: | ---: | --- |
| Interprete puro | 13,53 s | 35,49 | 11,20 s | `15053174716799755000` |
| Whole-AOT puro, senza le 9 funzioni manuali | 10,01 s | 47,95 | 7,00 s | `15053174716799755000` |
| Produzione, prima delle ultime promozioni | 8,53 s | 56,30 | 6,02 s | `15053174716799755000` |
| Produzione, hot closure | 7,90 s | 60,75 | 5,51 s | `15053174716799755000` |
| Produzione, copertura completa | 7,82 s | 61,41 | 5,42 s | `15053174716799755000` |
| Produzione, fast write trace guard | 7,7067 s | 62,28 | 5,3193 s | `15053174716799755000` |

File autoritativi delle ultime righe:

```text
I:\oot3dre_work\native_game\perf\forest_pure_interpreter_480_v18.json
I:\oot3dre_work\native_game\perf\forest_pure_whole_aot_complete_480_v18.json
I:\oot3dre_work\native_game\perf\forest_production_aot_480_v14.json
I:\oot3dre_work\native_game\perf\forest_production_aot_hot_480_v16.json
I:\oot3dre_work\native_game\perf\forest_production_aot_complete_480_v17.json
I:\oot3dre_work\native_game\perf\forest_production_aot_fastwrite_480_v19.json
```

Il benchmark migliore supera 60 FPS, ma non ha ancora il margine abbondante
richiesto. Dopo rebuild AOT lunghe la stessa macchina ha prodotto misure fra 45
e 59,6 FPS mentre guest, PICA e presentazione rallentavano insieme; si trattava
di carico di sistema o throttling, non di una variazione del lavoro guest.
Attendere il raffreddamento e usare coppie alternate prima di accettare o
rifiutare una patch.

Non usare una run con `-ProfileA32Runtime`, `-ProfileA32Blocks`, trace o
diagnostica estesa come misura FPS di produzione. Il profiling serve a
localizzare il costo e introduce lavoro aggiuntivo; la variabilita di sistema
puo inoltre renderne casualmente il wall time migliore di una run pulita.

## Input Riproducibile

Checkpoint:

```text
I:\oot3dre_work\native_game\checkpoints\navi_kokiri_main_forest.oot3dsav
size:   75.871.428 byte
SHA256: 888E3F2D599BA42A792EFAECBC9785D43EAD00BFE2CFE732DFC6444828B9DD1B
```

Immagine originale A32:

```text
E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin
size:   4.567.040 byte
SHA256: 16A6B0AA4C4784680220A6F780F7F8A73CFB205557AA9F9F0E705179E0613220
```

Il catalogo generale dei tool e degli input originali e
`tools/oot3d/operational_inputs.json`. Verificarlo con:

```powershell
.\scripts\oot3d\Test-Oot3dOperationalInputs.ps1
```

Non cercare copie alternative di `code.bin` finche questo controllo passa.

## Architettura Runtime Corrente

### Livelli Di Esecuzione

Esistono tre percorsi distinti:

1. **Whole-function/whole-program AOT C++**: percorso principale di produzione.
2. **True-AOT per piccole regioni**: percorso storico conservato come fixture e
   fallback diagnostico; non e il backend da espandere per la produzione.
3. **Interprete packed A32**: oracolo differenziale e fallback diagnostico.

All'avvio, `oot3d_native_a32_window.cpp` unisce gli entry point whole-AOT,
manuali e true-AOT e marca i blocchi corrispondenti come `native_candidate`.
Nel dispatcher A32:

1. viene cercato il blocco packed;
2. se e `native_candidate`, `ExecuteNativeCandidate` prova prima
   `ExecuteOot3dCompiledFunction`;
3. `ExecuteOot3dCompiledFunction` prova una delle nove sostituzioni manuali, se
   abilitate;
4. in caso contrario chiama `ExecuteOot3dWholeAotFunction`;
5. se il whole-AOT non gestisce l'entry, viene provato il vecchio true-AOT;
6. infine il dispatcher esegue il blocco packed/interprete.

Le chiamate dirette fra funzioni whole-AOT sono normali chiamate C++ e
condividono `Oot3dWholeAotContext`. SVC, fault, target indiretti non risolti,
budget esaurito e confini esterni tornano al piccolo dispatcher host.

### Stato Guest

`Oot3dWholeAotFrame` contiene oggi un riferimento canonico a
`a32::GuestState`; non mantiene una seconda copia di registri e VFP. Questo ha
eliminato copy/commit completi a ogni funzione e rende lo stato visibile alle
call annidate senza sincronizzazioni manuali.

Lo stato comprende:

- `r0..r15`;
- CPSR e FPSCR;
- 32 registri VFP;
- thread pointer;
- stato exclusive (indirizzo, token, size, validita).

Gli accessi RAM ordinari usano `NativeA32Memory::ReadFast<T>` e
`WriteFast<T>`, non l'interfaccia virtuale `MemoryBus`. Una page table sparsa a
due livelli risolve pagine completamente mappate; boundary, permessi e mapping
parziali ricadono sul percorso controllato. MMIO, atomiche ed exclusive restano
helper espliciti.

### Budget E Scheduling

`NativeA32Process::Run` chiama `a32::Dispatch` con un budget di basic block. Il
whole-AOT riceve il budget residuo, decrementa `BlocksRemaining` a ogni ingresso
di basic block e restituisce `blocksConsumed`. Il dispatcher aggiorna il proprio
contatore e conserva quindi la stessa granularita di scheduling dell'interprete.

Questo contratto e semanticamente importante, ma oggi e anche il principale
candidato prestazionale: il profilo Kokiri conta 53.534.709 ingressi di basic
block in 480 refresh.

### Call Esterne E Funzioni Manuali

Le nove sostituzioni manuali sono definite in
`tools/oot3d/native_game_runtime/oot3d_native_compiled_functions.cpp`:

| Entry | Funzione |
| --- | --- |
| `0x00307BD8` | Pica command register-range writer |
| `0x00307C94` | upload vertex float uniforms |
| `0x00313D6C` | framebuffer access material state |
| `0x0036C174` | matrix 3x4 multiply |
| `0x00371738` | runtime memcpy |
| `0x00372224` | matrix copy-if-distinct |
| `0x00466E2C` | mesh command packet submit |
| `0x004A022C` | four-channel audio delay |
| `0x004A0338` | stereo reverb |

Sono abilitate in produzione e spiegano la differenza fra 47,95 FPS del puro
whole-AOT e 62,28 FPS del prodotto corrente. Per validare il lowering generico
devono essere disattivate; per misurare il prodotto devono essere attive.

I 139 `external_functions` della selezione non equivalgono tutti a funzioni
manuali: sono confini ARM mantenuti per callee non selezionati o resume target.
Con `-DisableManualCompiledFunctions`, una call esterna prosegue nel guest senza
inventare un fallback host.

## File Da Conoscere

### Front-End E Selezione

| File | Responsabilita |
| --- | --- |
| `tools/oot3d/native_a32_runtime/whole_aot_program.py` | IR di programma, funzioni, CFG e call graph. |
| `tools/oot3d/native_a32_runtime/generate_whole_aot.py` | genera `aot_program.json`. |
| `tools/oot3d/native_a32_runtime/select_profile_whole_aot.py` | selezione, closure, inclusioni, esclusioni e conflitti. |
| `tools/oot3d/native_a32_runtime/whole_aot_functions.json` | selezione di produzione tracciata. |
| `tools/oot3d/native_a32_runtime/profiles/navi_kokiri_main_forest_source_coverage.json` | profilo stabile Kokiri. |

### Emitter E Semantica

| File | Responsabilita |
| --- | --- |
| `tools/oot3d/native_a32_runtime/whole_aot_cpp.py` | lowering A32 -> C++, CFG, call e shard. |
| `tools/oot3d/native_a32_runtime/generate_whole_aot_cpp.py` | CLI dell'emitter. |
| `tools/oot3d/native_game_runtime/oot3d_native_whole_aot_runtime.h` | frame, context, flow, flag, shift e helper. |
| `tools/oot3d/native_a32_runtime/oot3d_native_a32_vfp_ops.cpp` | primitive VFP esatte condivise. |
| `tools/oot3d/native_a32_runtime/upstream/recomp/a32_runtime.*` | dispatcher/interprete pinned e hook native candidate. |

### Integrazione Runtime

| File | Responsabilita |
| --- | --- |
| `tools/oot3d/native_game_runtime/oot3d_native_compiled_functions.*` | manual functions, bridge whole-AOT e statistiche. |
| `tools/oot3d/native_game_runtime/oot3d_native_a32_memory.*` | regioni guest, fast page table, atomiche e fingerprint. |
| `tools/oot3d/native_game_runtime/oot3d_native_a32_process.*` | thread, scheduler, SVC e dispatch. |
| `tools/oot3d/native_game_runtime/oot3d_native_a32_window.cpp` | configurazione, profiling, trace e report JSON. |
| `scripts/oot3d/Invoke-Oot3dNativeGame.ps1` | launcher riproducibile. |

### Test E Documentazione

| File | Responsabilita |
| --- | --- |
| `tools/oot3d/native_a32_runtime/test_whole_aot_cpp.py` | test emitter, lowering e sharding incrementale. |
| `tools/oot3d/native_a32_runtime/compare_block_traces.py` | prima divergenza semantica fra due trace. |
| `tools/oot3d/native_game_runtime/oot3d_native_a32_process_tests.cpp` | memoria/processo/callback. |
| `docs/OOT3D_WHOLE_PROGRAM_AOT_STRATEGY.md` | strategia architetturale. |
| `docs/OOT3D_TRUE_AOT_CONTRACT.md` | contratto e distinzione dal vecchio true-AOT. |
| `docs/OOT3D_OPERATIONAL_INPUTS.md` | percorsi operativi. |

## Artifact Generati

`build-codex` e una junction:

```text
I:\oot3dre-vulkan\build-codex
  -> C:\Users\xander\AppData\Local\oot3dre-build\build-vulkan
```

Artifact principali:

| Artifact | Dimensione corrente | Note |
| --- | ---: | --- |
| `build-codex/oot3d_whole_aot/aot_program.json` | 183.734.245 byte | IR completo, non versionato. |
| `build-codex/oot3d_whole_aot_cpp/whole_aot_cpp_manifest.json` | 3.223.756 byte | manifest emitter. |
| `build-codex/oot3d_whole_aot_cpp/oot3d_whole_aot_shard_*.cpp` | 256 file | C++ generato. |
| `build-codex/oot3d_native_whole_aot.lib` | circa 304 MB | archivio importato dalle build normali. |
| `build-codex/oot3d_native_game.exe` | circa 140 MB | applicazione corrente. |

Hash utili:

```text
aot_program.json SHA256:
7539807531265FE10230EBC0970EB5F37F2865511D81FBAA8BF45067C21B5C3E

whole_aot_functions.json SHA256:
322E058D34CB1156F77B7C7E91F38706A0E529B32E3A413EAD1FB37E584B5161
```

Gli artifact generati non sono sorgenti autoritativi. Le modifiche devono
finire in emitter, runtime o selezione tracciata.

## Build Corretta

### Toolchain E Cache Locale

La build validata usa questa configurazione, letta da
`build-codex/CMakeCache.txt`:

| Voce | Valore |
| --- | --- |
| Generator | `Ninja` |
| Configurazione | `RelWithDebInfo` |
| Compilatore | MSVC `14.44.35207`, `Hostx64/x64/cl.exe` |
| Flag configurazione | `/O2 /Ob1 /DNDEBUG` |
| Ninja | `C:\Users\xander\.local\bin\ninja.exe` |
| `OOT3D_REBUILD_WHOLE_AOT` | `OFF` |
| `OOT3D_WHOLE_AOT_MAX_PARALLEL_COMPILES` locale | `8` |
| `OOT3D_WHOLE_AOT_SHARD_COUNT` | `256` |
| Archive importato | `build-codex/oot3d_native_whole_aot.lib` |

Il valore locale 8 limita soltanto una rebuild esplicita con l'opzione `ON`;
non avvia compilazioni quando l'opzione e `OFF`. Il default conservativo nel
sorgente CMake resta 2. Non cancellare la cache per cambiare un solo parametro:
la junction e l'archive precompilato sono parte del percorso rapido validato.

### Build Ordinaria

La build quotidiana deve lasciare `OOT3D_REBUILD_WHOLE_AOT=OFF`. In questa
modalita CMake importa `build-codex/oot3d_native_whole_aot.lib` e non compila i
256 shard.

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && ninja -C build-codex -j 2 oot3d_native_game'
```

Ultima verifica: `ninja: no work to do` in circa 9,9 secondi, incluso il check
dei glob CMake. Un relink ordinario e anch'esso nell'ordine di pochi secondi.

### Rigenerazione Incrementale

Con `OOT3D_REBUILD_WHOLE_AOT=ON`, CMake espone
`oot3d_generate_whole_aot_cpp`. Il target dipende da emitter, selezione e
`aot_program.json`, usa 256 shard e strategia `incremental`.

```powershell
ninja -C build-codex oot3d_generate_whole_aot_cpp
```

Una seconda invocazione senza modifiche deve dire `no work to do`. Se cambia
soltanto una parte dell'output, `whole_aot_cpp.py` conserva l'ownership degli
shard esistenti e riscrive solo quelli diversi.

### Rebuild AOT Esplicita

Usare il CMake incluso in Visual Studio:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' -S . -B build-codex -DOOT3D_REBUILD_WHOLE_AOT=ON -DOOT3D_WHOLE_AOT_MAX_PARALLEL_COMPILES=4
```

Poi:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && ninja -C build-codex -j 4 oot3d_native_game'
```

Su questa macchina:

- 4 compilazioni concorrenti: circa 17,5 minuti, carico molto conservativo;
- 8 compilazioni concorrenti: circa 15,2 minuti, circa 1,5-2,5 GB nei processi
  `cl`, macchina stabile ma interattivita ridotta;
- il default versionato resta 2;
- non aumentare indiscriminatamente `/MP` e job Ninja: il C++ enorme scala poco
  per contesa CPU/cache.

Non interrompere la rebuild. Se una shell va in timeout, verificare prima
`Get-Process ninja,cl,link,cmake`; avviare un secondo Ninja mentre il primo
possiede gli `.obj` produce `C1083 Permission denied`.

Al termine ripristinare sempre la modalita normale:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' -S . -B build-codex -DOOT3D_REBUILD_WHOLE_AOT=OFF
```

### Rigenerazione Manuale Del C++

Normalmente usare il target CMake. Per diagnosi:

```powershell
python tools/oot3d/native_a32_runtime/generate_whole_aot_cpp.py --program build-codex/oot3d_whole_aot/aot_program.json --selection tools/oot3d/native_a32_runtime/whole_aot_functions.json --output build-codex/oot3d_whole_aot_cpp --shards 256 --shard-strategy incremental
```

Non usare il default CLI di 32 shard sul directory di produzione.

### Rigenerazione Di IR E Selezione

Sono operazioni rare e intenzionali:

```powershell
python tools/oot3d/native_a32_runtime/generate_whole_aot.py --output build-codex/oot3d_whole_aot/aot_program.json
python tools/oot3d/native_a32_runtime/select_profile_whole_aot.py --program build-codex/oot3d_whole_aot/aot_program.json --profile tools/oot3d/native_a32_runtime/profiles/navi_kokiri_main_forest_source_coverage.json --selection tools/oot3d/native_a32_runtime/whole_aot_functions.json --all-lowerable --output tools/oot3d/native_a32_runtime/whole_aot_functions.json
```

Prima di sovrascrivere la selezione tracciata, produrre un candidato temporaneo
e confrontare funzioni, external boundary, conflict ed esclusioni. Non
committare i file `_whole_aot_functions_*_candidate.json`.

## Comandi Di Run

### Produzione

```powershell
.\scripts\oot3d\Invoke-Oot3dNativeGame.ps1 -SkipBuild -Renderer vulkan -Frames 480 -LoadState 'I:\oot3dre_work\native_game\checkpoints\navi_kokiri_main_forest.oot3dsav' -Output 'I:\oot3dre_work\native_game\perf\candidate_production_480.json'
```

Non disattivare audio o interpolazione per il numero di prodotto.

### Interprete Puro

```powershell
.\scripts\oot3d\Invoke-Oot3dNativeGame.ps1 -SkipBuild -Renderer vulkan -Frames 480 -DisableCompiledFunctions -LoadState 'I:\oot3dre_work\native_game\checkpoints\navi_kokiri_main_forest.oot3dsav' -Output 'I:\oot3dre_work\native_game\perf\candidate_interpreter_480.json'
```

`-DisableCompiledFunctions` disabilita manual functions, whole-AOT e true-AOT.

### Whole-AOT Puro

```powershell
.\scripts\oot3d\Invoke-Oot3dNativeGame.ps1 -SkipBuild -Renderer vulkan -Frames 480 -DisableManualCompiledFunctions -DisableTrueAotBlocks -LoadState 'I:\oot3dre_work\native_game\checkpoints\navi_kokiri_main_forest.oot3dsav' -Output 'I:\oot3dre_work\native_game\perf\candidate_whole_aot_480.json'
```

Questa e la configurazione corretta per giudicare il lowering generico.

### Profilo

```powershell
.\scripts\oot3d\Invoke-Oot3dNativeGame.ps1 -SkipBuild -Renderer vulkan -Frames 480 -ProfileA32Runtime -ProfileA32Blocks -LoadState 'I:\oot3dre_work\native_game\checkpoints\navi_kokiri_main_forest.oot3dsav' -Output 'I:\oot3dre_work\native_game\perf\candidate_profile_480.json'
```

Il profiler runtime campiona 1 chiamata su 64. Il block profiler campiona gli
ingressi e riporta `estimated_entries`. Non sommare i tempi stimati a quelli di
fase: sono viste diverse dello stesso lavoro.

## Verifica Semantica

Il confronto forte usa trace JSONL a ogni ingresso di basic block. Registra:

- host frame e guest PC esplicito;
- `r0..r14` (non `r15`, perche il PC esplicito e canonico nel controllo host);
- CPSR, FPSCR e tutti i registri VFP;
- TLS ed exclusive state;
- generazione e fingerprint delle scritture memoria;
- diagnostica di eventuali mismatch `ReadFast`.

Generare trace corte, perche tre frame occupano circa 177 MB ciascuna.

Interprete:

```powershell
.\scripts\oot3d\Invoke-Oot3dNativeGame.ps1 -SkipBuild -Renderer vulkan -Frames 3 -DisableCompiledFunctions -A32BlockTrace 'I:\oot3dre_work\native_game\perf\candidate_interpreter.trace.jsonl' -LoadState 'I:\oot3dre_work\native_game\checkpoints\navi_kokiri_main_forest.oot3dsav' -Output 'I:\oot3dre_work\native_game\perf\candidate_interpreter_trace.json'
```

Whole-AOT puro:

```powershell
.\scripts\oot3d\Invoke-Oot3dNativeGame.ps1 -SkipBuild -Renderer vulkan -Frames 3 -DisableManualCompiledFunctions -DisableTrueAotBlocks -A32BlockTrace 'I:\oot3dre_work\native_game\perf\candidate_whole_aot.trace.jsonl' -LoadState 'I:\oot3dre_work\native_game\checkpoints\navi_kokiri_main_forest.oot3dsav' -Output 'I:\oot3dre_work\native_game\perf\candidate_whole_aot_trace.json'
```

Confronto:

```powershell
python tools/oot3d/native_a32_runtime/compare_block_traces.py 'I:\oot3dre_work\native_game\perf\candidate_interpreter.trace.jsonl' 'I:\oot3dre_work\native_game\perf\candidate_whole_aot.trace.jsonl'
```

Baseline validata: `equivalent traces: 257159 records`.

Il fingerprint finale della produzione non coincide necessariamente con quello
dell'interprete puro: le sostituzioni manuali possono effettuare un numero
diverso di transazioni host pur producendo lo stesso stato guest. Confrontare
fingerprint fra configurazioni omogenee; per equivalenza A32 usare manual
functions disattivate e il trace completo.

Test minimi prima di ogni commit:

```powershell
git diff --check
python -m py_compile tools/oot3d/native_a32_runtime/generate_whole_aot_cpp.py tools/oot3d/native_a32_runtime/select_profile_whole_aot.py tools/oot3d/native_a32_runtime/whole_aot_cpp.py tools/oot3d/native_a32_runtime/compare_block_traces.py
python -m pytest tools/oot3d/native_a32_runtime/test_whole_aot_cpp.py -q
.\build-codex\oot3d_native_a32_process_tests.exe
```

Baseline test: 22 test Python e `oot3d_native_a32_process_tests: ok`.

## Profilo Residuo

Il profilo stabile da 480 refresh riporta:

| Metrica | Valore |
| --- | ---: |
| Basic block entries | 53.534.709 |
| Campioni block profiler | 836.480 |
| Dispatch calls | 29.819 |
| Native candidate calls | 1.731.354 |
| Tempo candidate stimato nella run profilata | 3,3725 s |
| SVC calls | 29.819 |
| SVC time | 0,6507 s |
| Draw | 37.817 |

Hot block principali:

| PC | Entry stimate | Owner/ruolo |
| --- | ---: | --- |
| `0x00466E7C` | 3.739.648 | loop interno `MeshCommandPacket_Submit` |
| `0x00307C44` | 1.160.640 | residue CFG PICA command writer |
| `0x004A02C8` | 1.048.832 | loop audio delay |
| `0x004A0298` | 1.048.512 | loop audio delay |
| `0x004A02BC` | 785.536 | loop audio delay |
| `0x0047AF3C` | 545.344 | direct target runtime |
| `0x0047AFA0` | 539.520 | direct target runtime |
| `0x004A03A0` | 534.976 | loop stereo reverb |
| `0x004A040C` | 532.352 | loop stereo reverb |
| `0x004A04B4` | 527.488 | loop stereo reverb |
| `0x003084DC` | 344.384 | `PicaMaterialState_EmitLighting` |
| `0x00308920` | 283.200 | `NativeCurve_SampleFloat` |
| `0x00307B8C` | 273.408 | `PicaTev_ConvertSource` |

Il percorso guest resta dominante: circa 5,3 secondi su 7,7 nella miglior run.
PICA submit e visual presentation valgono circa 1,0 e 1,1 secondi. Ottimizzare
audio host o renderer prima di ridurre il costo AOT non affronta il collo di
bottiglia principale di questo checkpoint.

## Esperimenti Gia Scartati

### Cache Della Pagina Guest Corrente

E stata provata una cache separata dell'ultima pagina read/write dentro
`NativeA32Memory::ReadFast/WriteFast`. Era semanticamente equivalente, ma ha
prodotto 61,30 e 61,47 FPS contro 62,28. E stata completamente rimossa.

Artifact:

```text
I:\oot3dre_work\native_game\perf\forest_production_aot_pagecache_480_v20a.json
I:\oot3dre_work\native_game\perf\forest_production_aot_pagecache_480_v20b.json
```

Conclusione: il doppio lookup della page table non e oggi il primo problema.
Non riproporre una cache equivalente senza un profilo hardware che dimostri il
contrario.

### Eliminazione Di `BlocksConsumed`

E stato provato un solo countdown, ricavando i blocchi consumati come budget
iniziale meno residuo. Trace equivalente, ma 58,44 e 60,75 FPS. MSVC ottimizza
meglio l'attuale doppio contatore. La modifica e stata rimossa.

Artifact:

```text
I:\oot3dre_work\native_game\perf\forest_production_aot_blockbudget_480_v21a.json
I:\oot3dre_work\native_game\perf\forest_production_aot_blockbudget_480_v21b.json
```

Conclusione: cambiare la formula del contatore non basta. Serve ridurre il
numero di confini osservati o promuovere lo stato a locali/SSA.

### Trace Delle Scritture Sempre Chiamato

`WriteFast` chiamava il recorder di fingerprint anche in produzione. Il guard
`if (mTraceWriteFingerprint) [[unlikely]]` e ora inline prima della call. Questo
ha portato il miglior benchmark da 61,41 a 62,28 FPS. Non rimettere una call
incondizionata nel percorso write.

### Aumentare Soltanto La Copertura

La selezione e gia `all_lowerable`, gli unsupported exit sono zero e il puro
whole-AOT migliora il guest del 37,5% rispetto all'interprete. Ulteriori entry
non risolvono il residuo: il costo e dentro funzioni gia tradotte.

## Prossimi Esperimenti Raccomandati

La ricerca comparativa con LLBT, QEMU, FEX, Box64, N64Recomp, rev.ng e le
toolchain host e raccolta in
`docs/OOT3D_AOT_EXTERNAL_OPTIMIZATION_RESEARCH.md`. La conclusione principale
e implementare architectural-state promotion prima di affidarsi a un cambio di
compilatore o al solo PGO/LTO.

### Stato Architectural-State IR (2026-07-20)

`tools/oot3d/native_a32_runtime/whole_aot_optimization_ir.py` introduce il
primo livello backend-independent della pipeline raccomandata. Per ogni
istruzione e blocco descrive use/def GPR, use/def separati N/Z/C/V, liveness
dei flag, dirty set, uscite osservabili e regioni SCC/loop. Le istruzioni non
riconosciute sono barrier conservative; sul corpus whole-AOT corrente il loro
conteggio deve restare zero.

L'analisi completa e riproducibile con:

```powershell
python tools\oot3d\native_a32_runtime\analyze_whole_aot_optimization_ir.py `
  --program build-codex\oot3d_whole_aot\aot_program.json `
  --selection tools\oot3d\native_a32_runtime\whole_aot_functions.json `
  --code E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin `
  --output I:\oot3dre_work\whole_aot_optimization\analysis.json
```

Baseline corrente: 12.196 funzioni, 159.651 blocchi, 909.577 istruzioni,
3.590 regioni cicliche e zero barrier conservative. Su 69.393 producer di
N/Z/C/V, 21.853 hanno solo un sottoinsieme di flag vivo. Questa IR e il
contratto da consumare nel lowering: non va reintrodotta un'analisi testuale
del C++ generato.

Il pilot LLVM riproducibile usa
`tools/oot3d/native_a32_runtime/run_llvm_aot_pilot.py` e un compile database
generato dalla build isolata. Sullo shard che contiene
`MeshCommandPacket_Submit_00466E2C`, Clang O2 conserva 35 load e 25 store
riconducibili a `GuestState`; ripassare lo stesso modulo in `opt -O2` produce
37 load e 25 store. Questo chiude l'ambiguita del punto 4: LLVM sul C++
attuale non recupera da solo lo stato SSA. Il pilot successivo deve ricevere
la IR promossa, non lo stesso sorgente materializzato.

Sul branch `oot3d-aot-state-promotion` il frontend consuma ora la liveness e
passa un unico `Oot3dAotArchitecturalState` attraverso call dirette e
indirette. I normali edge CFG non materializzano `GuestState`; callback ed
external call eseguono flush/reload, e l'uscita pubblica esegue il flush
finale. N/Z/C/V sono scalari e ogni producer riceve la mask viva calcolata
dalla IR.

Sul medesimo `MeshCommandPacket_Submit_00466E2C`, il pilot promosso porta gli
accessi diretti `GuestState` da 35 load/25 store a 0/0. L'assembly dello shard
scende da 3.315.037 a 2.874.610 byte. Rimangono 32 load/33 store sul contenitore
promosso: sono il target del successivo split fast/slow per callback e safe
point, non un motivo per tornare alla materializzazione per blocco.

Il runtime controlla ora `BlockEntryPcs` prima del flush. Questo e rilevante
nel prodotto: la callback widescreen e installata globalmente, ma osserva solo
una whitelist di PC. I blocchi non inclusi consumano il budget senza rendere
visibile lo stato promosso; il ramo callback esegue flush, callback e reload.

### 1. Lowering Dei Loop Come Regioni Host

E il candidato con il miglior rapporto costo/resa. Il generatore deve
riconoscere in modo generale SCC/loop CFG chiusi e:

- mantenere registri e flag live in locali host lungo il loop;
- emettere `while`/`for` o goto host senza materializzare ogni istruzione;
- conservare guest PC esatto per fault e SVC;
- conservare il numero di basic block consumati;
- usare il percorso scalare corrente quando block trace/callback e attivo;
- non riconoscere indirizzi o nomi specifici della Foresta Kokiri.

Il primo pilot sensato e una regola strutturale che copra il loop di copia a
`0x00466E7C`, ma la regola deve derivare da use/def, CFG e access pattern. Il PC
serve solo come fixture di test.

### 2. Analisi Di Alias E Register Promotion

Il C++ generato accede spesso a `frame.Guest.r[]` e a `context` tramite
riferimenti. Verificare nell'assembly MSVC se aliasing impedisce di mantenere
registri e budget in registri host. Un pilot valido puo usare parametri o view
`__restrict` soltanto dopo aver provato che frame, context e memory non possono
aliasare per contratto.

Fare prima il pilot su uno shard alternativo e confrontare assembly/counter;
non cambiare tutti i 256 shard alla cieca. Se il guadagno e reale, formalizzare
l'invariante nell'ABI e aggiungere test.

### 3. Contatori Diagnostici Fuori Dal Percorso Caldo

La produzione incrementa milioni di volte `DirectCalls`, `IndirectCalls` e
altri contatori. Misurare il costo di una modalita senza statistiche dettagliate
oppure accumularle localmente e flusharle all'uscita. La semantica guest non
dipende da questi contatori, ma i report diagnostici si: mantenere una modalita
profilata completa e documentare i campi non disponibili nella modalita veloce.

### 4. Backend LLVM Sullo Stesso IR

Se register promotion, aliasing e loop lowering non danno margine sufficiente,
non aggiungere altri workaround al C++. `aot_program.json` e gia backend-neutral
quanto basta per un pilot LLVM:

- SSA per registri/flag;
- loop naturali;
- call dirette;
- helper memoria tipizzati;
- object file per shard;
- stessa tabella entry -> host symbol;
- stessi trace differenziali.

LLVM e un backend alternativo, non una nuova semantica. Non sostituire servizi
CTR, PICA, audio o scheduler.

### 5. Confronto Compilatori

Clang-cl e un test legittimo, ma non va assunto come soluzione. Produrre un
archive separato con identici sorgenti e flag equivalenti, quindi alternare
MSVC/clang sullo stesso executable/runtime. Verificare dimensione, compile
time, guest time, host time e trace. Non sovrascrivere l'archive validato finche
il confronto non passa.

## Protocollo Di Accettazione

Una patch prestazionale e accettata se:

1. passa i 22 test emitter e i test processo;
2. produce trace interprete/whole-AOT equivalente per almeno 3 frame;
3. mantiene `whole_aot_unsupported_exits == 0`;
4. mantiene PCM `15053174716799755000` nella run Kokiri;
5. mantiene 37.817 draw e lo stesso stato finale nella medesima configurazione;
6. non disattiva audio, interpolazione, renderer o funzionalita;
7. migliora almeno tre coppie A/B alternate dopo warm-up;
8. offre un guadagno significativo: target minimo consigliato 5% sul guest o
   circa 3 FPS di prodotto, non un singolo outlier sub-percentuale;
9. deriva da una trasformazione generale A32/CFG/ABI, non da un indirizzo o da
   una scena hardcoded;
10. lascia `OOT3D_REBUILD_WHOLE_AOT=OFF` nella configurazione quotidiana.

Per cambi che possono influire su draw/PICA, aggiungere confronto framebuffer
interno. Non usare screenshot Windows come oracolo.

## Igiene Del Worktree

Sono presenti file generati non tracciati gia esistenti. Non committarli e non
cancellarli come parte del lavoro AOT:

```text
oot3d_native_cutscene_player.json
oot3d_native_fast3d_demo.json
oot3d_native_game.json
tools/oot3d/native_a32_runtime/_whole_aot_functions_*_candidate.json
tools/oot3d/native_a32_runtime/profiles/_navi_kokiri_main_forest_source_coverage_candidate.json
tools/oot3d/native_game_runtime/input_timelines/title_to_existing_file_start.json
```

Committare separatamente:

- cambi di semantica/emitter;
- selezione o profilo stabile;
- infrastruttura build;
- documentazione.

Non lasciare processi `oot3d_native_game`, `ninja`, `cl`, `link` o `cmake`
attivi a fine sessione.

## Primo Passo Suggerito

1. Riprodurre produzione, interprete puro e whole-AOT puro dal checkpoint.
2. Confermare trace equivalente da 257.159 record.
3. Generare il profilo block/runtime e confermare che `0x00466E7C` resta il
   primo hot loop.
4. Ispezionare assembly MSVC dello shard owner di
   `MeshCommandPacket_Submit` e verificare materializzazione di GuestState,
   aliasing e chiamate a `Oot3dAotEnterBlock`/memory helper.
5. Implementare un solo loop-region lowering strutturale dietro test emitter.
6. Rigenerare incrementalmente, compilare gli shard modificati e fare A/B.
7. Solo se il pilot supera il protocollo, generalizzarlo agli altri loop PICA,
   audio, curve e animazioni.

Il punto da cui ripartire non e la copertura AOT: e la qualita del codice host
prodotto dentro le funzioni gia coperte.
