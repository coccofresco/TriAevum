# OOT3D Typed Gameplay Conversion

## Obiettivo

Il corpus chiamato finora "mass C++" e una traduzione A32 semantica: elimina
l'interpretazione istruzione per istruzione per molti blocchi, ma continua a
esporre registri ARM, memoria guest e flag CPU. Non e ancora sorgente gameplay.

Il percorso tipizzato sostituisce progressivamente interi confini ABI con:

- strutture C++ a layout verificato per i dati OOT3D;
- funzioni gameplay C++ con parametri e risultati tipizzati;
- tempo frazionario esplicito, non letto da offset globali nelle funzioni pure;
- un bridge A32 limitato a marshalling, validazione memoria e ritorno AAPCS32;
- mass-AOT mantenuto come fallback e oracolo differenziale.

L'autorita resta OOT3D: `code.bin`, layout estratti e comportamento osservato
dal codice ARM. Il sorgente N64 puo chiarire il ruolo semantico di una funzione,
ma non determina layout, costanti, timing o implementazione 3DS.

## Direzione Di Produzione

Il percorso di produzione e il gameplay C++ tipizzato. Mass-AOT, whole-AOT e
interprete A32 sono strumenti transitori per bootstrap, confronto differenziale
e copertura non ancora migrata; non sono backend da ottimizzare come soluzione
finale. Ogni milestone deve ridurre quantitativamente il lavoro A32 residuo.

La run Kokiri pre-HUD del 21 luglio 2026 rende il problema misurabile: 300 frame
di presentazione richiedono 19,16 secondi, con 13,42 secondi nel guest, 102,87
milioni di blocchi mass-AOT, 2,07 milioni di ingressi mass-AOT e 2,03 milioni di
uscite di regione. Il renderer, l'audio host e la presentazione non spiegano il
costo dominante. Le 48 leaf tipizzate effettuano 386.093 chiamate, ma ciascuna
continua a spezzare una regione A32 e quindi non costituisce ancora un grafo
gameplay nativo.

L'unita di conversione minima e un grafo chiamante completo che possiede:

- stato C++ tipizzato e lifetime degli oggetti;
- chiamate dirette fra update, matematica, animazione e collisione convertiti;
- ordine di update ed eventi osservabili;
- accesso a renderer, audio, input e filesystem tramite servizi host tipizzati;
- serializzazione e confronto differenziale del proprio stato.

Non si promuovono altre leaf isolate per ottenere benchmark migliori. Una prova
con nove servizi C++ gia presenti ha ridotto il guest del 18%, ma ha aumentato
le transizioni del dispatcher da 2,07 a 3,65 milioni e ha cambiato
l'interleaving guest, perche il vecchio scheduler contabilizza i blocchi ARM.
La modifica e stata scartata: conferma che scheduling e chiamante devono migrare
insieme. Per i confronti futuri il report distingue la firma dei byte guest
(`memory_content_fingerprint`) dalla firma completa che include anche il
contatore delle scritture (`memory_state_fingerprint`).

## Architettura

`tools/oot3d/typed_gameplay/` contiene codice indipendente da ARM, renderer,
audio e ThreeDsRecomp:

- `oot3d_gameplay_types.h`: wire layout C++ di `Actor`, `ActorShape`,
  `CollisionCheckInfo`, `SkelAnime`, puntatori guest a 32 bit e assert di
  dimensione/offset;
- `oot3d_gameplay_time.*`: contratto `seconds` + `nativeUpdateRate` continuo;
- `oot3d_gameplay_math.*`: interpolazione della LUT angolare, step, approach e
  smooth-step con rate esplicito;
- `oot3d_gameplay_actor.*`: stati di dominio `ActorKinematics` e
  `ActorLifecycleState`, movimento, ownership, kill e scala attore;
- `oot3d_gameplay_player.*`: layout wire sparso verificato e stati di dominio
  per equipaggiamento, cutscene, idle, lock-on, altezza e moto verticale in
  acqua;
- `oot3d_gameplay_animation.*`: cambio animazione, selezione dell'update mode,
  morph e piano esplicito degli effetti posa;
- `oot3d_gameplay_skel_anime.*`: avanzamento frame per tutti i mode nativi e
  consumo frazionario del morph.

`oot3d_typed_gameplay_bridge.*` e il solo adattatore verso `GuestState` e
`NativeA32Memory`. Carica LUT e costanti dai literal originali mappati dal
`code.bin`; non incorpora copie sostitutive degli asset o parametri scelti dal
runtime. Gli oggetti guest vengono copiati in valori C++ standard-layout e
tradotti nello stato gameplay minimo; sono riscritti solo i campi modificati,
senza reinterpretare memoria guest come puntatori host. Le funzioni pure non
ricevono quindi ne registri ARM ne l'intero oggetto wire `Actor`.

Il dispatcher transitorio prova il gameplay tipizzato prima del mass-AOT. I suoi entrypoint
sono esclusi dalle regioni tail-chained del mass-AOT: quando una regione AOT
raggiunge un confine convertito torna al dispatcher, esegue la funzione C++ e
puo poi rientrare nel fallback AOT. Questo rende effettiva la conversione anche
per chiamate annidate, non solo quando una funzione e l'entrypoint iniziale.

## Copertura Corrente

| Entry | Funzione | Stato |
|---|---|---|
| `0x002BB29C` | decremento morph interno direct pose helper | C++ tipizzato, poi tail posa nativa |
| `0x002BB3B4` | decremento morph interno legacy pose helper | C++ tipizzato, poi tail posa nativa |
| `0x002CFCA0` | `oot3d_sin_idx8` | C++ tipizzato, LUT nativa |
| `0x00320D28` | `SkelAnime_SetUpdate` | C++ tipizzato |
| `0x003279DC` | `Player_GetExplosiveHeld` | C++ tipizzato, item action nativa |
| `0x00330D5C` | `Player_SetCsAction` | C++ tipizzato, callback guest conservata |
| `0x00338F60` | `oot3d_cos_idx8` | C++ tipizzato, LUT nativa |
| `0x0033BD9C` | `Actor_UpdatePosWithVelocityFromRotation` | C++ tipizzato, assi X/Y nativi |
| `0x003404A8` | `LinkAnimation_PlayOnceWithSpeed` | C++ tipizzato, CSAB nativo |
| `0x0034807C` | `ZAR_GetCSABByIndex` | C++ tipizzato, catalogo ZAR nativo |
| `0x00349574` | `Player_UpdateHostileLockOn` | C++ tipizzato, target Actor nativo |
| `0x0034B17C` | `Player_UpdateSwimVerticalVelocity` | C++ tipizzato, profondita e literal nativi |
| `0x0034D4B0` | query hookshot senza attore trattenuto | C++ tipizzato |
| `0x0034D628` | `Player_GetIdleAnim` | C++ tipizzato, tabella `code.bin` nativa |
| `0x0034DD2C` | `Player_IsItemInHand` | C++ tipizzato |
| `0x003529D4` | `Math_StepToAngleS` | C++ tipizzato, percorso corto e wrap nativi |
| `0x0035302C` | `Animation_Change` | C++ tipizzato, effetti posa nativi |
| `0x00355A60` | `Player_HoldsHookshot` | C++ tipizzato |
| `0x00358DFC` | `LinkAnimation_PlayLoopSetSpeed` | C++ tipizzato, CSAB nativo |
| `0x00359AA0` | `LinkAnimation_PlayOnce` | C++ tipizzato, CSAB nativo |
| `0x0035AF04` | `Player_SetRuntimeFlag200` | C++ tipizzato |
| `0x0035D260` | `Player_HoldsTwoHandedWeapon` | C++ tipizzato |
| `0x0035FB14` | `Actor_UpdateVelocityXZGravity` | C++ tipizzato, LUT e gravita native |
| `0x00360190` | `LinkAnimation_Change` | C++ tipizzato, code posa nativo |
| `0x003604F0` | `LinkAnimation_PlayLoop` | C++ tipizzato, CSAB nativo |
| `0x00365860` | `Actor_UpdateVelocityXYZ` | C++ tipizzato, LUT nativa |
| `0x00367EF0` | `Player_GetHeight` | C++ tipizzato, stato e literal nativi |
| `0x0036A7A0` | `Player_InCsMode` | C++ tipizzato, `PlayState` e mask native |
| `0x0036B1E0` | `LinkAnimation_OnFrame` | C++ tipizzato, quantizzazione nativa |
| `0x0036B4EC` | `SkelAnime_Update` | C++ tipizzato per mode `0..8`, effetti posa nativi |
| `0x0036B96C` | `Actor_UpdatePos` | C++ tipizzato |
| `0x0036C940` | `Actor_HasNoParent` | C++ tipizzato |
| `0x0036E168` | `Math_SmoothStepToF` | C++ tipizzato |
| `0x0036E5E0` | `SkelAnime_IsFrameCrossed` | C++ tipizzato, scala temporale `float` |
| `0x0036E980` | `Player_SetCsActionWithHaltedActors` | C++ tipizzato, 130 chiamanti nativi |
| `0x0036FC20` | `Math_ApproachZeroF` | C++ tipizzato |
| `0x00370084` | `Math_SmoothStepToSUpdateRate` | C++ tipizzato |
| `0x00370378` | `Math_ScaledStepToS` | C++ tipizzato |
| `0x003705A0` | `Math_StepToF` | C++ tipizzato |
| `0x00371E40` | `Actor_HasParent` | C++ tipizzato |
| `0x00372AA8` | `Math_StepToS` | C++ tipizzato, rounding e wrap nativi |
| `0x00373500` | `Math_ApproachF` | C++ tipizzato |
| `0x003736FC` | `Animation_OnFrameImpl` | C++ tipizzato, scala temporale `float` |
| `0x00374428` | `Actor_Kill` | C++ tipizzato |
| `0x0037572C` | `Actor_SetScale` | C++ tipizzato |
| `0x00375A18` | `Math_SmoothStepToS` | C++ tipizzato |
| `0x00376864` | `Actor_MoveForward` | C++ tipizzato |
| `0x003FE340` | `Animation_GetLength` | C++ tipizzato, header CSAB nativo |

`SkelAnimeClock`, i due pose helper, il controllo frame di Link e i due leaf
generali di frame crossing consumano ora scale temporali frazionarie. La catena
`LinkAnimation_Play* -> LinkAnimation_Change` risolve lunghezza e indice dai
CSAB OOT3D e conserva le code native per campionamento/copia posa.
`SkelAnime_Update` decide in C++ frame, loop/once, completamento e morph per
tutti i nove mode osservati. Le primitive Actor coprono moto XZ con gravita,
moto XYZ dagli assi nativi X/Y e integrazione posizione con displacement di
collisione. Campionamento CSAB, copia delle matrici e blend del model handle
restano tail espliciti nel codice OOT3D verificato: non sono sostituzioni di
asset e non duplicano il decoder di posa nel gameplay tipizzato.

Il primo dominio Player copre tredici entrypoint completi. Il layout wire
mantiene opache le aree ancora sconosciute ma verifica con `static_assert` gli
offset osservati fino a `0x29BC`; il bridge legge soltanto i campi necessari.
Le tabelle idle, i puntatori globali, le mask e tutti i coefficienti del moto
in acqua sono risolti dal `code.bin`. Il confronto differenziale ha inoltre
corretto l'identita del campo `Actor+0x88`: e la profondita in acqua, non
l'altezza del pavimento.

Il rate logico A32 generale resta intenzionalmente limitato ai valori interi
rappresentabili dal campo guest: 30 e 60 Hz. Il contesto typed supporta rate
frazionari, ma il gate globale verra aperto solo dopo la conversione dei
chiamanti gameplay che leggono ancora direttamente `0x0051B2F4 + 0x110`.

## Build Iterativa ThinLTO

Il whole-AOT PGO rimane il fallback transitorio veloce, ma il suo archivio
contiene bitcode: senza cache ogni modifica al runtime forza `lld-link` a
ripetere ThinLTO sull'intero corpus. Le build iterative devono quindi impostare
una cache persistente esterna al checkout:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' -S . -B <build> `
  "-DOOT3D_WHOLE_AOT_GENERATED_DIR=<generated-source>" `
  "-DOOT3D_WHOLE_AOT_PREBUILT_LIBRARY=<pgo-bitcode-library>" `
  "-DOOT3D_WHOLE_AOT_THINLTO_CACHE_DIR=<persistent-cache>"
```

Il primo link popola la cache ed e intenzionalmente costoso. I link successivi
riusano gli oggetti ThinLTO finche non cambia il bitcode whole-AOT; modifiche a
bridge, runtime o gameplay tipizzato non devono invalidarlo. La verifica del 21
luglio 2026 ha ridotto un relink da 613 a 4,85 secondi. Il binario cache-hit ha
prodotto lo stesso framebuffer bit per bit e 92,22 FPS sulla sequenza
`navi_kokiri_main_forest` con Vulkan, audio, simulazione nativa a 30 Hz e
presentazione interpolata libera. Un archivio materializzato senza ThinLTO e
bit-identico ma scende a circa 31 FPS: puo servire per diagnosi, non e il
fallback di produzione.

## Verifica

`oot3d_typed_gameplay_tests` esegue lo stesso stato iniziale nel nuovo C++ e
nell'interprete ARM del `code.bin`, poi richiede uguaglianza bit per bit dei
campi posizione/velocita a update-rate `2` e `1`. Lo stesso confronto copre i
rami positivi, negativi, clamp, overshoot e wrap delle primitive matematiche,
entrambi gli stati di parent ownership, lifecycle kill e scala via registro
VFP. Le primitive Actor vengono confrontate anche sugli assi di rotazione
distinti. Il confronto di animazione copre i mode `0..8` a 30 e 60 Hz,
terminali, reverse playback, frame crossing con scale fino a `0.25`, taper
positivo/negativo, morph attivo/clamp/inattivo, wrapper `LinkAnimation_Play*` e
tutti i rami di `Animation_Change`/`LinkAnimation_Change`. Verifica inoltre
entry e argomenti dei side effect posa, layout, ABI preservata, catalogo
entrypoint, fallback non distruttivo e la composizione di due update morph a
120 Hz in un update a 60 Hz.

`oot3d_typed_player_gameplay_tests` confronta separatamente i tredici confini
Player con l'ARM sugli stessi byte guest. Copre item action, entrambi i setter
cutscene, acquisizione/rilascio lock-on, rami di galleggiamento e affondamento,
selezione idle, altezza e tutte le cause osservate di cutscene mode.

`oot3d_native_mass_aot_tests` impedisce che un entrypoint convertito rientri per
errore nelle regioni mass-AOT, cosa che renderebbe il nuovo codice inattivo.
Le statistiche runtime distinguono chiamate tipizzate, categorie gameplay,
fallback conservati e tempo campionato dal resto del compiled path. Con i 48
confini correnti l'inventario e `safe_entries=138850` e
`excluded_boundaries=644`.

## Procedura Di Conversione

Per ogni nuovo grafo:

1. fissare entrypoint, ABI e layout dai dati OOT3D;
2. recuperare dal `code.bin` literal, tabelle e globali realmente letti;
3. definire o estendere wire layout con `static_assert` su offset e dimensione;
4. scrivere una funzione C++ pura che non conosca registri o indirizzi guest;
5. aggiungere un bridge minimo e l'entrypoint al catalogo;
6. confrontare C++ e ARM sullo stesso stato, includendo rami limite;
7. lasciare il fallback attivo se un side effect non e ancora modellato;
8. convertire i chiamanti solo dopo che tutti i callee osservabili del grafo
   hanno un contratto tipizzato o un confine di fallback esplicito.

## Prossimi Grafi

L'ordine operativo e:

1. importare dal sorgente decompilato fissato un `GameState/PlayState` C++ con
   ownership del tick, senza `GuestState` nel percorso tipizzato;
2. convertire insieme chiamanti Player di movimento, orientamento, root motion
   e collisione, usando direttamente le primitive gia tipizzate;
3. convertire actor lifecycle, liste `PlayState` e update degli attori Kokiri;
4. spostare cutscene, camera e environment update su contesti tipizzati;
5. collegare renderer, audio e servizi CTR tramite API host, non entrypoint ARM;
6. ridurre il fallback per grafi interi fino a rimuovere mass-AOT dal prodotto.

La percentuale di istruzioni tradotte non e una metrica di completamento. La
metrica utile e la quota di grafi gameplay completi che attraversa il
dispatcher tipizzato senza rientrare in A32.
