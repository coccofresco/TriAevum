# TriAevum: handover generale di progetto

Snapshot: 22 settembre 2026. Lingua: italiano. Destinatario: nuovo agente di
sviluppo, anche indipendente e successivamente reintegrabile.

Questo documento descrive il progetto esistente, non propone di riscriverlo.
Le verifiche di questa sessione sono letture di sorgenti, manifest e Git:
**non costituiscono una nuova qualifica di build, prestazioni o gameplay**.

## 1. Stato da cui partire

- Repository operativo: `I:/TriAevum-public/source-repository-clean`.
- Repository pubblico: <https://github.com/coccofresco/TriAevum>.
- Branch locale osservato: `port/linux-nri`.
- HEAD osservato: `a9b4477`, 18 settembre 2026, ritiro delle release rotte.
- **alpha.3 e alpha.3b sono state ritirate come BROKEN**: non distribuirle,
  non interpretare le qualifiche precedenti come approvazione attuale.
- Il gioco ha un percorso whole-AOT realmente giocabile e un renderer NRI.
  Non siamo al punto di dover inventare un nuovo boot o sostituire il gioco
  con una sandbox. La qualifica delle ultime release e' invece da riaprire.
- Ultima attivita': valutazione, non integrazione, del donor Android
  `WINDROID-EMU/RECOMP-ZELDA-3DS-ANDROID`; dettagli nella sezione 13.
- Esiste lavoro AOT parallelo non tracciato: non cancellarlo, incorporarlo
  automaticamente o attribuirgli lo stato di funzionalita' attiva.

Prima azione: leggere `AGENTS.md`, `git status`, branch, HEAD e worktree.
Il nome di una cartella, una DLL presente o un vecchio report non identificano
da soli la build realmente eseguita.

### Gerarchia delle fonti

1. Contratti correnti, sorgenti e manifest della build effettivamente usata.
2. Evidenze riproducibili associate a commit, configurazione e input esatti.
3. Documenti recenti specifici del sottosistema.
4. Vecchi piani, rapporti e conversazione: contesto storico, non specifica attuale.

Molti documenti conservano appendici e proposte superate. In particolare,
`TRIAEVUM_PRODUCT_WORKFLOW.md` e `TRIAEVUM_DEVELOPMENT_AND_RELEASE_WORKFLOWS.md`
contengono sezioni storiche sulla compilazione lato utente: sono superate dal
[contratto precompiled](TRIAEVUM_PRECOMPILED_RELEASE.md).

## 2. Identita' e obiettivo

TriAevum e' una ricompilazione statica di Ocarina of Time 3D con runtime host,
renderer moderno e presentazione a schermo singolo. Il modello operativo e'
analogo, in termini generali, alle recomp statiche Xbox 360: traduzione del
programma guest in codice compilato per la macchina host, servizi di piattaforma
sostituiti e grafica tradotta verso un backend moderno.

Non e' oggi un port del gameplay N64 con texture sostituite. Non e' neppure
una build di tutto il gameplay riscritto a mano in C++ semantico. La decomp
serve come evidenza, fonte di strutture e interventi mirati; il prodotto
funzionante deriva dalla traduzione whole-AOT del programma originale 3DS.

Priorita': comportamento completo e fedele, runtime manutenibile, prestazioni
misurabili, Windows/Linux allineati e supporto Android. Modularita' rivolta
anche ad altri giochi **3DS**, non promessa di supporto universale ad altre console.

## 3. Mappa concettuale

```text
SVILUPPATORE
code.bin + ExHeader + inventari/contratti verificati
  -> recupero programma A32 e flusso di controllo
  -> IR whole-AOT -> C++ generato per shard -> compilatore/linker host
  -> modulo titolo precompilato + identita'/manifest + sorgente corrispondente

UTENTE
ROM personale decriptata .3ds/.cci
  -> Forge: riconoscimento, estrazione, adattamento della revisione, preparazione
  -> dati privati + selezione del modulo precompilato compatibile
  -> profilo di avvio -> TriAevum

ESECUZIONE
modulo titolo whole-AOT <-> memoria guest 32 bit / scheduler / servizi host
  |                          | input, filesystem, audio, tempo, savedata
  +-> comandi e stato PICA -> frontend PICA canonico
      -> stato draw/materiali/texture/trasferimenti
      -> programmi shader nativi e pipeline -> NRI -> Vulkan -> finestra
      -> metadati semantici -> grafo effetti opzionali -> composizione
                                  mondo / trasparenza / atmosfera / UI

PRESENTAZIONE
simulazione nativa -> campioni temporali -> interpolazione visiva x2/x3
  -> composizione TopScreen/UI -> output e rapporto d'aspetto
```

La figura rappresenta responsabilita', non un'autorizzazione a riordinare i
draw nativi: le superfici possono essere intercalate e il composer deve
conservarne i contratti reali. Un fullscreen quad non e' automaticamente UI.

## 4. Whole-AOT: che cosa e' e da dove viene

Il nome corretto e' **whole-AOT** (Ahead Of Time), non whole-OAT.

Il programma commerciale usa istruzioni ARM A32 e memoria della piattaforma
3DS. Gli inventari e le analisi, anche ottenuti con Ghidra, hanno aiutato a
recuperare funzioni, blocchi, archi e target indiretti. Il percorso di prodotto
li combina con il codice originale per costruire un programma strutturale e
abbassarlo in C++ compilabile per host. Non equivale a compilare ciecamente il
testo pseudocodice prodotto da Ghidra.

La traduzione conserva ancora semantica guest: registri, flag N/Z/C/V, VFP,
indirizzi 32 bit, chiamate indirette, budget, yield e confini osservabili.
Il codice macchina finale e' nativo host; questo non elimina da solo i costi
di gestione della memoria, dispatch, stato architetturale o servizi 3DS.

La decomp risemantizzata e' un altro prodotto: nomi, strutture e funzioni C/C++
piu' leggibili. Una decomp che compila non prova equivalenza con `code.bin`.
I tentativi di sostituzione estesa hanno incontrato errori di ABI, puntatori
host/guest, flusso e servizi. Non riaprire quel progetto implicitamente durante
una correzione grafica o una normale ottimizzazione.

### Manifest corrente, non vecchi conteggi

Fonte: `tools/oot3d/native_a32_runtime/whole_aot_product_manifest.json`.

| Campo | Valore osservato |
| --- | ---: |
| Funzioni inventariate | 12.442 |
| Funzioni selezionate | 12.439 |
| Confini host espliciti | 3 |
| Blocchi | 162.719 |
| Istruzioni uniche | 924.535 |
| Shard C++ | 256, strategia affinity |
| Residui A32, archi statici irrisolti, rifiuti lowering | 0 dichiarati nel contratto |

I tre confini elencati sono `PicaCommandWriter_WriteRegisterRange`,
`Mtx3x4_Multiply`, `Mtx3x4_CopyIfDistinct`. Vecchi documenti li chiamano helper
atomici: leggere il manifest corrente prima di assumerne natura e contenuto.
Questi conteggi provano la chiusura strutturale dichiarata, **non una partita
completa testata**, ne' l'assenza di errori semantici.

Input canonico: `code.bin` di 4.567.040 byte, SHA-256
`16a6b0aa4c4784680220a6f780f7f8a73cfb205557aa9f9f0e705179e0613220`.
Il supporto EUR/USA e alle revisioni usa adattatori e cataloghi espliciti:
non inferire compatibilita' dal solo Program ID o nome ROM.

### Invarianti AOT

- Non confondere puntatori host 64 bit con indirizzi guest 32 bit.
- Preservare wrapping, aliasing, allineamento e semantica VFP/flag.
- Materializzare lo stato prima di servizi, callback, fault, yield e salvataggio;
  ricaricarlo quando un confine host puo' averlo modificato.
- L'unwind non deve sovrascrivere lo stato guest piu' profondo con uno snapshot
  esterno obsoleto; vedere scope-exit e contratti di uscita osservabile.
- Budget e scheduling servono anche audio, movie e thread: eliminarli o gonfiarli
  indiscriminatamente non e' un'ottimizzazione valida.
- ABI V2 e support library devono corrispondere: stesso numero di ABI non basta
  quando cambiano layout o dipendenze C++.
- Non aggiungere fallback interprete per nascondere buchi della traduzione.

## 5. Renderer NRI e grafica nativa

Leggere prima [architettura](OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md).
NRI e' l'astrazione grafica usata dal renderer, **non una sostituzione della
semantica PICA**. Il backend operativo principale e' Vulkan. Nomi storici
`gfx_vulkan`, `oot3d` e `ship` rimangono in parti attive: non dedurne che siano
renderer o gameplay N64 da eliminare.

Tre livelli da conservare:

1. Adapter OOT3D: identita' di attori/scene, indirizzi titolo, classificazione
   motivata e formati specifici.
2. Livello condiviso 3DS/PICA: registri, TEV, illuminazione, LUT, texture,
   vertex processing, fog, blend/depth/stencil e trasferimenti.
3. Core NRI/estensioni: risorse, pass, barriere, lifetime e pubblicazione dati.

Il percorso storico genera shader specializzati e usa corpus/cache. Il nuovo
percorso recupera comportamento nativo, incluso lavoro sul vertex shader
`CmbVShader`, in programmi stabili parametrizzati dallo stato. Non confondere:

- AOT CPU del gameplay;
- preparazione offline shader/SPIR-V;
- creazione di pipeline GPU/driver;
- ottimizzazione di upload, copie, draw e sincronizzazioni.

TEV parametrico non risolve automaticamente tutte le altre categorie.
La cache Forge resta un percorso esistente e una risorsa per il legacy:
non chiamarla soluzione obbligatoria del nuovo percorso cache-independent.
Riferimenti: `TRIAEVUM_OOT3D_UBERSHADER_ANALYSIS.md`,
`TRIAEVUM_STUTTERING_RESOLUTION_STRATEGY.md`,
`TRIAEVUM_TEMPORAL_OFFLINE_STUTTER_PROGRESS.md`.

### Estensioni e composizione

Toon, outline, Grass, CACAO, riflessi, ombre e antialiasing devono essere moduli
con ingressi dichiarati. Il grafo compilato possiede ordine, risorse e barriere.
Un effetto disattivato deve essere inerte rispetto alla resa nativa.

- `PicaSceneFrame`: vista passiva dei draw e risorse, non nuova fonte di verita'.
- `NativeSceneView`: metadati disponibili per camere, trasformazioni, luci,
  geometria, materiali, scheletri e identita' stabili.
- UI/HUD non sono input di AO, toon, outline o altri effetti della scena.
- Trasparenza e occlusione seguono ordine e depth nativi, non euristiche visive.
- Grass deve interagire con guide/occlusione secondo il contratto specifico;
  non modificare globalmente depth o outline per correggere una singola scena.
- I conteggi MRT nei documenti sono evoluti: non copiare un vecchio layout
  numerico senza confrontare gli header e il piano effettivamente compilato.

Il black loader, la Camera dei Saggi e la UI sono regressioni sentinella utili:
un gradiente sul nero puo' derivare da composizione/effetti, non da un asset errato.

## 6. Mappa fisica del sorgente

Tutti i percorsi seguenti sono relativi al repository, salvo indicazione diversa.

| Percorso | Responsabilita' |
| --- | --- |
| `CMakeLists.txt`, `CMake/` | Target, configurazione e collegamenti |
| `tools/oot3d/native_a32_runtime/` | Frontend/IR/generatori AOT, manifest, helper e test |
| `tools/oot3d/native_game_runtime/` | Host gioco, scheduler, memoria, input, servizi, composizione titolo |
| `tools/oot3d/native_pica_frontend/` | Frontend PICA del titolo, preparazione shader |
| `tools/oot3d/ui_topscreen/` | Reimplementazione comportamenti/layout TopScreen |
| `tools/oot3d/ui_n64/` | Componenti UI storici e consumer: verificare uso reale prima di rimuovere |
| `tools/oot3d/decomp_support/` | Evidenze e snapshot; non una promessa di sorgente gameplay attivo |
| `runtime/three_ds_recomp/src/fast/renderer3ds/` | Meccanismi condivisi PICA/NRI |
| `runtime/three_ds_recomp/src/fast/oot3d/` | Adapter, moduli NRI, effetti e codice storico da classificare |
| `runtime/three_ds_recomp/src/fast/backends/` | Backend e integrazione finestra/Vulkan |
| `runtime/three_ds_recomp/include/fast/` | Contratti pubblici dei moduli grafici |
| `runtime/three_ds_recomp/src/ship/` | Servizi host ereditati/evoluti, audio e controller |
| `runtime/three_ds_recomp/tests/` | Test grafici e contratti runtime |
| `tools/renderer/tev_program/` | Analisi, prove e strumenti renderer/AOT; non tutto e' codice di prodotto |
| `tools/triaevum_release/` | Forge, import ROM, moduli precompilati, packaging/audit |
| `scripts/oot3d/`, `scripts/triaevum/` | Automazione Windows, build e qualifiche |
| `scripts/build-linux-runtime.sh`, `scripts/package-appimage.sh` | Build e pacchetto Linux |
| `ports/android/` | App, bridge JNI, controlli e integrazione Android |
| `config/`, `.github/workflows/` | Configurazioni versionate e CI |
| `docs/`, `LICENSES/`, `THIRD_PARTY_NOTICES.md` | Evidenze, contratti, licenze e attribuzioni |

### File di ingresso AOT

- `whole_aot_program.py`, `generate_whole_aot.py`: programma e CFG.
- `whole_aot_optimization_ir.py`: analisi use/def, flag, regioni e dirty state.
- `whole_aot_cpp.py`, `generate_whole_aot_cpp.py`: emissione e shard.
- `oot3d_aot_architectural_state.h`: stato e contratto di materializzazione.
- Host `oot3d_native_whole_aot_runtime.*`: esecuzione e confini.
- Host `triaevum_title_whole_aot_plugin.cpp`, `triaevum_title_whole_aot_abi.h`:
  ingresso e ABI del modulo.
- Release `whole_aot_source_backend.py`, `whole_aot_plugin_backend.py`,
  `whole_aot_object_cache.py`: costruzione e riuso degli oggetti.

### File grafici da individuare prima di una modifica

In `src/fast/oot3d/`: `effect_graph.cpp`, `effect_graph_physical_plan.cpp`,
`effect_graph_pass_barriers.cpp`, `native_scene_view.cpp`,
`pica_scene_frame.cpp`, `pica_scene_publication_adapter.cpp`,
`nri_pica_render_target_owner.cpp`, `nri_pica_texture_image_owner.cpp`,
`nri_pica_display_copy_pass.cpp`, `nri_pica_scanout_pass.cpp`, `nri_swapchain.cpp`.
In `src/fast/renderer3ds/`: `pica_semantic_scene_view.cpp`,
`nri_pica_pipeline_bridge.cpp`, `nri_pica_upload_arena.cpp`.

I nomi simili in directory diverse non autorizzano una seconda implementazione:
seguire le chiamate e il target CMake per identificare adapter e owner condiviso.

## 7. UI, input e tempo

- TopScreen e' una reimplementazione delle funzioni della mod di rlgcarrot,
  non semplicemente la promozione integrale del framebuffer inferiore.
- Il suo pacchetto di texture/font e' un input distinto, con download/import
  e licenza propri. Preservare credito e supporto all'autore.
- La UI conserva proporzioni e coordinate logiche native centrate rispetto
  all'output; il mondo usa FOV/aspect corretti, non stretching dell'immagine.
- L'ultima indicazione del manutentore mantiene **F1 consolidato**. Il progetto
  di separazione Dusklight F1 / ImGui F12 e' da isolare, non riattivare perche'
  descritto come corrente in vecchi handover.
- Un setting ha un owner: input per routing, TopScreen per comportamento/layout,
  window host per fullscreen/output, renderer per risoluzione interna.
- Esc deve liberare la cattura del mouse, non chiudere il gioco.
- Free camera e mira sono distinti. Gyro/accelerometro non devono pilotare la
  free camera. Mouse e analogico destro seguono il contesto e il profilo.
- Start deve essere consumato correttamente tra poll host e refresh guest:
  le regressioni storiche includono fronti persi e chiusura/riapertura col tasto
  tenuto. Non applicare debounce arbitrario per nascondere errori di routing.
- Simulazione nativa 30 Hz; x2/x3 sono presentazione interpolata 60/90 FPS,
  non fisica accelerata. Il vero gameplay a frequenza variabile e' un progetto
  separato e non va dichiarato completato.

Leggere `THREE_DS_RECOMP_INPUT_ARCHITECTURE.md`, `TRIAEVUM_F1_MENU_REVIEW.md`,
`TRIAEVUM_TOPSCREEN_ISSUES_25_32_39.md`, `OOT3D_TRUE_60FPS_HANDOVER.md`.

## 8. Storico ragionato: cosa e' cambiato

Le prime fasi sono una sintesi della storia del progetto, non una cronologia
di commit verificata riga per riga in questa sessione.

1. **Viewer asset OOT3D:** stanza di Link, mesh, texture, pose/animazioni, camera
   libera. Risoluzione di scale, sistemi di riferimento e materiali.
2. **Runtime sperimentale e scaffolding N64/SoH:** uso della semantica N64 per
   orientare scene, attori, collisioni e camere. Non e' il gameplay del prodotto
   attuale e non va reintrodotto come fallback generale.
3. **Ricostruzione intro/title:** Link adulto, Epona, camere, logo animato,
   transizione notte/giorno, cielo, luna, fog, polvere e materiali animati.
   Molte correzioni sono diventate meccanismi generali PICA/rendering.
4. **Decomp e Room Compilation Unit:** tentativi di consumer semantici e boot
   C++ diretto, con evidenze da Zelda3drecomp/oot3decomp. Compilazione e copertura
   dichiarata non hanno garantito esecuzione equivalente.
5. **Consolidamento whole-AOT:** traduzione strutturale, archivi/moduli separati,
   ABI, promozione stato, lowering, cache oggetti e verifiche in gioco.
6. **Renderer NRI e presentazione moderna:** Vulkan, grafica PICA, interpolazione,
   scene view, effetto graph, shader avanzati e isolamento UI.
7. **Prodotto TriAevum:** Forge, moduli precompilati, import ROM, TopScreen,
   release Windows, Linux e lavoro Android. Linux passa da Flatpak ad AppImage.
8. **Maturazione recente:** input/motion, font HD, movie/cutscene, Game Over,
   trasferimenti GPU e qualifica multipiattaforma; ultime due release ritirate.

### Commit recenti di orientamento

| Commit | Significato, non garanzia di assenza di regressioni |
| --- | --- |
| `c7f9686` | Correzioni trasferimenti memoria VFP double/mira |
| `7ee14a1`, `460960d`, `522f185` | Continuazioni movie, dispatch sparso e scheduling worker |
| `ecea4ca` | Esperimento direct-cohort AOT, non miglioramento attivo di prodotto |
| `f89d3fb` | Opzioni native TopScreen Game Over e diagnosi stallo Sheik |
| `3b7e8a1`, `b230513`, `0bcd22d` | Contratto TextureCopy, regioni tiled, avanzamento copie GPU (#45) |
| `a5d937e` | Documentazione allineamento Linux/Steam Runtime e replay #45 |
| `16be838` | Domini draw titolo/menu (#48) |
| `6863881`, `2df81d3` | Preparazione e qualifica alpha.3b, successivamente insufficienti |
| `a9b4477` | Ritiro alpha.3/alpha.3b: stato finale da rispettare |

Non copiare lo stato open/closed di issue da questo documento: ricontrollare
GitHub quando si riprende un ticket e conservare attribuzioni delle PR.

## 9. Build rapida e identificazione del binario

Una modifica a renderer/input/UI **non richiede la ricompilazione completa del
titolo**. Separare build host, modulo titolo, shader e pacchetto. Le build
incontrollate hanno gia' saturato RAM/dischi e bloccato il PC.

1. Trovare il build directory corrente dal rapporto della qualifica pertinente.
2. Leggere `CMakeCache.txt`: source root, generatore, compilatore, plugin/support,
   configurazione e dipendenze. Non usare un vecchio build tree a intuito.
3. Identificare il target reale: `oot3d_native_game`, pubblicato come
   `TriAevum.exe` dal percorso `triaevum_public_runtime`.
4. Consultare `triaevum-runtime-targets-Release.json` se prodotto dalla build.
5. Usare `--product-info` per identificare host/capacita'; verificare anche il
   modulo selezionato nel launch JSON e relativo manifest/hash.
6. Costruire il target minimo, con parallelismo compatibile con RAM libera.
7. Verificare che il file lanciato sia proprio quello appena aggiornato.

Comandi di orientamento, da adattare solo dopo lettura dei parametri reali:

```powershell
git status --short
git log -10 --oneline
git worktree list
Get-Content <build>/CMakeCache.txt
cmake --build <build> --target oot3d_native_game --parallel 2
& <runtime>/TriAevum.exe --product-info
```

Script disponibili: `scripts/oot3d/Build-Oot3dWholeAotProduct.ps1`,
`Configure-Oot3dWholeAotProduct.ps1`, `Build-Oot3dLlvm.ps1`,
`tools/oot3d/build_fast_dev.ps1`. Leggerli prima: alcuni default vengono da
worktree storici. Nessun nuovo full rebuild durante il semplice onboarding.

ThinLTO puo' rigenerare codice anche quando gli oggetti sono in cache.
Il rapporto del 17 settembre registra 465,67 s per preparare due varianti:
un relink invariato di due secondi non misura il costo della modifica successiva.

## 10. Percorsi locali e piattaforme

Il registro `tools/oot3d/operational_inputs.json` esiste per evitare ricerche
ripetute di tool/input. **E' datato 27 agosto e contiene `repoRoot=I:/oot3dre`:**
non eseguirne ciecamente tutti i percorsi. Validare esistenza/hash e riferire
il checkout attuale. Non aggiornarlo in blocco senza controllare i consumer.

| Riferimento | Stato/uso |
| --- | --- |
| `I:/oot3decomp/` | Decomp esterna recente; read-only salvo richiesta esplicita |
| `I:/Zelda3drecomp/` | Decomp/evidenze storiche; distinguere branch grezzo e semantico via Git/manifests |
| `E:/azahar pcvr/` | Emulator donor/oracolo e strumentazione storica |
| `E:/azahar pcvr/build-pcvr-qt/bin/Release/azahar.exe` | Percorso emulatore indicato dall'utente; verificare esistenza |
| `E:/azahar pcvr/tools/pcvr-re-tools/ghidra/` | Tool Ghidra storico, non cercarlo ogni iterazione |
| `E:/ppssppvr/oot3d_decomp/work/extract/exefs/code.bin` | Input storico canonico; controllare hash |
| `I:/oot3dre_tools/llvm-22.1.6/` | LLVM usato dai percorsi AOT documentati |
| `I:/oot3dre_work/`, `J:/...`, `%TEMP%/...` | Build/catture/cache storiche; possono essere state rimosse |
| `I:/TriAevum-public/donor-review-windroid/` | Clone di sola analisi del donor Android |
| `ssh <linux-host>` | Host Linux di prova; non e' uno Steam Deck |

In questa sessione ha funzionato il Git Windows:
`C:/Program Files/Git/mingw64/libexec/git-core/git.exe`.
Sono stati osservati problemi con wrapper `cmd/git.exe` e Git MSYS per fetch
lazy/HTTPS: verificare l'eseguibile con `Get-Command git` prima di diagnosticare
un problema di repository.

Linux: usare `TRIAEVUM_LINUX_PORT.md`, `TRIAEVUM_STEAM_RUNTIME_BUILD.md`,
`TRIAEVUM_DESKTOP_PACKAGING.md` e script AppImage. Flatpak e' storico.
Steam Deck: conformita'/compatibilita' da qualificare, non test hardware eseguito
sul PC Linux. Non confondere X11, Wayland, XWayland e Gamescope.
Se l'host non risponde, cercare la procedura Wake-on-LAN gia' documentata e il
MAC verificato: non inventarlo e non interpretare la macchina spenta come bug.

Android: leggere `ports/android/README.md`, `INTRO.md`, `CAPTURE.md`,
`SHADER_PREPARATION.md`; usare adb/scrcpy quando autorizzato e disponibile.
Le osservazioni storiche sulla intro a 720p non qualificano tutti i dispositivi
ne' l'intero gioco. Conservare NRI/Vulkan; non sostituirlo con un emulatore.

### 10.1 Operare sulla macchina Linux remota

Accesso SSH porta 22, utente locale della macchina, chiave locale documentata
`%USERPROFILE%/.ssh/triaevum_linux_ed25519`. Non riportare mai contenuto di chiavi
o password in log pubblici. Da PowerShell:

```powershell
$LinuxHost = '<linux-host>'
$Key = "$env:USERPROFILE/.ssh/triaevum_linux_ed25519"
ssh -i $Key -o BatchMode=yes -o ConnectTimeout=8 $LinuxHost 'hostname; uname -a; df -h /home'
```

Distinguere timeout/rete, autenticazione e comando fallito. Non disattivare il
controllo della host key per aggirare una segnalazione. Se spento, Wake-on-LAN
era gia' stato usato, ma **il MAC non e' stato recuperato dai documenti esaminati**.
Recuperare il launcher locale precedente o registrare l'interfaccia fisica
corretta mentre la macchina e' accesa (`ip -br link`, `ip route`); non usare il
MAC di bridge/container o inventare indirizzi. Il risveglio non sblocca il desktop.

Percorsi remoti documentati il 18 settembre, da verificare prima dell'uso:

| Percorso Linux | Funzione |
| --- | --- |
| `~/triaevum-linux` | Mirror sorgente; puo' avere modifiche locali |
| `~/triaevum-linux-build` | Build nativa host, iterazione rapida |
| `~/triaevum-steamrt4-build/runtime` | Build con ABI Steam Runtime |
| `~/triaevum-steamrt4-qualified-sdk` | SDK qualificato riutilizzabile |
| `~/triaevum-ui-movie-title-build/triaevum_title_aot.so` | Modulo titolo con correzioni movie |
| `~/triaevum-linux-play` | Installazione/profilo di sviluppo |
| `~/triaevum-android-build` | Lavoro Android; verificare sottobuild attivi |
| `~/triaevum-pipeline-live-proof` | Evidenze/cache/test privati storici |

Le due build Linux NON sono due edizioni del gioco: una usa il toolchain host,
l'altra il sysroot Steam per distribuzione. Un AppImage installato e il desktop
entry di sviluppo possono puntare a binari diversi. Nel rapporto #45 il desktop
entry **TriAevum - Updated Development Build** punta al runtime SDK, non al
pacchetto pubblico: ricontrollare il campo `Exec` prima di far provare il gioco.

Procedura per sincronizzare senza distruggere il lavoro remoto:

1. Leggere `git status` sul mirror, i CMakeCache delle due build e processi attivi.
2. Trasferire solo la revisione/tracked delta approvata, con `scp` o archivio
   tracciato. Salvare prima i file remoti divergenti in una directory privata.
3. Non usare `reset --hard`, `clean` o sincronizzazione con delete sul mirror.
   Il suo HEAD da solo puo' non rappresentare i file, aggiornati in passato per copia.
4. Confrontare hash/contenuti dei file trasferiti (normalizzare solo line ending
   quando necessario); registrare commit sorgente e manifest di sincronizzazione.
5. Ricostruire host e SDK sequenzialmente, solitamente tre job, senza rigenerare
   il titolo se il cambiamento non lo richiede.
6. Testare il binario corretto con copia privata di configurazione/savedata.

Esempio di build incrementale host, dopo tale verifica:

```powershell
ssh -i $Key $LinuxHost 'cmake --build ~/triaevum-linux-build --target oot3d_native_game --parallel 3'
```

Per SDK usare `scripts/run-in-steamrt4.sh` e le istruzioni di
`TRIAEVUM_STEAM_RUNTIME_BUILD.md`, conservando il build tree esistente. Non
aggiornare la distro o installare pacchetti con sudo per compensare un errore
nel sysroot; l'SDK puo' essere preparato senza modificare il package database.

Per avvio grafico remoto, la connessione SSH non eredita necessariamente la
sessione desktop. Identificare con `loginctl` la sessione attiva e ricavare
`DISPLAY`, `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR` e l'eventuale ambiente D-Bus dalla
sessione di quell'utente. Non forzare `DISPLAY=:0` o `SDL_VIDEODRIVER=x11` come
correzione universale. Usare il launcher/profilo esistente dopo averne letto i
percorsi. Verificare schermo acceso, sessione sbloccata, GPU effettiva e assenza
di software rendering; raccogliere log e framebuffer dal processo reale.

La shell interattiva dell'utente puo' essere fish: script Bash vanno trasferiti
e avviati con `bash script.sh`. Gli heredoc Bash incollati direttamente in fish
hanno gia' causato errori. Preferire script brevi a quoting annidato SSH/PowerShell.
Conservare PID/log del proprio test e chiuderlo esplicitamente, senza kill globali.

### 10.2 Dispositivo Android e divisione dei ruoli

Telefono documentato: **Samsung SM-S931B, Android 16, GPU Adreno 830**.
Il seriale va letto da `adb devices -l`, non assunto. Host di build preferito:
Linux remoto. Ponte USB/installazione/cattura: Windows, dove il dispositivo e'
stato collegato. Non presumere che sia attualmente collegato o autorizzato.

Tool locali documentati:

```powershell
$env:ADB = 'I:/oot3dre_tools/android/platform-tools/adb.exe'
$env:SCRCPY = 'I:/oot3dre_tools/android/scrcpy-4.1/scrcpy-win64-v4.1/scrcpy.exe'
& $env:ADB devices -l
# Impostare solo dopo aver identificato il dispositivo:
$Serial = '<seriale-verificato>'
& $env:ADB -s $Serial shell getprop ro.product.model
& $env:ADB -s $Serial shell getprop ro.build.version.release
```

Conservare il server ADB Windows e la coppia `%USERPROFILE%/.android/adbkey`.
Non copiare la chiave privata su Linux, non rigenerarla e non eseguire
`kill-server` ad ogni test. Scrcpy deve usare lo stesso ADB tramite `$env:ADB`.
Se `unauthorized`, serve l'approvazione sul telefono, preferibilmente permanente.
Non e' risolvibile con un bypass lato host. Telefono bloccato e ADB autorizzato
sono stati diversi: sbloccare prima della prova grafica.

### 10.3 Build, deploy e dati Android

Prerequisiti storici documentati: NDK r29, SDK 35, JDK 17, Gradle 8.13,
CMake/Ninja, target `arm64-v8a`, API nativa 29. Rileggere Gradle/CMake correnti
prima di riprodurre questi numeri. Runtime, titolo e APK hanno build separate:

- `ports/android/runtime`: wrapper CMake del runtime condiviso;
- `ports/android/native`: modulo titolo e probe, non APK giocabile da solo;
- `tools/android/stage_native_app.py`: staging delle librerie native;
- `ports/android/app`: Activity e packaging Gradle;
- `ports/android/controls`: donor overlay Azahar isolato, non presumere che
  ogni funzione sia gia' collegata alla Activity di intro;
- `tools/android/prepare_intro_data.py`: preparazione privata dei dati.

La sequenza completa e' in `ports/android/INTRO.md`, sezione build. Riutilizzare
il titolo ottimizzato e ricostruire solo runtime/staging/APK per modifiche host.
Non usare la vecchia build titolo `-O0` come baseline prestazionale. Sorgenti
FetchContent possono essere condivisi; le directory `_deps/*-build` no.

Trasferire l'APK Linux con `scp` a una directory privata Windows, verificarne
l'hash sui due host e installarlo senza cancellare dati:

```powershell
& $env:ADB -s $Serial install -r '<percorso-apk-verificato>'
& $env:ADB -s $Serial shell am force-stop org.triaevum.android
& $env:ADB -s $Serial shell am start -n org.triaevum.android/.TriAevumActivity
```

Non disinstallare e non usare `pm clear` per tentativi: distruggono dati/profili.
APK con firma diversa richiede una decisione esplicita, non reset automatico.
Controllare che il package/Activity restino quelli del manifest corrente.

Dati privati storici: `/sdcard/Android/data/org.triaevum.android/files`.
Vanno preparati da una installazione Forge valida, senza librerie desktop o
driver cache importate. Lasciare creare la cartella all'app; per il debug,
estrarre l'archivio privato attraverso `run-as org.triaevum.android`, non con
ownership dell'utente ADB shell. Leggere i dettagli in `INTRO.md` prima del
trasferimento. `run-as` richiede una build debuggable e non e' un contratto
dell'installazione utente finale. Mai inserire questi dati ROM-derived nell'APK.

### 10.4 Catture e qualifiche Android

Preview non interattiva, chiudibile senza terminare il gioco:

```powershell
& $env:SCRCPY --serial=$Serial --no-control --no-audio --window-title='TriAevum Android'
python tools/android/capture_device.py --serial $Serial --package org.triaevum.android --output I:/oot3dre_work/android-captures --seconds 15
```

Il tool salva video display, PNG, metadati e logcat filtrato; supporta anche
`--backend adb` e audio opzionale. Non catturare la finestra scrcpy dal desktop.
Non usare redirezioni testuali PowerShell per flussi PNG binari: usare il helper.
Le registrazioni restano fuori dal repository e possono contenere dati privati.

Per misurare: foreground reale, ingresso host (`SDL_main` nell'attuale percorso
documentato), superficie valida e incremento dei contatori guest. Leggere il
`data/runtime-state.json` relativo al profilo attivo. Il frame rate scrcpy non
e' il frame rate del gioco; cattura/encoder aggiungono carico. Separare test
prestazionali senza registrazione e test visivi, e conservare risoluzione/aspect,
limiter, interpolazione e profilo GPU identici tra confronti.

Per profilazione CPU esiste la procedura NDK simpleperf `record --app
org.triaevum.android`; non cambiare permanentemente le impostazioni di sicurezza
del telefono. Usare prove limitate, ripristinare ogni modifica autorizzata e
fermare solo il package proprio al termine. Un lock screen nero non e' un bug NRI.

Questa integrazione documentale non ha riacceso il PC, contattato SSH o installato
APK: disponibilita', percorsi e versioni sul dispositivo restano da ricontrollare
all'inizio della prossima sessione operativa.

## 11. Come verificare senza ricadere nei problemi precedenti

### Correttezza

- Test unitari al confine proprietario, poi gioco reale con input/save riproducibili.
- Catture dal framebuffer del runtime, non screenshot Windows come unica prova.
- Includere configurazione, hash host/plugin, frame guest e presentazione,
  risoluzione, aspect, effetti, interpolazione e savedata utilizzati.
- Per regressioni UI: boot loader, title, selezione salvataggio, gameplay,
  pausa/inventario/mappa, ocarina, Game Over, cavalcata e transizioni.
- Per runtime/GPU: includere movie e rivelazione Sheik oltre a Kokiri/title.
- Testare effetti Off e On; la modalita' F2 non deve alterare stato nativo.
- Per F1 seguire l'actual-widget smoke in `TRIAEVUM_F1_MENU_REVIEW.md`.
- Un test C++ positivo non prova che quel codice sia selezionato nel gioco.
- Una conferma visiva del manutentore e' evidenza utile da registrare, non da
  contraddire con un test che usa un altro profilo o savestate.

### Prestazioni

Separare tempo CPU attivo, attese, sincronizzazioni, frontend PICA, shader/pipeline,
upload, GPU NRI, present e audio. Il totale pre-NRI non e' tutto AOT.

- Non contare caricamenti nei FPS di gameplay e non contare frame interpolati
  come nuova simulazione. Dichiarare native refresh/s e presentation/s distinti.
- Fissare VSync/limiter/interpolazione; per throughput usare profilo esplicito
  senza limiti, senza contaminare la configurazione normale dell'utente.
- Warmup e cold start sono esperimenti diversi. Fare A/B ripetuti, meglio ABBA.
- Readback sincroni e logging dettagliato falsano il pacing: separare catture
  di fedelta' dalla misura prestazionale.
- Non vendere microbenchmark o riduzioni di contatori come guadagni in gioco.
- Fermare l'estensione di un prototipo senza beneficio ripetibile.

Esempio reale: `TRIAEVUM_AOT_RENDICONTO_2026-09-17.md`, pilota da 22 funzioni:
8,224 -> 8,928 ms nella prima serie, 9,010 -> 8,957 ms nella seconda.
**Nessun guadagno ripetibile; esperimento disattivato.** Il gruppo da 303 funzioni
non e' per questo diventato tutto attivo/verificato.

Le run non devono restare aperte indefinitamente. Dichiarare timeout e pulizia
del proprio processo; lasciare una sessione aperta quando serve al test umano.
I 15 secondi citati nella storia non sono un limite invalicabile per una run
diagnostica giustificata.

## 12. Release, licenze e dati

Autorita': `TRIAEVUM_PRECOMPILED_RELEASE.md`, `LICENSE_SCOPE.md`,
`THIRD_PARTY_NOTICES.md`, release audit/allowlist.

- Forge utente chiede la ROM decriptata; non installa SDK e non compila/linka
  il titolo. Modulo mancante/incompatibile = errore di pacchetto.
- Distribuire runtime, Forge, modulo titolo catalogato e sorgente tradotto
  corrispondente secondo il contratto adottato, senza chiamarlo title-neutral.
- ROM, code.bin, ExHeader/RomFS estratti, keys, save/catture e SDK non vanno nel
  pacchetto pubblico. Dati generati dell'utente restano locali.
- Il corpus shader portabile ha un'eccezione esplicita approvata il 12 settembre:
  vedere `TRIAEVUM_BUNDLED_FORGE_SHADER_CORPUS.md`. Non estenderla ai driver cache.
- Windows portable; Linux AppImage. UI e runtime vanno verificati dal pacchetto
  reale, non solo dalla macchina di sviluppo con dipendenze installate.
- Non copiare una directory di sviluppo o una installazione usata nella release.
- Non pubblicare un archivio con binari aggiornati e manifest/sorgenti vecchi.
- Preservare crediti SoH/Ship of Harkinian, Azahar, NRI, TopScreen/rlgcarrot,
  componenti terzi e contributori PR, incluso Sion dove pertinente.
- Non rinominare dipendenze per nasconderne l'origine; attribuzione separata
  dalla nomenclatura tecnica, licenze specifiche sempre conservate.

## 13. Donor Android esaminato, non integrato

URL: <https://github.com/WINDROID-EMU/RECOMP-ZELDA-3DS-ANDROID>.
Revisione analizzata: `69ee73ea61be06f62785c0fcd6bf65a487197db0`.
Base TriAevum nella storia: `e419028` (alpha.2c). Molti nuovi commit sono
attribuiti a `genrogamer`: preservare autori se ripresi.

Interessanti: Activity/ANativeWindow nativi, sospensione/ripresa superficie,
audio AAudio, editor touch/HUD, preparazione shader parallela/incrementale.
Quest'ultima puo' servire anche Forge desktop, ma non dimostra un guadagno FPS.

Non fare merge wholesale. Criticita' osservate:

- callback AAudio usa mutex condiviso col produttore;
- custom HUD usa globali e controlli/parsing filesystem nel modulo di profilo;
- pacing Android aumenta spin da 0,5 a 2 ms e cambia recupero ritardo;
- sostituzione SDL richiede verifica di mapping, hotplug, motion e controller;
- sprint/skip dialoghi sono modifiche gameplay, non fix fedelta';
- geometrie/viewport TopScreen divergono dalla nostra evoluzione recente;
- packaging e payload vanno revisionati indipendentemente dalla GPL del codice.

Portare eventualmente moduli separati, con test sull'host Android e contratti
comuni, non i default o le modifiche gameplay indiscriminatamente. Non sono
stati effettuati benchmark o avvii APK in questa valutazione.

## 14. Lavoro concorrente e sicurezza del repository

Allo snapshot risultano non tracciati `.pi/`,
`docs/TRIAEVUM_AOT_MEMORY_REGIONS.md`,
`tools/oot3d/native_a32_runtime/{memory_region_probe.cpp,run_memory_region_probe.py,test_whole_aot_memory_regions.py,whole_aot_memory_regions.py}`,
`tools/oot3d/native_game_runtime/oot3d_native_a32_memory_region.h`,
`tools/renderer/tev_program/{build_aot_region_pilot.py,summarize_aot_abba.py,test_summarize_aot_abba.py}`.

Non sono stati modificati per questo handover. Chiarirne proprietario e stato
prima di integrarli. E' gia' accaduto che un'altra istanza ripristinasse file
non committati: usare worktree, branch, build/cache e profili separati.

Mai reset/clean globale per ripartire. Prima di pulizia disco verificare target
assoluti, worktree registrati, proprietario, processi e riferimenti dei manifest.
Cartelle vecchie possono ancora contenere l'unico modulo/oracolo funzionante.

## 15. Procedura di ripresa consigliata

1. Fotografare Git e leggere questo documento, AGENTS e contratto dell'area scelta.
2. Identificare build, modulo, launch JSON e dati reali; non lanciare un vecchio
   viewer o una build diagnostica al posto del gioco.
3. Fissare una baseline riproducibile e conservare gli input in sola lettura.
4. Scegliere una sola responsabilita' modulare, con obiettivo osservabile.
5. Applicare il minimo intervento generale; testare il percorso realmente usato.
6. Validare Windows e Linux per cambi condivisi sensibili a GPU/input/scheduling;
   aggiungere Android quando toccato. Il codice comune non elimina questi test.
7. Consegnare commit piccoli e coerenti, evidenze e limiti espliciti. Non chiudere
   issue o dichiarare una release pronta per la sola riuscita della build.

Primo problema di prodotto ancora aperto a questo snapshot: qualificare il
motivo del ritiro alpha.3/alpha.3b prima di una nuova pubblicazione. La review
Android e le ottimizzazioni AOT parallele non sostituiscono tale verifica.

## 16. Letture mirate

| Tema | Documento |
| --- | --- |
| Regole grafica | `OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md` |
| AOT prodotto, con conteggi storici da aggiornare dal manifest | `OOT3D_WHOLE_AOT_PRODUCT.md` |
| Ottimizzazione AOT indipendente | `TRIAEVUM_AOT_PARALLEL_OPTIMIZATION_HANDOVER.md` |
| Risultato esperimento piu' recente esaminato | `TRIAEVUM_AOT_RENDICONTO_2026-09-17.md` |
| Shader nativi/cache-independent | `TRIAEVUM_OOT3D_UBERSHADER_ANALYSIS.md` |
| Stuttering e metodi di misura | `TRIAEVUM_STUTTERING_RESOLUTION_STRATEGY.md` |
| Input e menu | `THREE_DS_RECOMP_INPUT_ARCHITECTURE.md`, `TRIAEVUM_F1_MENU_REVIEW.md` |
| TopScreen e regressioni storiche | `TRIAEVUM_TOPSCREEN_ISSUES_25_32_39.md`, `TRIAEVUM_TOPSCREEN_HUD_CADENCE_REVIEW.md` |
| Asset/font TopScreen | `TRIAEVUM_TOPSCREEN_INSTALLATION.md` |
| Distribuzione corrente | `TRIAEVUM_PRECOMPILED_RELEASE.md` |
| Linux | `TRIAEVUM_LINUX_PORT.md`, `TRIAEVUM_STEAM_RUNTIME_BUILD.md`, `TRIAEVUM_DESKTOP_PACKAGING.md` |
| Android | `../ports/android/README.md`, `../ports/android/INTRO.md` |
| Indice tool/input, da validare | `OOT3D_OPERATIONAL_INPUTS.md` |
| Percorsi di codice tradotto, savestate, scenari (mappa operativa) | `OOT3D_WORKSPACE_PATHS.md` + `tools/oot3d/operational_inputs.json` |

Regola conclusiva: distinguere sempre **esiste nel sorgente**, **e' collegato**,
**e' selezionato**, **e' stato eseguito**, **e' corretto negli scenari provati**
ed **e' qualificato per la release**. Sono sei affermazioni diverse.
