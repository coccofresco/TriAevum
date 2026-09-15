# Handover: ottimizzazione AOT in parallelo

Data: 2026-09-15. Documento operativo per un agente indipendente.
Baseline sorgente: `0b30a6ef99706bd96f3dd090fa5ac8b33bef3cd1`, branch
`port/linux-nri`, repository `I:/TriAevum-public/source-repository-clean`.

## Obiettivo e separazione

Ridurre il costo CPU del codice whole-AOT realmente eseguito dal gioco,
mantenendo comportamento, completezza, memoria guest, audio, salvataggi e tempi
di simulazione. Consegnare ottimizzazioni riproducibili e reintegrabili, non una
nuova implementazione del gioco o una migrazione verso la decompilazione.

L'agente principale lavora su menu standard/controller derivati da Dusklight,
F1 standard e F12 avanzato ImGui. Non intervenire su questo lavoro, sui pannelli,
sul renderer NRI/PICA, su TopScreen, input, shader, Grass o cache GPU.
Non riaprire automaticamente il backlog di stuttering del renderer.

La tecnica e' libera entro questi confini: IR, lowering, C++ generato, dispatch,
ottimizzazione delle copie di stato, compilazione e layout. Una riduzione di
contatori interni o un microbenchmark non dimostra un miglioramento del gioco.
Nessun target percentuale e' promesso prima della misura della baseline attuale.

## Baseline autoritativa

Leggere prima:

1. `TRIAEVUM_PRECOMPILED_RELEASE.md`: contratto attuale di distribuzione.
2. `TRIAEVUM_TOPSCREEN_ISSUES_25_32_39.md`: ultime correzioni AOT/movie/scheduler.
3. `OOT3D_CROSS_PLATFORM_PERFORMANCE_STATUS.md`: meccanismi preesistenti, ma
   numeri e qualifiche sono uno snapshot del 28 agosto, non la baseline odierna.
4. `TRIAEVUM_STUTTERING_RESOLUTION_STRATEGY.md`: risultati renderer/pacing e limiti.

`OOT3D_AOT_PERFORMANCE_HANDOVER.md` e' esplicitamente storico (20 luglio).
`TRIAEVUM_WHOLE_AOT_MODULE_MIGRATION.md` contiene anche proposte storiche sulla
distribuzione superate dal contratto precompiled: non usarle per cambiare release.

Il manifest corrente `tools/oot3d/native_a32_runtime/whole_aot_product_manifest.json`
riporta **12.439 funzioni selezionate**, 3 confini host, 256 shard `affinity`, zero
residui A32 e zero rifiuti di lowering. Non ripartire dal vecchio conteggio 12.419.
Un audit strutturale non prova da solo la copertura di esecuzione dell'intero gioco.

| Identita | SHA-256 |
| --- | --- |
| code.bin baseline (4.567.040 byte) | `16a6b0aa4c4784680220a6f780f7f8a73cfb205557aa9f9f0e705179e0613220` |
| Programma strutturale qualificato | `c5df1b2c227fecd86bff4a0ba883932c37e2977266c7254db5ee4182b2cd1df5` |
| DLL Windows qualificata (86.859.776 byte) | `304dae27dde2d287f0d911efd2d66e194359ad9403f5bd9083b05fc16f5ffe6b` |
| SO Linux qualificata | `fbf5291cc268d5162c58162319e625bba56877b5700ef0d26c937535592b4b69` |

Il manifest DLL e il suo hash sono stati ricontrollati durante questo handover.
Non e' stata eseguita una nuova misura prestazionale. Il profilo DLL e'
`x86_64-windows-thinlto-release-v1`: **ThinLTO e' gia' presente**, non un nuovo guadagno.

Commit da preservare:

- `c7f9686`: trasferimenti memoria VFP double e correzione della mira.
- `7ee14a1`: continuazioni del decoder movie nel frontend AOT offline.
- `460960d`: recupero delle tabelle sparse di dispatch del decoder.
- `522f185`: quantum generico dei worker in background, necessario ai filmati.
- `d108659`, `dfc3e13`: composizione/qualificazione UI Windows e Linux.
- `0b30a6e`: separazione F1/F12; estranea all'ottimizzazione AOT.

## Lavorare senza interferire

Creare un worktree e un branch propri, senza cambiare branch nel checkout principale:

```powershell
git -C I:/TriAevum-public/source-repository-clean worktree add -b perf/aot-independent I:/TriAevum-aot-lab 0b30a6ef99706bd96f3dd090fa5ac8b33bef3cd1
```

Il comando e' una procedura, NON e' stato eseguito da questo handover. Se il nome
esiste gia', verificarne proprietario e stato; non resettarlo. La directory e' una
proposta: scegliere un volume con spazio sufficiente prima di creare build/cache.
Al momento della verifica: C circa 9,3 GB liberi, I circa 7,5 GB, J circa 108 MB.
Non avviare una nuova build AOT completa su J. Non liberare spazio cancellando
le cache, i checkpoint o le build dell'altro agente.

- Cartelle private distinte per build, oggetti, generated, ThinLTO, TMP/TEMP,
  profili PGO, configurazioni, save-data, log e framebuffer.
- La sola separazione del worktree non basta: CMakeCache, launch JSON e script
  possono ancora puntare al checkout principale o attivare un modulo condiviso.
  Verificare ogni percorso assoluto e le variabili d'ambiente prima del lancio.
- Dipendenze e baseline possono essere lette/confrontate; non eseguire build
  che rigenerino file dentro `_deps` condivisi. Usare una copia privata se scritti.
- Non modificare i profili attivi del giocatore, non sostituire DLL/SO nelle
  installazioni Windows/Linux e non lanciare test su Linux durante quelli altrui.
- Non fare push, release, merge o cherry-pick nel branch principale. Consegnare
  una sequenza di commit. La reintegrazione compete all'agente principale.
- Le decomp esterne, incluso `I:/oot3decomp`, restano evidenze in sola lettura.
  Questo lavoro non autorizza sincronizzazione, nuova decomp o loro modifica.

## Mappa del codice

Prefisso AOT: `tools/oot3d/native_a32_runtime/`.
Prefisso host titolo: `tools/oot3d/native_game_runtime/`.

| File/modulo | Responsabilita e tipo di intervento |
| --- | --- |
| AOT `whole_aot_program.py`, `generate_whole_aot.py` | Programma/CFG e copertura; non eliminare target poco frequenti |
| AOT `whole_aot_optimization_ir.py` | Use/def GPR, liveness N/Z/C/V, regioni CFG e dirty state |
| AOT `whole_aot_cpp.py`, `generate_whole_aot_cpp.py` | Emissione C++, terminali, continuazioni, shard e dispatch |
| AOT `oot3d_aot_architectural_state.h` | Stato promosso/lazy flags: ABI e correttezza ai confini osservabili |
| AOT `oot3d_native_a32_vfp_ops.*` | Semantica VFP, memoria double, corner case numerici |
| Host `oot3d_native_whole_aot_runtime.*` | Contesto, registry, esecuzione e confini host; cambi da coordinare |
| Host `triaevum_title_whole_aot_plugin.cpp` | Wrapper del modulo generato |
| Host `triaevum_title_whole_aot_abi.h`, `triaevum_title_aot_abi.h` | ABI del plugin: preservare, non alterare unilateralmente |
| `tools/triaevum_release/whole_aot_source_backend.py` | IR verificato a partire da input espliciti |
| `tools/triaevum_release/whole_aot_plugin_backend.py` | Generazione, compilazione, link del plugin senza attivazione utente |
| `tools/triaevum_release/whole_aot_object_cache.py` | Cache oggetti e compilazione incrementale, identita delle dipendenze |

Proprieta primaria del ramo parallelo: optimizer/emitter, test AOT e un nuovo
report specifico. Cambi a runtime condiviso, support library, CMake principale,
ABI, selezione o release builder devono essere commit separati, motivati, da
integrare esplicitamente. Non introdurre scheduler alternativo o seconda memoria.

## Vincoli di correttezza

- ABI whole-AOT **V2**, export `triaevum_title_whole_aot_query`.
  `Oot3dWholeAotProgramV2` ha size 48 nello header attuale. Usa anche tipi C++:
  il numero V2 da solo NON rende intercambiabili layout e support library diversi.
- Preservare `blockBudget`, `blocksConsumed`, `stopPc`, filtro/callback di ingresso,
  `skipFirstBlockEntry`, notifiche di uscita e nested external calls.
- Conservare indirizzi guest 32 bit separati dai puntatori host, wrapping, alias,
  alignment/unaligned access e flags osservabili. Nessun fast-math globale.
- Flush dello stato prima di callback, SVC, fault, yield, stop o serializzazione;
  reload dopo un host call che possa modificarlo. Le eccezioni/unwind contano.
- Il budget sostiene scheduling, audio e reattivazione dei thread: aumentarlo
  fino a bloccare gli altri task non e' un'ottimizzazione accettabile.
- Nessuna esclusione di funzioni perche' assenti dai benchmark. La selezione
  copre il prodotto, non soltanto title screen/Kokiri. Zero fallback aggiunti.
- Nessuna modifica a 30 Hz logici, velocita del gioco, interpolazione, audio,
  risoluzione o qualita per far apparire il candidato piu' veloce.

## Percorsi operativi verificati

Windows, da usare come riferimenti in sola lettura:

```text
Python: C:/Users/xander/AppData/Local/Programs/Python/Python313/python.exe
LLVM: I:/oot3dre_tools/llvm-22.1.6/bin/clang++.exe
Archiver: I:/oot3dre_tools/llvm-22.1.6/bin/llvm-ar.exe
Ninja: C:/Users/xander/.local/bin/ninja.exe
Headers JSON: C:/vcpkg/installed/x64-windows-static/include
Runtime: J:/TriAevum-verify-20260910/runtime/TriAevum.exe
Support: J:/TriAevum-verify-20260910/runtime/triaevum_title_whole_aot_support.lib
code.bin: E:/ppssppvr/oot3d_decomp/work/extract/exefs/code.bin
```

DLL baseline e manifest adiacente `whole-aot-plugin.json`:

```text
%TEMP%/triaevum-ui-title-cache/plugins/b10ba15bf96af856f8fbad0a902ff012507e62f1c36ba39b935c252a29fcb9f0/triaevum_title_aot.dll
```

Cache generated osservata:
`%TEMP%/triaevum-ui-title-cache/generated/290cbaa479ef25382a9201e78f2eaf8a1bd9f546e819a624e10b333a94e3aefc/whole_aot_cpp_manifest.json`.
Verificarne hash programma/selezione e file emessi prima di usarla; non scegliere
una directory soltanto per data di modifica. I file temporanei possono sparire:
copiare soltanto gli input necessari in un archivio privato dell'esperimento,
registrandone hash e provenienza. Non pubblicarli nel repository.

`tools/oot3d/operational_inputs.json` e' un indice storico utile, ma contiene
`repoRoot=I:/oot3dre`: NON considerarlo il checkout attuale. Anche il launch JSON
generato nella build runtime puo' riferirsi a manifest non presenti; qui e' stato
verificato che `runtime/oot3d_native_process_manifest.json` non esiste.
Usare un'invocazione di gioco qualificata, non quel launcher solo perche' esiste.

Linux, riferimenti dall'ultima qualificazione (non ricontrollati via SSH qui):
`xander@192.168.1.190`, chiave `%USERPROFILE%/.ssh/triaevum_linux_ed25519`;
source `/home/xander/triaevum-linux`, build `/home/xander/triaevum-linux-build`,
installazione `/home/xander/triaevum-linux-play`, modulo
`/home/xander/triaevum-ui-movie-title-build/triaevum_title_aot.so`.
Creare directory proprie: non sovrascrivere nessuna di queste.
La baseline renderer Linux non e' ancora interamente allineata a Windows:
confrontare A/B per piattaforma, mai dedurre speedup Windows da FPS Linux.

## Iterazioni veloci e sicure

1. Congelare runtime, support library, DLL baseline, input e toolchain; registrare
   hash in `baseline.json` privato. Riprodurre il gioco prima di ottimizzare.
2. Profilare una volta una scena rappresentativa; separare costo generato da
   SVC/IPC/PICA host, GPU, sleep e I/O. Ispezionare il disassemblato del codice caldo.
3. Scegliere una trasformazione generale guidata dal costo dominante. Promozione
   dei registri, lazy flags, affinity sharding e ThinLTO esistono gia': verificare
   cosa emettono davvero, non ripresentarli come nuove implementazioni.
4. Testare la trasformazione su fixture eseguibili; poi generare/compilare il
   modulo completo usando la cache incrementale. Non modificare a mano soltanto
   gli shard: l'emitter deve riprodurre il risultato da zero.
5. Fare A/B cambiando solo `--title-plugin` su un runtime fissato. Se serve una
   modifica host, misurare separatamente runtime vecchio/nuovo e modulo vecchio/nuovo
   quando ABI-compatibili; non attribuire un guadagno host al compilatore.

API di build piu' sicura per l'esperimento:
`build_whole_aot_plugin(program_path=..., selection_path=..., code_path=...,
cache_root=..., toolchain=WholeAotPluginToolchain(...), shard_count=256, jobs=2)`
in `tools.triaevum_release.whole_aot_plugin_backend`. Restituisce il percorso del
plugin content-addressed senza attivarlo nel profilo del giocatore.
Eseguire Python dal worktree proprio e controllare che `REPO_ROOT` punti li'.

Il comando `python -m tools.triaevum_release.forge build-title --help` descrive
l'alternativa CLI, ma **build-title attiva anche il risultato**: impostare tutti
i percorsi privati, inclusi `--active-title-state`, `--runtime-plugin`,
`--launch-profile`, `--data-root`, oltre a prepared/cache. Non usare i default.
Mai usare l'installazione Forge utente per compilare il candidato.

Iniziare con 2 compilazioni simultanee, aumentare solo misurando RAM e paging.
Mantenere shard count/layout stabili fra esperimenti e cache ThinLTO separata.
Registrare tempo generation/compile/link, oggetti reused/compiled e picco RAM.
Non copiare una CMakeCache con riferimenti al checkout principale. Per il percorso
CMake leggere le opzioni `OOT3D_REBUILD_WHOLE_AOT`,
`OOT3D_WHOLE_AOT_MAX_PARALLEL_COMPILES`, `OOT3D_WHOLE_AOT_PROFILE_MODE` e
`OOT3D_WHOLE_AOT_THINLTO_CACHE_DIR`: non tutte governano il builder plugin Python.
PGO/layout profilato sono esperimenti successivi, non motivo per riscrivere il
frontend prima di una misura; profili esclusivamente title screen sono insufficienti.

## Verifiche e misurazioni

Suite rapida, dalla radice del worktree (configurare `CXX` al compilatore disponibile
per evitare che i test eseguibili vengano saltati silenziosamente):

```powershell
$env:CXX = 'I:/oot3dre_tools/llvm-22.1.6/bin/clang++.exe'
& 'C:/Users/xander/AppData/Local/Programs/Python/Python313/python.exe' -m unittest discover -s tools/oot3d/native_a32_runtime -p 'test_whole_aot*.py'
& 'C:/Users/xander/AppData/Local/Programs/Python/Python313/python.exe' -m unittest discover -s tools/oot3d/native_a32_runtime -p 'test_audit_whole_aot_product.py'
```

Aggiungere i test release di `whole_aot_source_backend`, `whole_aot_plugin_backend`
e `whole_aot_object_cache` se si toccano quei confini. Compilare e lanciare test
ABI/support/host pertinenti nel proprio build. Verificare il caricamento con:
`TriAevum.exe --verify-title-plugin <candidato.dll>`; e' un controllo ABI, NON un boot.

Matrice minima di gioco:

| Scenario | Rischio coperto |
| --- | --- |
| Boot reale e intro/title completa | Entry, callback indirette, animazione e audio |
| Kokiri giocabile, camminata e menu | Scheduling ordinario, input, savestate |
| Link adulto a Hyrule Field/castello, movimento e cavalcata | Carico ampio, attori, spikes |
| Volo Navi / cambio di scena | Stream, lavoro guest e cooperazione thread |
| Sheikah Stone: movie completo e annullamento | Decoder, dispatch sparso, worker e ritorno al gioco |

Fixture/timeline movie documentate in `TRIAEVUM_TOPSCREEN_ISSUES_25_32_39.md`:
`%TEMP%/TriAevum-ui-canvas-ultrawide`, `%TEMP%/stone-complete-input.json`,
`%TEMP%/stone-cancel-input.json`. Non usare il vecchio fixture Linux stone-facing:
non entrava nel filmato. Su Linux esiste il runner privato
`%TEMP%/triaevum_linux_stone_complete.py` sul PC Windows come riferimento dei
comandi, non come dipendenza pubblica. Conservare privatamente le fixture richieste.

Tre passaggi distinti:

- Correttezza deterministica: stessi step guest/input, stato/memoria osservabili,
  flusso PICA, PCM e framebuffer. Escludere solo campi host non deterministici
  esplicitamente identificati, non normalizzare divergenze guest. Le catture devono
  provenire dal framebuffer; includere save/load e continuazioni dopo restore.
- Throughput CPU: stessi parametri, native30 senza interpolazione, VSync/limiti
  disattivati e verificati; `--throughput-benchmark`, audio dichiarato e conservato
  nel risultato principale. Escludere warmup/caricamento e misurare tick guest
  completati/secondo e tempo guest, non chiamate di presentazione.
- Pacing reale: impostazioni utente normali, x2/x3 dichiarati, nessun fixed delta,
  screenshot o profiling pesante. Misurare p50/p95/p99/max e intervalli oltre
  16,67/33,33 ms; distinguere attese del limiter dal lavoro CPU.

`tools/renderer/tev_program/measure_pacing.py` misura pacing, NON throughput.
Accetta `--invocation` JSON array argv oppure `{executable, arguments}`;
`--rates 30 60 90`, `--warmup-frames`, `--load-state`, `--input-timeline`.
Riutilizzare `castle_field_motion.json` nella stessa cartella. Il runner prepara
cache output proprie: uniformare lo stato cold/warm fra A/B e non includere la
preparazione nel confronto post-warmup. Non modificare questo runner condiviso:
aggiungere l'eventuale harness AOT in file separato.

Almeno tre coppie alternate A/B, nessuna build pesante contemporanea; riportare
mediana, dispersione e run peggiori. Se la differenza e' sotto il rumore, scrivere
"nessun miglioramento dimostrato". GPU-bound non significa AOT ottimizzato.
I test devono avere timeout e chiudere soltanto il processo da loro avviato.
15 secondi non sono un limite rigido: consentire boot/test piu' lunghi quando
necessari, ma mai processi lasciati indefinitamente in background.

## Consegna e reintegrazione

Consegnare in `docs/TRIAEVUM_AOT_OPTIMIZATION_RESULTS.md` (nuovo, sul ramo proprio):

1. Base commit, commit ordinati, file toccati e comando per riprodurre ogni prova.
2. Ipotesi, trasformazione, casi limite e perche' il comportamento resta equivalente.
3. Hash di runtime, support, programma, selezione, generated manifest, DLL/SO,
   toolchain e input. Dichiarare quali piattaforme sono effettivamente verificate.
4. Tempi A/B grezzi e riepilogo, contatori di lavoro, RAM/code size/build time,
   test pass/fail/skip, divergenze note e collocazione privata delle evidenze.
5. Eventuali cambi ABI/host obbligatori in commit separati e procedura di rollback.

Una trasformazione per commit logico, test nel relativo commit; niente merge di
intere directory storiche, formattazioni massive o modifiche alla decomp esterna.
Per reintegrare: ispezionare diff, cherry-pick in ordine, ricompilare con il builder
attuale e ripetere almeno boot/Kokiri/movie sulla nuova testa del ramo principale.
La DLL del laboratorio non va copiata alla cieca nella release: rigenerare il
modulo qualificato e la sua sorgente corrispondente, aggiornare i cataloghi tramite
il publisher e superare audit pubblico soltanto in una successiva fase release.

Non committare ROM, code.bin, asset, save, catture, SDK o cache oggetti. Lo stato
attuale distribuisce logica titolo precompilata esplicitamente catalogata e sorgente
corrispondente; non introdurre compilazione/IR/SDK nel percorso Forge dell'utente.

## Primo incarico per l'agente

Creare il worktree isolato, preservare il modulo qualificato, produrre una baseline
A/B ripetibile senza modifiche, identificare il costo generato dominante e solo
poi applicare la prima trasformazione. Non toccare UI/Dusklight/NRI. Se manca un
input o un percorso storico e' obsoleto, ricostruire l'invocazione dai manifest
verificati; non ripartire da una vecchia DLL incompleta. Questo handover non ha
avviato nuove build AOT, processi di gioco o modifiche alle installazioni.
