# OOT3D Room Compilation Unit

## Scopo

La Room Compilation Unit, o RCU, e il contratto offline che trasforma una richiesta semantica Ship in un'unita di esecuzione composta esclusivamente da identita, dati e codice OOT3D nativi.

Il sorgente N64 decide **cosa** il gameplay sta richiedendo. Non fornisce asset, coordinate, tempi, setup o parametri visuali. La RCU risolve quella richiesta nei dati OOT3D e conserva le dipendenze necessarie al consumer runtime.

```text
richiesta semantica Ship/N64
    -> oot3d_semantic_route_catalog_v1
    -> setup, ingresso e room OOT3D
    -> profili ActorInit e object bank da code.bin
    -> asset catalog e callback scene-room
    -> oot3d_room_compilation_unit_v1
    -> consumer runtime OOT3D
```

La RCU non converte gli asset N64 e non effettua sostituzioni runtime di percorsi OTR. I file originali OOT3D sono referenziati tramite percorso logico, dimensione e SHA-256; i membri ZAR e le strutture ZSI sono indicizzati esplicitamente.

## Posizione

- compilatore: `tools/oot3d/oot3d_asset_tool/src/oot3d_asset_tool/room_compilation_unit.py`;
- packager/catalogo: `tools/oot3d/oot3d_asset_tool/src/oot3d_asset_tool/room_compilation_pack.py`;
- schema: `tools/oot3d/oot3d_asset_tool/schemas/oot3d_room_compilation_unit_v1.schema.json`;
- CLI: `oot3d-assets compile-room-unit`;
- wrapper operativo: `scripts/oot3d/Invoke-Oot3dRoomCompilationUnit.ps1`;
- output locali: `I:\oot3dre_work\room_compilation_units`.
- catalogo nel core O2R: `oot3d/catalog/oot3d_room_compilation_units.json`;
- payload runtime nel core O2R: `oot3d/room_units/*.json`;
- parser runtime condiviso: `tools/oot3d/runtime_common/oot3d_room_compilation_unit.*`;
- consumer Ship: `soh/soh/Enhancements/oot3d/Oot3dRoomCompilationUnitRuntime.*`.
- consumer game nativo: `tools/oot3d/native_game_runtime/oot3d_native_actor_runtime.*`.

La collocazione nel tool asset esistente e intenzionale: parser ZSI/ZAR, decoder `ActorInit`, cataloghi e test restano una sola implementazione offline. Nessuna logica di compilazione entra in `three_ds_recomp_runtime` o nel game loop.

## Autorita Dei Dati

| Ambito | Autorita |
|---|---|
| richiesta, progressione e variante semantica | scaffold gameplay N64/Ship |
| scene, setup, ingresso e room | ZSI OOT3D |
| posizione, rotazione e parametri | ZSI OOT3D |
| profilo, object id e callback attore | `code.bin` OOT3D |
| object bank e membri asset | tabella oggetti `code.bin` + ZAR OOT3D |
| collisione, camera, luci, skybox, audio e path | comandi e payload ZSI OOT3D |
| callback speciali di room | record e codice `code.bin` OOT3D |
| rendering e timing | OOT3D nativo |
| HUD, menu e logica collegata, nella fase corrente | Ship/N64 |
| emulator dump | sola validazione |

## Contenuto Dell'Unita

`oot3d_room_compilation_unit_v1` contiene:

- identita della route, scene id, stem, setup e room native;
- provenance hashata di cataloghi, `code.bin`, file ZSI/ZAR e snapshot Zelda3drecomp;
- richiesta semantica e condizioni della variante;
- setup ZSI selezionato con comandi e payload decodificati;
- ingresso locale, spawn, room iniziale e sorgente camera;
- collision header e dati scene-resource;
- callback `Init`, `Cleanup` e `PrepareDraw` del draw-config nativo;
- CMB, object list e actor list per ogni room;
- tutte le istanze attore, incluse spawn e transition actor;
- profilo `ActorInit` nativo per ogni actor id distinto;
- firme e stato dell'evidenza per callback `Init/Destroy/Update/Draw`;
- grafo di comportamento per profilo, con funzioni, firme, transizioni action,
  layout della struttura e candidati di stato iniziale ricavati dalle evidenze OOT3D;
- catalogo delle radici native a zero chiamanti diretti e binding al profilo solo
  quando la ABI tipizzata contiene il puntatore concreto della struttura `ActorInit`;
- object bank richiesti, membri ZAR e binding nel catalogo asset;
- gap distinti per composizione, semantica comportamentale e binding runtime;
- digest canonico del payload per verificare determinismo e manomissioni.

## Stati Di Chiusura

La presenza di un attore non equivale alla sua eseguibilita. Per questo lo stato e diviso:

- `composition_complete`: route, file, setup, room, entry, profili e dipendenze native sono stati compilati;
- `behavior_evidence_complete`: tutte le callback richieste hanno semantica di workflow ricostruita nello snapshot;
- `consumer_binding_required`: il runtime deve ancora dimostrare di saper istanziare e collegare i contratti compilati.

Gli stati top-level sono:

- `composition_incomplete`;
- `composition_complete_behavior_incomplete`;
- `composition_complete_behavior_complete`.

Il compilatore emette sempre l'unita quando la struttura primaria e interpretabile. I gap non diventano gate artificiali: sono dati machine-readable che il runtime e i test di copertura possono consumare.

## Object Bank Non Materializzati

La tabella oggetti di `code.bin` include percorsi che non hanno un file nel RomFS, per esempio `actor/object_os_anime.zar`. Non devono essere sostituiti con un archivio dal nome simile.

Le evidenze native `ResourceFile_GetSize` e `Object_Spawn` mostrano la regola generale:

1. il file-not-found nativo atteso conserva dimensione zero;
2. il record object bank resta valido;
3. con `archive_size == 0` non viene eseguito il setup ZAR.

La RCU rappresenta quindi questi record come `native_unmaterialized_object_bank_reference` con capability `native_zero_size_object_bank`. Questa e una semantica nativa, non un alias o un fallback.

## Evidenze Zelda3drecomp

`I:\Zelda3drecomp` e sempre in sola lettura. Il compilatore non apre direttamente il checkout: usa soltanto snapshot copiati in:

```text
tools/oot3d/decomp_support/evidence/zelda3drecomp/<snapshot-id>/
```

Lo script `import_zelda3drecomp_evidence.py`:

- legge il checkout senza modificarlo;
- verifica l'identita del `code.bin` canonico;
- copia solo CSV e corpus tipizzati selezionati;
- costruisce la copia in staging e la pubblica atomicamente solo dopo gli hash;
- include automaticamente workflow, firme, action target e layout strutturali delle
  tranche actor-workflow disponibili;
- legge per default l'albero Git del commit `HEAD`, non i file modificati nel
  worktree esterno, e accetta `--revision` per fissare un altro commit;
- importa i cataloghi root/signature/symbol delle tranche indirette mantenendo
  separati candidati, ownership dimostrata e binding runtime;
- calcola hash per ogni file;
- lega lo snapshot a revisione, hash dei file e overlay simboli; gli eventuali
  dossier Ghidra generati e non versionati sono marcati singolarmente nel manifest;
- rifiuta di mutare uno snapshot gia esistente.

In modalita revision-pinned il manifest descrive l'albero del commit come pulito e
non incorpora le modifiche WIP del checkout. Questo impedisce a un normale refresh
di acquisire una tranche semantica ancora incompleta.

## Uso

Compilazione della variante iniziale della Foresta Kokiri usando l'ultimo snapshot RCU disponibile:

```powershell
.\scripts\oot3d\Invoke-Oot3dRoomCompilationUnit.ps1
```

Acquisizione read-only delle evidenze correnti prima della compilazione:

```powershell
.\scripts\oot3d\Invoke-Oot3dRoomCompilationUnit.ps1 -RefreshEvidence
```

Compilazione riproducibile con uno snapshot specifico:

```powershell
.\scripts\oot3d\Invoke-Oot3dRoomCompilationUnit.ps1 -EvidenceSnapshot I:\oot3dre-vulkan\tools\oot3d\decomp_support\evidence\zelda3drecomp\849140697187b895
```

Lo snapshot di evidenza runtime corrente e `849140697187b895`, importato in sola lettura
dall'albero Git Zelda3drecomp
`2cec5ef08305fbe1e4fe00efaf3f2467ff5228b1`. La selezione v16 contiene 907 file
tracciati e due cataloghi derivati; nessun file WIP del worktree esterno entra nello
snapshot revision-pinned. Include il contratto UI nativo completo fino alla tranche
246, conservato senza sostituire prematuramente HUD e menu nel runtime corrente.

Per i 26 profili Kokiri la RCU chiude 99 callback lifecycle in 746 funzioni consumer
e 718 call edge, di cui 40 funzioni locali agli attori e 607 dipendenze da servizi
nativi. Le candidate prive di ownership restano indicizzate e non vengono assegnate
per euristica. La RCU contiene 566 campi strutturali e nessun gap strutturale
irrisolto; il suo payload corrente e
`7a9339d150a11dc98f78763c59be573dcc2f1e09163f7466981e102aa57ab1ba`.

La selezione v15 include inoltre il backend A32 C++ e il generatore AOT prodotti da
Zelda3drecomp. Il runtime collega tutte le 526 funzioni richieste dalla closure
Kokiri all'immagine AOT della stessa revisione. Rimane distinto il successivo
problema di esecuzione: trasporto della memoria guest, servizi host e callback. Gli
indici fisici della scena continuano a provenire dai command stream ZSI originali,
non da liste ricostruite a mano.

Evidenze riutilizzabili individuate nel checkout:

| Blocco Zelda3drecomp | Uso nella compilazione scena |
|---|---|
| `codebin_scene_room_lifecycle_*` | dispatch dei comandi, richiesta/caricamento room e installazione del command stream |
| `codebin_room_runtime_resources_*` | ownership di ZSI, CMB e CMAB della room |
| `codebin_room_scene_callback_*` | tabella esatta delle 53 configurazioni draw scena |
| `codebin_object_bank_async_*` | caricamento e residenza delle ZAR richieste dalle object list |
| `codebin_camera_workflow_*` e `codebin_camera_mode_dispatch.csv` | selezione camera, mode e dati bg-camera |
| `codebin_onepoint_cutscene_*` | coordinamento delle camere one-point/cutscene |
| `codebin_collision_at_ac_*` e `codebin_collision_oc_damage_*` | consumer collisione attori, separato dalla geometria collision ZSI |
| tranche semantici correnti | stato iniziale mappa, lookup bg-camera e path ambientali/attori |

Questi blocchi sono contratti runtime e non sostituiscono le liste native. Attori,
object bank, ingressi, path, collisione, luci e riferimenti camera/cutscene vengono
sempre indicizzati dai file OOT3D; i callback ancora non ricostruiti rimangono gap
comportamentali espliciti.

La CLI generica accetta `--route-id` e, per route con piu setup ammessi, `--setup-index`. Non contiene nomi scena o alias asset hardcoded.

## Indice Nativo Completo

Il parser segue la stessa convenzione confermata da `Scene_ExecuteCommands` in
Zelda3drecomp: i puntatori dei comandi sono relativi a `resource_base = file +
0x10`. L'inventario RomFS verificato contiene 114 ZSI scena con 254 setup e 610
ZSI room con 895 setup. Ogni room possiede esattamente lo stesso numero di setup
della scena cui appartiene; il setup `N` della room viene quindi accoppiato solo al
setup `N` della scena.

La vecchia condizione che riconosceva soltanto il primo comando `0x15` lasciava
79 scene e tutti i command stream room fuori dall'indice. Il parser riconosce ora
anche gli header nativi `0x04`, `0x08` e `0x16`, conserva il terminatore `0x14` e
decodifica le liste room di attori (`0x01`) e object bank (`0x0B`) usando conteggio
e puntatore originali. Wind, behavior, mesh header, luci room, tempo, controlli
sky/environment e audio/environment restano comandi nativi tipizzati, non valori
ricostruiti dal runtime N64.

## Prima Unita Verificata

Route:

```text
scene_entry:SCENE_ENTRY_KOKIRI_FOREST_FROM_LINKS_HOUSE:KOKIRI_FOREST_INITIAL_CHILD_DAY
```

Output corrente:

```text
I:\oot3dre_work\room_compilation_units\scene_entry_SCENE_ENTRY_KOKIRI_FOREST_FROM_LINKS_HOUSE_KOKIRI_FOREST_INITIAL_CHILD_DAY.json
```

Risultato strutturale verificato:

- scena `spot04_info.zsi`, setup 0;
- ingresso locale 3, spawn 3, room iniziale 0;
- posizione spawn `[-31, 100, 1073]` e rotazione native;
- room 0, 1 e 2 con un CMB ciascuna;
- 80 + 9 + 12 actor entry di room, lette dai command stream del setup 0;
- 104 istanze totali includendo player e transition actor;
- 26 profili `ActorInit` distinti;
- 19 object bank, di cui 18 ZAR materializzati e 1 riferimento nativo a dimensione zero;
- draw-config 4 `KokiriForest`, incluso `RoomScene_KokiriForest_PrepareDraw`;
- composizione completa e zero gap strutturali;
- 99 callback non nulle identificate: 11 con workflow semantico ricostruito e 88 con firma tipizzata ancora da chiudere a livello di workflow/runtime.
- 26 grafi di comportamento/consumer, uno per ogni profilo presente, per un totale
  aggregato di 879 funzioni, 23 transizioni action esatte e 425 campi tipizzati;
- 99 root lifecycle chiuse in 746 consumer e 718 call edge, con servizi condivisi
  distinti dalle 40 funzioni locali agli attori;
- 103 radici indirette associate tramite ABI concreta: 102 a `Player`, incluse
  locomozione, `SkelAnime`, collision response, landing e setup action, e
  `EnItem00_NativeAction_002B175C` al profilo `EnItem00`;
- nuovi layout nativi materializzati per i buffer posa Player/EnSw, i 64 record
  meteo ObjectKankyo e collider, tracking, pose e fidget table di EnKo;
- candidati iniziali `EnKarebaba_Grow` e `EnKarebaba_Idle` derivati dai target
  assegnati da `EnKarebaba_Init`, non da una scelta del consumer.

Due compilazioni consecutive con lo stesso snapshot producono lo stesso SHA-256 del file e lo stesso digest canonico interno.

## Packaging E Consumer Runtime

`Invoke-Oot3dPlayablePack.ps1` valida nuovamente digest canonico, schema e conteggi di chiusura di ogni RCU presente in `I:\oot3dre_work\room_compilation_units`. Solo le unita `composition_complete_*` vengono incluse come payload eseguibili nel core O2R. Le unita incomplete restano nell'array `excluded` del catalogo con motivo e conteggi, ma il relativo JSON non viene montato come unita runtime.

Il parser C++ condiviso:

1. ricostruisce strutture tipizzate per room, object list, istanze, profili
   `ActorInit`, grafi di comportamento e object dependency;
2. valida formato, schema, digest dichiarati, provenance `code.bin`, riferimenti incrociati e conteggi di chiusura;
3. rifiuta duplicati, riferimenti a profili o room inesistenti, layout diversi da
   `ActorInit`, transizioni fuori grafo, conteggi falsati ed entrypoint incoerenti;
4. viene usato sia dal wrapper diagnostico Ship sia dal game runtime nativo, evitando due interpretazioni divergenti della RCU.

Il consumer Ship:

1. carica e valida una sola volta il catalogo tramite `ArchiveManager` durante il refresh degli archivi;
2. confronta digest, route, scena, setup ed entrypoint tra indice, payload RCU e selezione `NativeSceneEntryProvider`;
3. espone la RCU preparata e telemetria nel `SemanticSceneEntryRuntimeStatus`.

Il game runtime nativo:

1. richiede una RCU al bootstrap e ne verifica route, scena, setup, ingresso globale/locale e SHA-256 del `code.bin`;
2. conserva l'ingresso globale come autorita e lascia al loader ZSI la risoluzione della riga locale nativa;
3. usa i profili e le istanze compilate come unica sorgente della popolazione, senza rileggere `ActorInit` dal `code.bin` durante lo spawn;
4. risolve gli object bank dal manifest della shard actor, carica e valida gli ZAR
   originali richiesti dalle room residenti e mantiene espliciti i riferimenti
   nativi non materializzati;
5. inizializza la state machine room dal contratto compilato e conserva ownership distinta per room corrente e precedente;
6. esegue soltanto callback OOT3D gia collegate e conserva le altre come gap per indirizzo, senza fallback N64.
7. seleziona dal playable pack lo shard che possiede la scena e tutte le room dichiarate dalla RCU, quindi monta gli archivi dichiarati dal pack senza inferenze sul nome della scena;
8. risolve la CMB richiesta dal record ZSI nativo e trasferisce al renderer l'ownership delle room corrente, precedente e in cleanup;
9. risolve dal manifest dello stesso scene shard i sidecar ZAR nativi e lega le CMAB dichiarate nel namespace `ROOM<n>` alla room residente, con clock nativo a 30 tick/s.
10. consuma i grafi compilati per profilo ed espone funzioni collegate e ancora
    prive di binding, transizioni, layout e candidati iniziali senza rileggere il
    checkout Zelda3drecomp durante l'esecuzione.
11. valida il catalogo delle radici indirette, espone conteggi per profilo e separa
    le radici associate ma non ancora bindate dalle candidate prive di ownership.

Il binding CMAB non consulta la RomFS durante il gameplay e non ricostruisce nomi
dal scene stem: manifest, ZAR e membro sono risorse del playable pack validate per
indice, tipo, nome e dimensione. Una room senza membri CMAB resta esplicitamente
senza animazione. Una futura room con piu CMB e binding non univoco viene rifiutata
come gap di formato, invece di scegliere un modello per euristica.

## Contratto Lifecycle Room

La RCU incorpora `room_lifecycle_contract`, costruito dalle firme e strutture
Zelda3drecomp copiate nello snapshot. Il contratto conserva indirizzo, firma e
provenienza per `Room_Init`, `Room_RequestNewRoom`, `Room_ProcessRoomRequest`,
`Room_Destroy`, cleanup attori/risorse e spawn dei transition actor.

Il consumer interpreta dal contratto:

- stati nativi idle/loading/terminal;
- massimo due room residenti, `current` e `previous`;
- uno o tre buffer in base alla presenza di transition actor;
- attori room conservati solo se globali, correnti o precedenti;
- entry transition eleggibili allo spawn quando front/back coincide con current/previous;
- istanze transition gia spawnate conservate secondo la room ownership dell'attore,
  come ogni altra istanza visitata da `Actor_CleanupRoomActors`;
- actor id di spawn mascherato con `0x1FFF` e params derivati con `index * 0x400`;
- cleanup risorse differito di un tick e completato al passaggio successivo.

Il compilatore conserva separatamente l'actor id grezzo del record e il profile id
mascherato. Questo evita di risolvere un `ActorInit` errato quando i bit alti del
transition record contengono flag.

Il digest canonico completo viene verificato offline dal packager, che produce nello stesso archive indice e payload. Il runtime lega poi il digest dichiarato dal payload a quello dell'indice; non ricalcola a ogni ingresso scena l'hash del JSON da centinaia di kilobyte.

L'assenza del catalogo rimane non bloccante per le route preesistenti. Se il catalogo e presente ma la RCU selezionata e incoerente, il bridge pubblica `unit_entrypoint_mismatch` o l'errore strutturale senza sostituire dati OOT3D con contenuti N64.

## Stato Runtime Verificato

Lo smoke OpenGL della route Kokiri iniziale riporta:

- `population_source = oot3d_room_compilation_unit`;
- 3 room, 104 istanze, 26 profili e 19 object dependency compilati;
- 80 actor entry della room iniziale e 80 istanze create, piu il player gestito dal controller nativo;
- room corrente 0, previous/pending assenti e stato di caricamento idle;
- 21 istanze di room non residenti differite come `native_room_not_resident`;
- entrambi i transition actor adiacenti istanziati con identita e params derivati dal contratto;
- 83 istanze vive: player esterno, 80 attori room e 2 transition actor;
- 18 object bank materializzati inclusi nella shard actor, caricabili dal playable
  pack, e il riferimento zero-size nativo conservato;
- sidecar `spot04.zar` originale montato dallo scene shard e
  `ROOM0\spot04_00.cmab` applicato ai cinque batch dichiarati dal modello;
- frame CMAB aggiornato dal clock `MaterialAnimation_Update@0x00373BEC`, con campi
  frame/speed/loop agli offset `0x08/0x0C/0x10` e tick rate runtime di 30 Hz;
- zero profile gap e zero letture runtime dei profili dal `code.bin`;
- snapshot `849140697187b895` condiviso dal catalogo ABI e dalla RCU;
- 26 grafi actor, 879 funzioni aggregate, 23 transizioni e 425 campi arrivano
  tipizzati al consumer; callback non implementate restano gap espliciti invece
  di essere eseguite come no-op o sostituite con comportamento N64;
- 103 funzioni sono radici indirette con ownership ABI provata (102 Player e una
  EnItem00);
  il parser runtime e la telemetria conservano separatamente questo conteggio;
- `RoomScene_KokiriForest_PrepareDraw@0x001EB3D8` compilata come operazione
  tipizzata e applicata dopo i CMAB ai materiali nativi indicati dal callback.

Lo smoke di transizione `room 0 -> room 2 -> room 0`, eseguito attraverso la
stessa API richiamata da `En_Holl`, verifica inoltre:

- shard `oot3d-scenes-overworld` scelto dal playable pack per scena e tre room;
- sorgente nativa `room:spot04_2_info.zsi` risolta dal catalogo e caricata dal relativo O2R;
- room 2 corrente e room 0 precedente dopo l'andata, quindi room 0 corrente e room 2 precedente dopo il ritorno;
- due modelli room visibili, due preparazioni e due installazioni effettive delle risorse;
- 91 attori room residenti dopo la riconciliazione, contro i 79 della sola room iniziale;
- room 0 ricostruita con una CMAB packaged e cinque batch animati, mentre room 2
  conserva count, frame e update count CMAB a zero come dichiarato dagli asset;
- due request e due completion concluse, senza sostituzioni di asset N64;
- un cleanup renderer differito concluso e nessuna risorsa rimasta pending.

La shard actor e ora derivata anche dalle dipendenze della RCU: contiene 21 ZAR,
19 binding object-bank per questa unita e nessun percorso ricostruito nel game
loop. Il consumer mantiene un refcount per ogni room proprietaria, oltre a un
riferimento permanente per le dipendenze globali. Prima di una transizione carica
e verifica integralmente i nuovi ZAR (dimensione, indice, nome, tipo, indice locale,
offset e dimensione di ogni membro); solo dopo applica il nuovo insieme residente.
Un errore lascia intatta la room precedente. Quando il refcount raggiunge zero
archive e source vengono rilasciati dal runtime, mentre il provider puo conservare
i byte immutabili nella propria cache condivisa.

Lo smoke esteso `0 -> 2 -> 1 -> 0` forza un vero cambio dell'insieme residente:
registra 18 caricamenti logici, 3 rilasci dei bank esclusivi di room 2, zero
transizioni fallite e conserva il bank 303 condiviso con il refcount derivato
dalle room correnti. Il riferimento object 197 resta residente e pronto senza
inventare uno ZAR che nel gioco nativo ha dimensione zero.

Il contratto `En_Holl` e generato direttamente dal `code.bin`: profilo ActorInit,
selettore `(params >> 6) & 7`, indice transition `params >> 10`, action table e
limiti geometrici dei modi 4 e 6 sono verificati contro istruzioni, literal e
branch ARM. Il runtime trasforma la posizione player/camera nello spazio locale
dell'attore e richiede la room front/back indicata dalla transition entry RCU.

Il consumer Ship monta lo stesso catalogo dal core O2R e dichiara 1 unita eseguibile e 1 esclusa. Il game runtime nativo usa invece direttamente il payload di sviluppo selezionato dal launcher; il passaggio al catalogo O2R non cambia il contratto tipizzato.

Il primo consumer scene-room e ora eseguibile end-to-end. Il compilatore lega
`RoomScene_KokiriForest_PrepareDraw@0x001EB3D8` all'esatto SHA-256 dei suoi 300
byte e verifica 18 literal nel `code.bin`. La RCU conserva target room/resource,
indici materiale 2/3, constant index 0, operazione PICA 2, selettore di setup e
progressione e contratto `Rand_S16Offset`. Il runtime valuta questi dati dopo il
CMAB e modifica soltanto l'alfa della costante dei batch bersaglio, conservando
RGB e materiali adiacenti. Lo smoke della room 1 applica due batch per frame e
riporta l'indirizzo callback, selettore, valore random e alfa; setup 6 rimane un
gap esplicito finche il draw parameter nativo non sara disponibile.

Il primo consumer generico delle istanze actor RCU e anch'esso eseguibile. La
risorsa `oot3d_rigid_actor_native_runtime_contract_v1` separa il decoder offline
dal runtime: il decoder verifica profilo `ActorInit`, opcode ARM del selettore,
tabella di dispatch draw, struttura dei tre helper, tabella scale e indici CMB
contro `code.bin` e ZAR originali; il runtime interpreta soltanto la definizione
tipizzata `actor id + params -> model asset id + scale`. `EnIshi` e il primo
record della famiglia, non un ramo nel renderer. Per le quattro istanze Kokiri
con `params=0x0200` il contratto seleziona
`Model/obj_isi01_model.cmb` da `zelda_field_keep.zar` e la scala float nativa
`0.1`. Lo smoke room-request le porta tutte a `active` e `render_bound`, aumenta
le visuali ambientali da 17 a 21 e riduce i callback non supportati da 62 a 58.
Raycast al pavimento, collisione cilindrica, pickup e break state restano gap
comportamentali espliciti e non sono simulati dal consumer visuale.

La stessa risorsa contiene ora anche `EnAObj`, senza modifiche al consumer C++.
Il decoder ricostruisce i 12 byte di selezione CMB e il jump table delle cinque
scale direttamente dal corpo `EnAObj_Init@0x001E0734`. Le otto istanze Kokiri
hanno tutte selettore low-byte 10: risolvono quindi il membro originale
`objects/model/kanban2_model.cmb` di `zelda_keep.zar` alla scala float32 `0.01`.
Lo smoke le porta a `active/render_bound`, aumenta le visuali ambientali da 21
a 29 e riduce i callback non supportati da 58 a 50. Collisione dinamica,
messaggi/interazione e override TEV della variante 11 restano separati e
marcati pending.

`EnItem00` usa una seconda risorsa dichiarativa perche il suo modello dipende da
una tabella di 26 stati e non da un semplice prop rigido. Il decoder verifica nel
`code.bin` profilo e callback, maschera del selettore, tabella modelli, jump table
delle scale, `shape.yOffset`, preset di visibilita delle mesh, incremento yaw e i
bit di spawn/draw; ogni CMB viene poi risolto nel relativo ZAR OOT3D originale.
Il consumer actor resta generico: applica trasformazione, filtro mesh e rotazione
dal contratto e non contiene nomi o selettori di item. Delle nove istanze Kokiri,
cinque vengono sottoposte al renderer (due rupee con mesh 1 e tre cuori con mesh
0), mentre quattro conservano il draw iniziale soppresso dal bit nativo `0x4000`.
Lo smoke porta le visuali ambientali da 29 a 38 e i callback non supportati da 50
a 41, senza fallback N64. Pickup, collisione, persistenza save e traiettoria dello
spawn lanciato restano gap comportamentali espliciti.

`EnKanban` e il terzo record `rigid_single_model`. Il decoder verifica profilo e
quattro callback, ricava la scala float32 `0.01`, la traslazione draw locale
`(0, 0, -100)`, la tabella dei modelli pezzo e le relative undici maschere dal
`code.bin`. Il cartello integro usa il CMB 30 di `zelda_keep.zar`; pezzi ed effetto
sono risolti nei dodici CMB originali di `zelda_kanban.zar`. Le otto istanze della
room iniziale vengono tutte materializzate dal medesimo consumer e portano le
visuali ambientali da 38 a 46, riducendo i callback non supportati da 41 a 33.
Taglio, fisica dei pezzi, collisione, messaggi e floor check restano gap separati,
senza simulazioni nel contratto visuale.

`EnGs` e il quarto record della stessa famiglia. Il decoder verifica il profilo
`0x01B9`, il caricamento CMB nel campo handle `+0x270`, il percorso draw fino al
submit nativo e la InitChain che imposta la scala `0.1`. Le tre istanze Kokiri
risolvono quindi `Model/gossip_stone2_model.cmb` direttamente da
`zelda_gs.zar`. Animazione materiale Torch, collisione, dialogo e reazioni ai
colpi restano contratti comportamentali separati e dichiarati pending.

Quando un consumer nativo prende in carico un'istanza, il runtime rimuove ora
la preview CMB selezionata dall'asset graph per la stessa coppia actor/entry.
Lo smoke `0 -> 1` produce quindi tre attori `EnGs`, tre visuali runtime e nessun
doppione; le preview restano disponibili solo per le famiglie ancora prive di
un contratto eseguibile.

L'aumento degli attori ha inoltre esposto modelli interamente fuori vista che il
backend OpenGL continuava a sottoporre. Il renderer applica ora un test
conservativo dell'AABB locale trasformata contro i sei piani omogenei del
frustum: un modello viene escluso solo quando tutti gli otto vertici sono fuori
dallo stesso piano. La prova iniziale Kokiri mantiene attivi gli otto `EnKanban`,
ma evita i loro 24 draw fuori vista in tre frame; stanza e Link restano invariati.
I contatori per modello rendono il comportamento verificabile senza regole per
actor id o nomi asset.

## Prossimo Incremento

Il lifecycle, il trigger fisico `En_Holl` per i modi room-request 4/6 e i
consumer CMB/CMAB corrente e precedente sono presenti. I prossimi blocchi sono:

1. collegare `EnKarebaba` mediante il grafo RCU gia completo (15 funzioni, 23
   transizioni e due candidati iniziali), aggiungendo al consumer soltanto le
   primitive native dimostrate dai corpi funzione e dagli asset OOT3D;
2. completare i modi `En_Holl` 0/1/2/3/5 e il relativo eventuale draw soltanto dopo averne chiuso il contratto da `code.bin`;
3. verificare interattivamente il percorso fisico `0 -> 2 -> 0`, inclusi camera, collisione e rilascio delle risorse non piu residenti;
4. ampliare i binding callback per famiglie, mantenendo ogni indirizzo non supportato esplicito;
5. integrare il consumer nel loop gameplay Ship mantenendo HUD e menu N64 separati dal world runtime OOT3D.

La stessa interfaccia deve funzionare per ogni scena, ingresso e cutscene; Kokiri e soltanto la prima fixture completa.
