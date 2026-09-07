# Strategia OOT3D Nativo Single-Screen 16:9 A 60 FPS

## Obiettivo

Realizzare un'applicazione PC nativa e giocabile dall'inizio alla fine che esegua
il codice e consumi i dati originali di Ocarina of Time 3D, usando il backend
Vulkan e l'audio OOT3D gia sviluppati. La presentazione deve usare un solo
schermo 16:9 e produrre 60 frame visivamente distinti al secondo. HUD, menu e
relative meccaniche seguono il contratto single-screen ibrido definito sotto;
tutto il gameplay resta OOT3D.

Il codice N64 non deve fornire progressione, attori, player controller,
collisioni, camere, cutscene, clock, scene, asset o fallback gameplay. Il suo
unico dominio runtime e la parte di comportamento, layout e grafica UI
espressamente assegnata dalla policy single-screen attraverso il contratto UI
OOT3D gia ricostruito in `Zelda3drecomp`.

### Policy UI Normativa

Questa policy prevale sulle sezioni storiche datate presenti in fondo al
documento:

- File Select, Name Entry, opzioni e ogni altro menu fuori dal gameplay restano
  quelli originali OOT3D. Il framebuffer inferiore viene ricollocato sul solo
  schermo 16:9 superiore, omettendo esclusivamente lo sfondo autonomo quando e
  una risorsa separabile, cosi il cielo superiore rimane visibile;
- mouse e touch usano l'inversa della stessa trasformazione di presentazione:
  il gioco riceve coordinate native inferiori `320x240` ed esegue il proprio
  hit-test originale. Fuori dal rettangolo presentato il touch e rilasciato;
- i dialoghi e la HUD direttamente collegata ai dialoghi possono restare OOT3D;
- la HUD in-game riproduce layout e comportamento N64, ma usa esclusivamente
  elementi grafici originali OOT3D ricollocati; nessun asset grafico HUD N64;
- pausa, mappa e inventario usano comportamento N64 e sfondi/tabelle N64, ma
  icone, mappe, equipaggiamento e contenuti sono OOT3D. Quando N64 usa un
  elemento 2D e OOT3D l'omologo 3D, viene renderizzato l'elemento 3D OOT3D.

La rete e esplicitamente fuori scope. Il prodotto corrente e solo locale:
netplay, matchmaking, servizi online, sincronizzazione remota e sostituzioni
host dei daemon di rete OOT3D non fanno parte delle milestone, dei criteri di
completezza o dell'architettura runtime. Eventuali dipendenze di rete ereditate
dall'upstream non devono essere usate dal target `oot3d_native_game`.

## Decisione Architetturale

Il prodotto resta il target dedicato `oot3d_native_game`, ma cambia il proprietario
del game loop:

```text
codice C/C++ OOT3D decompilato e mantenuto
    -> compilazione host nativa
    -> processo OOT3D persistente
       -> gameplay, progressione, scene, attori, collisioni, camera, cutscene
       -> caricamento degli asset nativi OOT3D
       -> comandi grafici PICA
       -> comandi audio/DSP

residui code.bin esplicitamente inventariati
    -> fallback A32 temporaneo, mai percorso di produzione implicito

host PC nativo
    -> servizi 3DS strettamente necessari, derivati dove utile da Azahar
    -> frontend PICA -> backend Vulkan OOT3D esistente
    -> backend audio OOT3D esistente
    -> input PC/HID/IMU virtuale, filesystem, salvataggi, finestra e scheduler
    -> runtime UI single-screen conforme alla policy -> pass Vulkan 16:9
```

Non si completa piu il gioco implementando a mano un attore OOT3D alla volta nel
runtime demo. La copertura A32 rende eseguibile il codice originale; il lavoro
principale diventa fornire al processo ricompilato memoria, servizi, GPU, audio,
input e UI host corretti.

## Precedenza Sui Piani Precedenti

Questo documento sostituisce, per l'architettura di produzione:

- l'uso del gameplay Ship/N64 come impalcatura runtime in
  `OOT3D_SEMANTIC_REHOSTING_PLAN.md`;
- la regola "N64 solo come evidenza semantica offline" in
  `OOT3D_CURRENT.md`, limitatamente alla UI, che ora ha un
  contratto di sostituzione esplicito;
- l'obiettivo di rendere `soh.exe` il prodotto finale in
  `OOT3D_PLAYABLE_SHIP_ASSET_STRATEGY.md`;
- l'uso di OpenGL come unico backend di produzione.

Restano validi i parser, il renderer, l'audio, gli indici, la Room Compilation
Unit, i test e le evidenze gia prodotti. Le demo attuali restano fixture e
strumenti di confronto, non il game loop finale.

## Base Tecnica Disponibile

### Codice OOT3D

La snapshot attualmente importata non fornisce ancora tutto il gioco come
sorgente host compilabile. I 794.924 slot raggiungibili del generatore indicano
copertura di istruzioni ARM predecodificate, non una decompilazione C/C++: il
relativo `PackedOp` runtime resta un interprete e non deve essere confuso con
codice host nativo. Il materiale locale mantiene 204 funzioni exact-match; una
parte e C strutturato e una parte conserva assembly ARM inline. Il pseudocodice
Ghidra restante e evidenza di decompilazione, non input diretto del compilatore.

Il percorso di produzione deve quindi aumentare la copertura del sorgente
host mantenuto e sostituire per funzione il fallback A32. La decompilazione
tipizzata e essenziale per:

- definire ABI, strutture e proprieta dei sottosistemi;
- individuare i confini host e i servizi da sostituire;
- compilare le funzioni calde come codice host senza cambiare comportamento;
- applicare in modo controllato 16:9, 60 Hz e UI N64.

### Rendering E Audio

Il branch corrente possiede gia:

- geometria e texture CMB native nel backend Vulkan;
- trasformazioni, materiali PICA, TEV, fog, vertex lighting e Shadow2D;
- applicazione delle matrici limb OOT3D agli attori;
- caricamento, sequenze, DSP, resampling ed effetti dell'audio OOT3D;
- una corsia runtime gia misurata sopra i 60 FPS nella scena corrente.

Questi sistemi diventano backend del processo ricompilato. Non devono continuare
a possedere un secondo stato di scena o di attore parallelo a quello di OOT3D.
Non sono tuttavia considerati completi o autoritativi: nuove evidenze nei formati,
nel codice decompilato, nei command buffer PICA o nel percorso DSP devono
correggerli ed estenderli. Il lavoro gia svolto e una base da conservare, non un
vincolo che puo prevalere sul comportamento originale.

### Contratto UI

`Zelda3drecomp/docs/oot3d_ui_substitution_contract.md` e il confine normativo.
Il contratto gia distingue, per dieci sottosistemi, ownership di contenuto,
meccaniche, input, presentazione e lifecycle. Espone:

- `Oot3dUiSemanticState` e `UiBackendStateView` come viste read-only;
- proiezioni tipizzate per HUD, inventario, pausa, stato persistente, file
  select e name entry;
- `UiBackendIntent` per la navigazione host;
- nove `UiBackendNativeActionRequest` per le mutazioni ammesse;
- tre `UiFrontendNativeActionRequest` per copy/delete/load del salvataggio;
- query native e operazioni interne che non possono essere sostituite;
- 63 contratti funzione primari e una closure nativa di 264 funzioni e 1.348
  archi, classificati per workflow, responsabilita e handoff trattenuti.

Il runtime UI N64 deve implementare questo contratto dentro la stessa applicazione
e negli stessi lifecycle del gioco. Non legge offset guest, non possiede un
secondo SaveContext e non scrive direttamente nella memoria OOT3D.

### Stato Di Implementazione

Il primo incremento del processo persistente e ora presente nel target dedicato
`oot3d_native_a32_process`:

- `NativeA32Memory` monta regioni nominate, zero-fill BSS-like, immagini
  iniziali e permessi write/execute, mantenendo atomiche ed esclusivi A32;
- `NativeA32Process` conserva registri, VFP/FPSCR, memoria, stack e TLS fra
  dispatch, SVC e sospensioni;
- SVC e fallback attraversano un'interfaccia host tipizzata con esiti
  resume/wait/terminate/fault; un missing block non gestito fallisce chiuso;
- `oot3d_native_a32_execution` usa lo stesso address space senza cambiare la sua
  API callback-oriented, cosi le closure OOT3D gia integrate non regrediscono;
- i test sintetici coprono permessi, zero-fill, esclusivi, stack/TLS,
  persistenza wait/resume e confine fallback; il test AOT esistente continua a
  eseguire le funzioni RNG originali.

Il secondo incremento monta ora il processo originale e ne esegue davvero il
bootstrap:

- l'ExHeader genera un manifest riproducibile per text, rodata, data/BSS, stack,
  TLS, Config Memory, Shared Page, heap e resource limit CTR;
- il generatore AOT include l'entrypoint, i corpi funzione discontinui raggiunti
  dal control flow e le target entry delle tabelle native a offset relativo;
- un inventario supplementare revisionabile conserva le entry native scoperte
  da esecuzione o disassembly senza modificare la snapshot in sola lettura di
  `Zelda3drecomp`; il primo caso chiuso e la funzione leaf a `0x004350F8`;
- il boot originale ha completato static initializer, interrogazione dei
  resource limit, allocazione dei 64 MiB applicativi e inizializzazione `srv:`;
- il kernel host implementa i soli SVC e comandi IPC effettivamente osservati,
  con handle tipizzati e memoria guest reale;
- lo scheduler cooperativo esegue il main thread e i worker OOT3D secondo le
  priorita CTR, con registri, stack e slot TLS indipendenti e stabili;
- mutex, semafori, eventi, thread handle e `WaitSynchronizationN` condividono
  oggetti di sincronizzazione separati dalla vita dei singoli handle;
- `srv:` e `APT:U` completano registrazione, notification setup, lock,
  inizializzazione e wakeup iniziale usando i command buffer originali;
- `GetSystemTick` legge un clock CTR virtuale controllato dall'application host,
  pronto per essere avanzato dal tick logico senza dipendere dai frame render;
- `fs:USER` apre il SelfNCCH RomFS con il low path binario originale; il manifest
  distingue l'immagine NCCH/IVFC estratta dalla vista file Level-3 esposta dal
  servizio e valida l'offset sorgente `0x1000`, senza modificare i dati;
- il loader OOT3D legge header e tabelle Level-3 dal RomFS originale attraverso
  i propri comandi IPC, senza asset sostitutivi o una scena host parallela;
- address arbiter e worker aggiuntivi permettono al bootstrap di completare il
  wake-up APT e la transizione applicativa nativa;
- `gsp::Gpu` acquisisce il diritto GPU, registra la relay queue e restituisce la
  shared memory GSP da 4 KiB, poi mappata dal processo a `0x08081000` tramite
  `svcMapMemoryBlock`;
- il modulo isolato `oot3d_native_pica_frontend` applica `WriteHWRegs`, scritture
  mascherate e command queue GSP a stato backend-neutral con provenienza;
- i submit originali vengono decodificati in scritture ai 0x300 registri PICA,
  conservando mask, valore finale, offset nel command list e stati sconosciuti;
- il processo raggiunge l'attesa dell'interrupt GPU dopo i primi submit;
- i command list senza draw applicano i valori iniziali PICA, IRQ compare e
  autostop, producendo P3D nella relay queue e risvegliando il worker tramite
  wait kernel reale; il bootstrap supera ora il primo submit;
- `oot3d_native_pica_transfer` decodifica ed esegue il primo display transfer
  VRAM -> framebuffer con formati, tiling, crop, flip e scale CTR tipizzati; PPF
  viene consegnato soltanto dopo il completamento riuscito;
- `dsp::DSP` carica e conserva il componente firmware originale, registra gli
  event guest, espone semaforo e mask, e modella le otto pipe CTR;
- la pipe audio implementa il protocollo di inizializzazione HLE verificato in
  Azahar, restituisce le 15 strutture DSP e segnala l'interrupt registrato;
- la RAM DSP da `0x1FF00000` e mappata dal manifest; le conversioni word-address
  espongono entrambe le regioni condivise a `0x1FF5...` e `0x1FF7...`;
- `GetHeadphoneStatus` dipende dalla route audio host configurata e non da uno
  stato gameplay artificiale;
- `cfg:u` espone lingua, regione, audio e calibrazione stereo dai parametri host;
  `hid:USER` monta la shared memory e completa setup di pad, touch,
  accelerometro e giroscopio con calibrazione CTR tipizzata;
- `ndm:u` e limitato allo shim suspend/resume richiesto durante il bootstrap,
  sulla base dell'ABI CTR verificata in Azahar; rete e daemon restano fuori
  scope;
- il bootstrap non incontra piu fallback o servizi mancanti: dopo 1.017
  transizioni host e 6.590 scritture PICA, il main thread entra nel primo
  `nngxWaitVSync` mentre i worker sono correttamente quiescenti;
- l'adapter asincrono Vulkan e il mixer audio finale restano da collegare agli
  stessi contratti prima della produzione;
- PDC0/PDC1 entrano ora nella relay queue GSP registrata dal gioco a 60 Hz,
  senza risvegliare direttamente funzioni gameplay: due refresh fanno avanzare
  il main loop a 12 comandi GSP, 10.331 scritture PICA e 5 draw originali;
- `FS::OpenArchive(SaveData)` monta un archivio persistente OOT3D confinato
  nella directory dati utente, mantenendo l'handle archivio nativo a 64 bit;
- i cinque draw raggiunti conservano lo snapshot completo dei 0x300 registri
  PICA, inclusi layout vertex/index, programmi vertex, texture, TEV e stato
  framebuffer; non sono draw Fast3D o materiali ricostruiti dalla demo;
- il frontend conserva inoltre lo stato non leggibile dal solo register file:
  programmi e swizzle VS/GS caricati tramite porte autoincrementali e uniform
  bool, int, float24/float32; ogni draw ne riceve uno snapshot immutabile;
- `oot3d_native_pica_draw_state` decodifica in strutture backend-neutral
  framebuffer, texture, vertex/index loader, topologia e interfaccia shader;
  una fixture ricavata dal primo draw nativo verifica anche gli indirizzi fisici
  `0x18000000`, `0x20670210` e `0x206702C0` senza profili del gioco;
- `oot3d_native_pica_submission` traduce le regioni fisiche FCRAM/VRAM nella
  memoria guest e snapshotta indici, loader vertex e texture al confine draw,
  assegnando un ID monotono consumabile in modo asincrono dal renderer;
- una run originale a quattro VBlank converte tutti i cinque draw in submission:
  ciascuna contiene 512 word programma VS, 54 swizzle e indici 0..3; i draw
  testurizzati catturano 8.280 byte e quelli non testurizzati 120 byte, senza
  fallback, risorse fuori mappa o asset sostitutivi;
- il decompiler vertex PICA di Azahar e nihstro vive in un modulo GPL/BSD
  isolato e revisionato, non nel fork MIT di Libultraship; sui cinque draw reali
  genera una sola sorgente cacheabile da 27.945 byte, chiave
  `0x0BD08516F28D3F5D`, compilata con successo in SPIR-V Vulkan da shaderc;
- il generatore fragment backend-neutral interpreta i sei stadi TEV PICA,
  sorgenti/modificatori/operazioni colore e alpha, scale, combiner buffer,
  alpha test, routing delle coordinate texture e `Projection2D` direttamente
  dagli snapshot nativi; la stessa run produce due chiavi fragment distinte,
  compila entrambi gli shader in SPIR-V Vulkan e riusa la cache per i tre draw
  successivi, senza fallback o profili specifici del gioco;
- lo stato pipeline tipizzato comprende ora viewport/depth range PICA,
  scissor include/exclude, culling, topologia, fragment mode, write mask,
  logic-op, blend e depth/stencil completi; la run reale identifica cinque
  triangle strip con viewport nativa 240x400, culling counter-clockwise e write
  RGBA, applicando depth test/write soltanto al primo e all'ultimo draw;
- il frontend conserva anche i valori float24 degli attributi vertex
  fixed/default caricati dalle porte autoincrementali `0x232..0x235`; ogni draw
  reale usa sei attributi default `(0,0,0,1)` con il mapping input-register
  originale, quindi il consumer Vulkan non deve sintetizzare costanti mancanti;
- `oot3d_native_pica_vulkan_plan` converte ogni submission in un piano
  immutabile con shader e uniform, binding vertex raw, binding fixed
  per-instance, sedici attributi mappati, indici/base-vertex e texture native;
  tutti i cinque draw reali producono piani completi con tre binding vertex e
  nessuna lettura differita dalla memoria guest;
- il servizio CTR espone un completion PICA tipizzato: P3D percorre la relay
  queue GSP e risveglia le attese native, ma viene emesso soltanto dal consumer
  GPU dopo una submission riuscita;
- `oot3d_native_game --a32-process-manifest` esegue ora il processo originale
  persistente nello stesso application loop del backend Vulkan dedicato: avanza
  il clock CTR, consegna PDC0/PDC1 a 60 Hz e inoltra draw, risorse e stato PICA
  senza costruire scene, attori o materiali host alternativi;
- i marker P3D conservano l'ordine del command list rispetto al draw che li
  precede. Il servizio GSP li consegna quando i comandi e le relative risorse
  immutabili sono stati registrati con successo nel flusso Vulkan ordinato;
  attendere il fence fisico prima di sbloccare il guest serializzava
  artificialmente producer e consumer e dimezzava la cadenza nativa. I fence
  del backend restano responsabili del riuso delle risorse GPU, senza diventare
  parte del timing logico CTR;
- il consumer svuota nella stessa VBlank le catene di lavoro PICA prodotte dalle
  completion (massimo osservato: tre passaggi), senza lasciare marker pendenti;
  `SetLcdForceBlack` e implementato dal servizio GSP secondo l'ABI CTR anziche
  bloccare il main thread;
- la conversione culling segue il comportamento PICA verificato in Azahar,
  compresa l'inversione del lato scartato quando il framebuffer e flipped;
- il backend mantiene ora render target color/depth persistenti indicizzati
  dagli indirizzi fisici, dimensioni e formati del framebuffer PICA; un display
  transfer viene differito alla GPU soltanto quando la sorgente virtuale CTR si
  traduce in un target effettivamente prodotto dai draw, mentre gli altri casi
  continuano a usare il transfer CPU tipizzato;
- una run di 180 refresh riconsegna tutte le relative PPF e P3D dopo la
  registrazione ordinata dei comandi, senza lavori pendenti. Lo
  scanout selezionato persiste sui successivi swapchain frame come sul display
  CTR; una clear diagnostica temporanea, poi rimossa, ha verificato l'intero
  percorso target -> transfer -> swapchain -> readback;
- il nero era in larga parte una fase di bootstrap: il main thread era sospeso
  nel comando originale FS `CloseArchive` (`0x080E0080`), ora implementato con
  handle a 64 bit, rimozione dell'archivio e risultato CTR
  `0xC8804465` per handle invalido, verificati contro Azahar;
- la diagnostica per thread ha poi identificato `FS:OpenFile` (`0x080201C2`):
  il servizio decodifica ora LowPath UTF-16/char, confina il percorso nella
  directory SaveData montata e restituisce sessioni file native utilizzabili da
  `GetSize`, `Read` e `Close`; la prima richiesta reale e `/system.dat` e la sua
  assenza viene comunicata con il risultato CTR `0xC8804470`;
- la precedente run da 303 draw mostrava lo spinner mentre il loader era
  sospeso, quindi non costituisce evidenza sulla viewport della scena. Con
  `OpenFile` corretto il guest prosegue nelle letture RomFS e raggiunge un nuovo
  stato di attesa con 43 draw e 15 display transfer; il framebuffer presentato
  era nero perche il backend presentava l'ultimo transfer, spesso appartenente
  al bottom screen;
- il servizio GSP interpreta ora sia `SetBufferSwap` (`0x00050200`) sia i due
  `FrameBufferUpdate` per thread nella shared memory a ogni PDC0/PDC1, conserva
  i due buffer per schermo e riconosce quello mostrato. Il consumer associa i
  transfer ai rispettivi output e il backend Vulkan esistente presenta soltanto
  lo scanout top selezionato al VBlank successivo, senza alterare le completion
  PPF: la stessa run torna a mostrare lo spinner top con 15/15 PPF e 23/23 P3D.
  I difetti geometrici residui appartengono quindi al percorso PICA, non alla
  selezione dello schermo e non richiedono un nuovo renderer;
- `RequestDma` esegue ora la copia guest richiesta e inserisce l'interrupt DMA
  `6` nella relay queue. Era il completion mancante che fermava il processo dopo
  43 draw; un test verifica dati copiati, avanzamento della command queue e IRQ;
- il topology PICA `Shader` (`3`) viene tradotto in triangle list come nei
  renderer Azahar, mentre l'abilitazione del geometry shader resta un registro
  distinto e continua a essere validata separatamente;
- il frontend conserva la LUT fog nativa da 128 elementi applicando
  l'autoincremento `0xE6/0xE8..0xEF`; il generatore fragment decodifica valore
  `0.0.11` e differenza signed `1.1.11`, e il backend Vulkan li consuma nello
  stesso mix depth/LUT/colore PICA;
- una run Vulkan di 600 refresh completa senza fallback, produce 6.434 draw e
  240 display transfer e mostra Hyrule Field notturna con luna. Il contenuto
  della intro e quindi raggiunto;
- gli snapshot visuali conservano draw, memory fill e display transfer PICA
  dell'intero frame, inclusi i render target offscreen. Il replay nel namespace
  Vulkan isolato riproduce le dipendenze nell'ordine espresso da
  `BeforeDrawSubmissionId` e `AfterDrawSubmissionId`; limitarsi ai draw diretti
  verso il top framebuffer perdeva il compositing di logo ed effetti e produceva
  frame neri;
- il matching interpola soltanto uniform e stato continuo di draw compatibili;
  una metrica robusta sui draw del target top riconosce i camera cut e presenta
  direttamente lo stato autorevole. Dopo una discontinuita il namespace visuale
  viene risincronizzato dal frame corrente per evitare contenuti sintetici
  obsoleti;
- il flag diagnostico `--disable-visual-interpolation` conserva simulazione,
  comandi e timing OOT3D ma presenta il namespace nativo autorevole. Al frame
  1200 questo percorso e il replay completo mostrano entrambi intro, Link adulto,
  Epona, cielo, terreno, logo animato e copyright; al cut del frame 650 il replay
  passa direttamente alla camera nuova senza la precedente geometria deformata;
- una run completa di 1201 refresh produce 43.441 draw, 591 snapshot e 579 frame
  intermedi presentati. Impiega ancora circa 39,5 secondi: la correttezza del
  replay e verificata, ma il target prestazionale superiore a 60 fps non e
  ancora raggiunto;
- il loop Vulkan distingue ora presentazioni host e VBlank guest. A 60 Hz o
  oltre, i frame aggiuntivi ricampionano la coppia PICA senza avanzare gameplay,
  audio, HID o fisica; a 30 Hz una presentazione esegue invece due refresh CTR
  completi e ordinati, inclusi clock, HID, DSP, VBlank, guest run e completion
  PICA. Il batch e limitato a due e l'eventuale debito ulteriore viene scartato,
  evitando catch-up illimitati. `--presentation-rate 30|60|120` imposta il cap
  host e `--presentation-rate free` lascia il pacing allo swapchain; il launcher
  espone gli stessi valori con `-PresentationRate`;
- smoke deterministici a 30, 60, 120 Hz e free confermano la corrispondenza fra
  refresh pianificati e VBlank realmente eseguiti, senza refresh persi alle
  cadenze nominali. Gli hitch scartano il debito oltre due VBlank invece di
  avviare un ciclo di catch-up capace di bloccare il PC;
- il generatore AOT classifica ora le 150.547 operazioni core raggiungibili in
  tre famiglie disgiunte gia determinate dall'istruzione nativa: 232 system,
  53.286 ALU e 97.029 memory. Il dispatcher chiama direttamente il decoder
  corrispondente invece di provare sempre system -> ALU -> memory; `Core` resta
  disponibile come percorso compatibile per corpus non rigenerati;
- sulla stessa run Vulkan 4K da 600 refresh, con present nativo e interpolazione
  disattivata soltanto per isolare il costo guest, la separazione dei decoder
  riduce il wall time da 18,53 a 16,46 secondi (-11,1%) e il tempo guest da 12,94
  a 11,04-11,21 secondi (-13,4/-14,7%). Una seconda build senza modifiche usa la
  cache AOT: Ninja non ha lavoro e l'intero wrapper termina in 2,77 secondi;
- `ExecuteBlock` materializza ora `r15` soltanto ai confini osservabili (fine
  blocco, branch, SVC, fault e fallback), invece di scriverlo prima e dopo ogni
  operazione. Un test verifica esplicitamente il PC visto dal callback fallback;
  la migliore run comparabile scende a 15,99 secondi wall e 10,66 secondi guest,
  ma ulteriori misure mostrano variabilita di sistema e il dato non viene usato
  come nuovo requisito prestazionale acquisito;
- il percorso nativo costruisce anche le matrici previste dall'architettura
  stereoscopica CTR, ma nella run mono osservata gli indirizzi top left/right
  coincidono: non esiste un secondo scanout da comporre e lo stereo non spiega
  duplicazioni o traslazioni del contenuto;
- le texture 2D PICA applicano ora l'origine verticale bottom-to-top verificata
  nel rasterizer Azahar. Lo spinner usa cosi le celle corrette del proprio atlas
  RGBA4 e torna a mostrare un solo Triforce composto da tre triangoli;
- un pass Vulkan dedicato compone ora il render target PICA fisico nel verso
  logico del top screen, applica il flip del display transfer e deriva il
  rapporto `OutputHeight:OutputWidth` dai registri originali. La stessa run
  conserva 6.434 draw, 240 transfer e tutte le completion senza errori e
  presenta il frame logico `400x240` centrato, senza rotazione o stretch;
- il target top Vulkan viene esteso realmente al rapporto host e la correzione
  dei bounds resta limitata ai frustum prospettici. L'espansione ortografica
  globale e stata rimossa perche riduceva anche fade e quad fullscreen,
  lasciando bande laterali; una capture interna `640x360` verifica ora fade,
  Hyrule Field, cielo e luna sull'intera larghezza. UI centrata, fullscreen e
  asset 2D a rapporto fisso riceveranno policy screen-space distinte;
- i target che non rappresentano il top screen conservano dimensioni e rapporto
  nativi; l'estensione 16:9 si applica soltanto al target top riconosciuto dal
  contratto framebuffer/transfer, senza deformare render-to-texture o bottom UI.
- la prima corsia HUD ibrida separa ora il layout N64 dalle risorse OOT3D: un
  catalogo tipizzato rifiuta texture fuori dalla namespace `oot3d/` e il
  presenter non possiede alcun fallback OTR/N64;
- le regioni item `42x42` di `icon_item_menu00.ctxb` e tutte le varianti cifra
  di `num_all00.ctxb` provengono dalle tabelle OOT3D gia recuperate. Le cinque
  lane B/Y/X/I/II, incluso lo stato disabled a alpha `0x46`, attraversano lo
  snapshot tipizzato senza riduzione ai tre C-button N64;
- la lifecycle bridge associa agli stessi binding sia l'oggetto sorgente CTXB
  sia il relativo indirizzo surface quando sono residenti. Il provider legge
  dall'oggetto nativo dimensioni `+0x2C/+0x2E`, PICA format/type
  `+0x30/+0x32` e payload `+0x4C`, li valida contro il semantic slot e usa la
  coppia source/surface come identita della cache;
- le encoding localizzate non vengono uniformate: il catalogo conserva tutte
  le varianti realmente presenti (per esempio RGBA5551 e RGB565 di
  `menu_item_parts01.ctxb`) e seleziona esclusivamente quella dichiarata dal
  CTXB runtime attivo, senza fallback a una lingua o a un formato predefinito;
- cuori, frame/fill magia, icona rupia/chiave e face dei pulsanti sono gia
  rappresentati da chiavi tipizzate, ma non ricevono coordinate arbitrarie:
  restano `missing_asset_binding` finche viene recuperata la rispettiva tabella
  UV nativa di `hud_all00.ctxb`. La HUD resta percio in shadow mode e nessun
  draw/meccanica OOT3D viene ancora soppresso.

Gli indirizzi legacy di stack/scratch restano confinati al vecchio wrapper di
chiamata e non sono stati promossi a layout del processo di produzione.

## Autorita Dei Sottosistemi

| Dominio | Autorita di produzione |
| --- | --- |
| Boot, scheduler di gioco e stato globale | codice A32/decomp OOT3D |
| Progressione, flag, save e conseguenze gameplay | codice e strutture OOT3D |
| Scene, setup, room, ingressi e popolazione | ZSI, RomFS e codice OOT3D |
| Player, attori, AI, collisioni e combattimento | codice OOT3D |
| Animazioni, root motion, attachment e visibilita | CSAB/CMB e codice OOT3D |
| Camera, cutscene, luci, ambiente e materiali | dati e codice OOT3D |
| Audio, musica e timing delle sequenze | dati e codice OOT3D |
| File select, name entry e menu fuori dal gameplay | codice, asset e lifecycle OOT3D ricollocati sul top screen |
| HUD gameplay | comportamento/layout N64, soli asset grafici OOT3D |
| Pausa, mappa e inventario | comportamento e pannelli N64, contenuti e omologhi 3D OOT3D |
| Mutazioni richieste dalla UI | action sink e workflow interni OOT3D |
| Dialoghi e overlay appartenenti alle cutscene | OOT3D, finche non esiste un contratto specifico |
| Rendering | frontend PICA OOT3D + backend Vulkan host |
| Input gameplay e mira | input PC -> contratto HID/gyro OOT3D |
| Servizi 3DS | host minimo dedicato, con port mirati da Azahar |
| Validazione | Azahar e test differenziali, mai sorgente di dati runtime |

Una chiamata N64 fuori dal modulo UI e un errore architetturale. Un asset N64
fuori dal pacchetto UI e un errore di packaging.

## Processo OOT3D Persistente

### Da Closure Isolate A Processo Completo

L'attuale `oot3d_native_a32_execution` invoca funzioni native selezionate per
attori e callback. Il prodotto richiede invece un solo `Oot3dA32Process`:

1. mappa `.text`, `.rodata`, `.data`, BSS, heap, stack e TLS agli indirizzi guest;
2. conserva registri, FPSCR, memoria e globali per l'intera sessione;
3. carica RomFS e save OOT3D tramite servizi host;
4. avvia l'entrypoint originale e lascia al gioco la proprieta del main loop;
5. esegue chiamate indirette attraverso il registro AOT generato;
6. intercetta soltanto SVC, IPC e confini host esplicitamente classificati;
7. non copia strutture attore o PlayState dentro oggetti host a ogni chiamata.

Le 38 word classificate ma non dimostrate raggiungibili non giustificano un JIT
nel prodotto. Se una viene raggiunta, la run registra PC e call stack, il
generatore aggiorna il corpus e la build locale viene rigenerata. Dynarmic puo
restare uno strumento differenziale, non un fallback silenzioso di release.

### Servizi Host

Portare da Azahar solo i componenti necessari all'applicazione:

- memoria virtuale, heap, TLS e primitive atomiche;
- thread, event, mutex, timer e scheduling richiesti dal titolo;
- filesystem RomFS/ExeFS e archivi di salvataggio;
- HID, gamepad, time, locale e configurazione;
- IPC/SVC effettivamente raggiunti;
- semantica dei command buffer PICA;
- DSP/service glue non gia coperto dal mixer nativo.

Il port deve vivere in un modulo isolato, per esempio
`runtime/three_ds_recomp/include/three_ds_recomp/oot3d/platform_ctr`, e non deve trascinare frontend Qt,
Dynarmic, renderer completo di Azahar o supporto ad altri giochi. Se viene
copiato codice GPL, il confine di distribuzione e la licenza del prodotto devono
essere verificati prima di consolidarlo.

## Rendering Vulkan

### Frontend PICA Unico

Il codice OOT3D deve poter emettere gli stessi comandi che emette sul 3DS. Un
frontend PICA backend-neutral decodifica registri, command buffer, shader state,
texture, framebuffer e draw call in pacchetti immutabili. Il backend Vulkan gia
esistente consuma quei pacchetti.

Azahar e una fonte adatta per la semantica dei registri e i casi limite PICA;
non deve diventare il renderer di produzione parallelo. I decoder CMB/CMAB e le
strutture tipizzate gia presenti possono anticipare risorse e pipeline, ma non
devono ricostruire a mano la lista degli oggetti che il gioco vuole disegnare.

Il frontend mantiene un registro machine-readable di copertura per registri,
shader, TEV, LUT, texture format, framebuffer operation e draw path realmente
osservati. Uno stato sconosciuto non viene approssimato: conserva command stream,
provenance e primo call site, poi viene implementato come capability generale.
La stessa capability deve essere riutilizzabile da ogni scena, attore e cutscene.

Ordine di migrazione:

1. collegare upload texture, vertex/index buffer e draw opachi;
2. collegare TEV, alpha test, depth, stencil, blend, cull e scissor;
3. collegare lighting, fog, LUT, Shadow2D e framebuffer texture;
4. collegare traslucidi, acqua, particelle, sky, lens flare e post-process;
5. rimuovere dal percorso di produzione l'orchestrazione scene/attori della demo.

Ogni nuova tranche di decompilazione relativa a CMB, CMAB, PICA, luce, ombra,
materiali, particelle o post-process richiede un audit del frontend e del backend.
Se emerge un mismatch, vince il contratto OOT3D e si aggiorna il renderer; non si
compensa modificando asset, colori o parametri di una scena.

Le pipeline Vulkan vengono indicizzate con una chiave di stato PICA stabile,
compilate in anticipo o in background e persistite nella pipeline cache. Nessuna
compilazione shader deve avvenire ripetutamente nel frame loop.

### Stato Render Separato Dallo Stato Gameplay

Alla fine di ogni tick gameplay si pubblica un `RenderSnapshot` immutabile con
trasformazioni precedenti/correnti, camera, animazioni, luci e draw packets. Il
thread render puo produrre due frame da uno stesso intervallo gameplay senza
leggere memoria guest mentre viene modificata.

## Audio OOT3D

Il gioco conserva identita, priorita, sequenze e timing OOT3D. Il servizio host:

- riceve eventi o command buffer audio dal processo OOT3D;
- usa BCSAR/CSEQ/BCWAV/BCSTM e le tabelle native gia supportate;
- mantiene il clock nativo separato dal frame rate video;
- miscela su un worker real-time senza chiamate A32 nel callback dispositivo;
- converte dal rate nativo verificato al rate del dispositivo con il resampler
  gia implementato;
- conserva riverbero, limiti player, modulation, loop e scheduling sample-accurate.

L'aumento a 60 FPS non cambia tempo, pitch o durata. Gli eventi gameplay vengono
timestampati in tempo monotono/audio, non contati in frame presentati.

Anche l'audio mantiene una matrice di copertura per BCSAR, CSEQ, bank, wave,
stream, DSP command, codec, effect bus e service call osservati. Nuove evidenze
da `Zelda3drecomp`, asset originali o Azahar correggono parser, mixer e DSP; non
vengono adattate al sottoinsieme oggi implementato. Un comando sconosciuto viene
registrato con contesto e chiuso per famiglia prima di promuovere il workflow che
lo usa.

## Runtime UI Ibrido Integrato Single-Screen

### Confine Di Implementazione

Il runtime implementa `UiBackend` direttamente in `oot3d_native_app`.
`UiBackend` resta il nome tecnico del confine gia recuperato, non indica un
overlay o un processo esterno. Ogni subsystem sceglie il provider stabilito
dalla policy normativa: frontend e dialoghi restano guest OOT3D; HUD gameplay
usa comportamento/layout N64 con visual OOT3D; pausa usa il provider ibrido.
Tutti vengono invocati dagli stessi root `initialize/update/input/draw/reset`
che delimitano la UI OOT3D.

Riceve soltanto:

- una `UiBackendStateView` costruita nello stesso tick dai contesti OOT3D tramite
  l'adapter verificato, con proiezioni tipizzate e senza un secondo stato
  gameplay autorevole;
- input host normalizzato;
- il contesto del pass UI Vulkan single-screen;
- un dispatcher di intenti e action request.

Non riceve `PlayState*`, puntatori guest, offset di `SaveContext`, ID grezzi di
controller 3DS o accesso generico alla memoria A32.

Il codice UI N64 portato non viene riscritto come una collezione di widget
generici. Le sole state machine assegnate a HUD e pausa restano riconoscibili e
vengono adattate tramite una facade tipizzata. Frontend OOT3D, inventario,
progressione, equipaggiamento e save restano nello stato OOT3D.

### Integrazione Nel Frame

Per ogni sottosistema UI il dispatcher originale esegue uno dei due percorsi:

```text
root UI OOT3D raggiunto dal processo A32
    -> UiFramePlan
       -> route guest: continua nel corpo UI OOT3D
       -> route host: chiama il provider N64/ibrido assegnato
                         -> legge la view OOT3D corrente
                         -> aggiorna il solo stato visuale/UI N64
                         -> emette draw nel pass Vulkan UI corrente
                         -> inoltra eventuali action request al sink OOT3D
```

Non esistono una finestra overlay, un secondo loop input o una composizione
postuma sopra lo screenshot del gioco. Il framebuffer inferiore OOT3D puo
restare un target nativo offscreen, ma viene compositato nel render graph dello
stesso frame. HUD, dialoghi, pausa, fade e transizioni condividono viewport,
scissor, color space e ordinamento della presentazione finale.

### Ownership

Per HUD gameplay il runtime N64 possiede comportamento e layout, i valori e gli
asset provengono dalla proiezione OOT3D. Per pausa, inventario, gear, quest e
mappe possiede anche navigazione, input e pannelli di sfondo; contenuti, icone,
mappe ed equipaggiamento provengono da OOT3D. File Select, Name Entry, opzioni e
gli altri menu fuori dal gameplay conservano navigazione, input, draw e asset
OOT3D; il layer host modifica solo presentazione e coordinate del puntatore.

Rimangono sempre OOT3D:

- validazione e commit di item/equipaggiamento/ammo/bottle;
- inventario child/adult e campi esclusivi OOT3D;
- query native mantenute dal contratto;
- continuazione asincrona di save, copy, erase, load e name write;
- checksum, formato save e persistenza;
- Boss Challenge, Master Quest e dati esclusivi anche se non ancora mostrati.

Il runtime emette una richiesta tipizzata nello stesso tick UI; l'adapter inietta
il contesto nativo necessario e chiama l'action sink OOT3D. Solo dopo successo
la view corrente o quella del tick seguente mostra il nuovo stato.

### Soppressione Della UI OOT3D

Ogni sottosistema viene sostituito atomicamente usando `UiFramePlan`:

1. il runtime N64 raggiunge parita funzionale per l'intero workflow;
2. update/draw nativi vengono soppressi soltanto per le responsabilita migrate;
3. action sink, query e lifecycle indispensabili restano attivi;
4. il profilo `oot3d-native` deve poter ripristinare tutte le route originali.

Lo schermo inferiore non viene presentato. Durante la transizione puo essere
renderizzato offscreen solo se un workflow nativo non ancora sostituito lo
richiede; la release completa non deve dipendere da touch o secondo display.

### Pacchetto Asset UI

Gli asset N64 sono consentiti esclusivamente sotto una namespace montata come
`ui/n64/*`. Il resolver di contenuti del mondo deve rifiutarli. In questo modo
una dipendenza UI non puo diventare per errore un fallback per modelli, texture,
animazioni o audio del mondo OOT3D.

## Input PC, Mouse E Giroscopio

### Principio

Tutto il gioco deve essere completabile sia con gamepad sia con mouse e tastiera.
Il codice OOT3D continua a ricevere il proprio modello HID: circle pad, pulsanti,
canale look/aim e campioni giroscopio. Il layer host associa dispositivi PC a
questi segnali; non modifica direttamente player, camera o reticolo.

Il mapping avviene prima del tick gameplay e conserva timestamp, pressione,
rilascio, repeat, assi analogici e ordine degli eventi. Gameplay e UI usano due
context map distinti, cosi l'apertura di un menu non lascia input premuti nel
mondo e il ritorno al gioco non produce azioni spurie.

### Mouse Come IMU Virtuale

La mira giroscopica OOT3D viene mantenuta. Un `VirtualImuProvider` implementa la
stessa struttura di campionamento e calibrazione usata dal percorso HID nativo:

1. acquisisce raw mouse delta ad alta frequenza, senza accelerazione del sistema;
2. converte i delta in spostamento angolare configurabile e indipendente dal
   frame rate;
3. integra un orientamento virtuale e ne deriva velocita angolare e timestamp;
4. pubblica i campioni nel buffer HID/gyro consumato dal codice OOT3D;
5. azzera il riferimento sulle transizioni native fra gameplay, first-person,
   item aim, pausa e perdita del focus;
6. lascia al codice OOT3D clamp, smoothing, recenter, targeting e camera mode.

Il layout esatto del campione, le unita, gli assi, il segno, la frequenza e la
calibrazione devono provenire dalla decompilazione HID OOT3D e dal corrispondente
servizio Azahar. Non si converte il mouse direttamente in yaw/pitch camera.

Un controller dotato di gyro usa lo stesso `VirtualImuProvider`, ma alimentato
dai sensori SDL. Mouse, gyro fisico e right stick possono essere alternati senza
resettare la state machine nativa; il provider sceglie una sola sorgente attiva
per campione per evitare doppia integrazione.

Opzioni obbligatorie:

- sensibilita mouse e gyro separate per X/Y;
- inversione verticale e orizzontale;
- smoothing opzionale, disattivato per default sul raw mouse;
- deadzone e calibrazione solo per sensori reali;
- scala uniforme rispetto a DPI e presentation rate;
- tasto di rilascio/cattura del mouse e gestione robusta di alt-tab/focus loss.

### Preset Mouse E Tastiera

Il preset iniziale segue la convenzione consolidata dei giochi PC in terza
persona, ma ogni azione resta rimappabile e puo avere piu binding:

| Azione semantica | Binding predefinito |
| --- | --- |
| Movimento/circle pad | `W`, `A`, `S`, `D` |
| Camera e mira gyro | movimento mouse raw |
| Attacco/B | pulsante sinistro mouse |
| Target/lock-on | pulsante destro mouse |
| Azione contestuale/A | `E` e `Space` |
| Scudo/R | `Left Shift` |
| Look/Navi | `F` |
| Quattro quick-slot OOT3D | `1`, `2`, `3`, `4` |
| Ocarina dedicata | `Q` |
| Pausa/menu | `Tab` |
| Conferma/annulla UI | `Enter` / `Escape` e binding contestuali |

I quick-slot sono i quattro slot OOT3D reali, anche se la loro presentazione
segue la UI N64 single-screen. Il runtime UI emette gli action request nativi e
non riduce inventario o save ai tre C-button N64.

Il preset non e l'ABI del gioco. Profili alternativi, tastiere AZERTY/QWERTZ,
mouse mancino e layout accessibili possono cambiare ogni binding senza cambiare
il codice gameplay. Menu, ocarina, pesca, first-person item, minigame, dialoghi,
name entry e Boss Challenge devono avere una riga esplicita nella matrice di
copertura input prima di dichiarare completo mouse+tastiera.

## Presentazione 16:9

### Mondo 3D

Il target logico e il framebuffer finale sono realmente 16:9, indipendentemente
dalla risoluzione fisica. Non si renderizza una vista 400x240 per deformarla a
schermo intero. Per la camera:

- conservare FOV verticale, near/far e semantica camera OOT3D;
- derivare il FOV orizzontale dal nuovo aspect ratio;
- usare lo stesso aspect per proiezione, culling, screen-space effects e input
  di mira;
- allargare il frustum e i limiti di visibilita coerentemente, senza scalare
  coordinate o velocita del mondo;
- ricostruire viewport, scissor e render target direttamente alle dimensioni
  16:9;
- disabilitare stereo e secondo occhio;
- correggere soltanto elementi con un contratto screen-space dimostrato.

Con un canvas di riferimento alto 240, la larghezza 16:9 e `426.666...`: i 400
pixel nativi definiscono la regione centrale e il mondo guadagna circa 13,33
pixel logici per lato. Questa relazione serve per gli elementi screen-space; il
3D usa una nuova matrice di proiezione, non un offset applicato all'immagine.

Cutscene e camere restano native nei movimenti e nei valori di FOV, ma la loro
proiezione usa l'aspect 16:9 e mostra contenuto laterale aggiuntivo. Il pillarbox
non e ammesso nel normale percorso 3D. Un asset bidimensionale authored a rapporto
fisso usa una policy esplicita `fit`, `crop` o ricostruzione, mai stretch; tale
policy appartiene al formato/ruolo dell'asset e non al nome della scena.

Devono usare le dimensioni finali e lo stesso aspect:

- culling, LOD e occlusion query;
- sky, luna, sole e lens flare;
- fog e ricostruzione depth;
- Shadow2D e framebuffer texture;
- acqua, reflection/refraction e post-process;
- reticoli, targeting e proiezione dei marker;
- scissor, viewport e coordinate di capture.

I test widescreen confrontano raggi camera, world-to-screen e bordi del frustum,
non soltanto screenshot. Una primitiva circolare deve restare circolare e una
misura angolare della camera deve restare invariata fra risoluzioni 16:9.

### UI

Usare un canvas virtuale 16:9 con safe area. Gli elementi N64 sono classificati
come `left`, `right`, `center` o `viewport`; le coordinate verticali conservano
la composizione, mentre le ancore laterali seguono i bordi 16:9. Pause e file
select occupano un overlay full-screen; dialoghi OOT3D vengono composti nello
stesso framebuffer con priorita esplicita.

La risoluzione interna 3D, la scala UI e la risoluzione finestra sono tre valori
separati. L'UI non deve cambiare dimensione al variare del render scale.

## Clock Nativo E Simulazione A Refresh Elevato

### Autorita Temporale

Il clock autorevole non e il campo `PlayState+0x110`. `GameState_Init`
(`0x00416F60`, store `0x00417000`), `SkelAnime_Update`, `Actor_MoveForward` e
gli helper `Math_*Step*` convergono sullo stato puntato dalla cella globale
`0x0051B2F4`, con update rate signed a `+0x110`. OOT3D combina 30 update al
secondo e valore `2`, cioe 60 unita temporali native al secondo.

Il contratto source-facing espone separatamente:

- frequenza simulazione e presentazione;
- `stepSeconds = 1 / simulationHz`;
- `nativeUpdateRate = 60 / simulationHz`;
- `nativeStepScale = nativeUpdateRate / 2`.

A 60 Hz il processo esegue un vero `GameState_Update` per refresh e usa update
rate `1`: animazioni, root motion, gravita, velocita, collisioni e gli step
matematici consumano due mezzi passi invece di un passo a 30 Hz. I tick CTR e
l'audio non vengono accelerati. Il limiter nativo viene adattato nel suo stato
deadline/intervallo, dopo il VSync obbligatorio, senza produrre VBlank fittizi.

L'adattatore A32 e isolato dal contratto C++ e risolve il puntatore globale a
ogni update, quindi resta valido attraverso transizioni e savestate. L'ABI A32
puo rappresentare esattamente solo update rate interi: 30 e 60 Hz sono esatti;
una frequenza frazionaria non viene approssimata o alternata. Il percorso
source-recompiled ha ora un primo runtime gameplay tipizzato che consuma
direttamente il valore floating del contratto; architettura, copertura e metodo
di conversione sono descritti in
[`OOT3D_TYPED_GAMEPLAY_CONVERSION.md`](OOT3D_TYPED_GAMEPLAY_CONVERSION.md).

La regressione deterministica sul medesimo checkpoint e su 120 VBlank conferma
60 update con rate `2` nel profilo 30 Hz e 120 update con rate `1` nel profilo
60 Hz. Le coppie a tempo equivalente conservano action, risorsa animazione e
posizione; lo scarto massimo del frame animato e inferiore a `8e-5` e il
framebuffer finale e identico. Una trace con input di movimento conferma inoltre
la stessa progressione di velocita reale, con differenze limitate al substep di
input e collisione piu fine.

### Loop

```text
poll input host
accumulate wall time
while simulation debt >= stepSeconds:
    publish current HID state
    apply native time scale to the authoritative timing state
    execute one complete OOT3D update
    simulation debt -= stepSeconds
render the newest authoritative state
present
```

A 60/60 il replay di snapshot e disattivato: ogni frame presentato proviene da
un update e da draw OOT3D nuovi. L'interpolazione resta una capability opzionale
solo quando la presentazione supera la frequenza simulazione disponibile; non e
piu il mezzo usato per dichiarare il gameplay a 60 Hz.

I contatori discreti che non consumano il timing state devono essere migrati a
un dominio temporale tipizzato quando emergono dalla decompilazione. Non si
applicano patch per scena e non si rallenta globalmente il clock CTR per
compensarli. I test deterministici confrontano stato guest a parita di tempo e
segnalano il primo action/timer che diverge.

### Identita E Discontinuita

Ogni elemento interpolabile deve avere un'identita stabile derivata dal processo
OOT3D, per esempio puntatore istanza guest, generation counter, draw owner,
materiale, limb e pass. Non si associano primitive soltanto per ordine di draw,
perche spawn, destroy e culling potrebbero far interpolare due oggetti diversi.

L'interpolazione viene annullata per:

- camera cut e cambi modalita non continui;
- teleport, room transition e respawn;
- spawn/destroy e cambio generation;
- cambio improvviso di modello, skeleton o visibility contract;
- wrap di timeline, eventi step/hold e valori marcati discreti;
- framebuffer history incompatibile o resize.

In questi casi il frame usa immediatamente lo stato corrente. Una transizione
non interpolabile corretta e preferibile a un frame fluido ma semanticamente
sbagliato.

### Copertura Visiva

La matrice di interpolazione classifica ogni campo come:

| Classe | Trattamento render |
| --- | --- |
| posizione/scala continua | lerp fra snapshot |
| rotazione continua | shortest-arc coerente con il formato |
| camera eye/at/up/FOV | interpolazione solo senza camera cut |
| animazione CSAB | campionamento al frame frazionario visuale |
| morph | interpolazione della progressione nativa |
| materiale CMAB continuo | campionamento visuale frazionario |
| alpha/visibility discreta | step sul tick autorevole |
| particella persistente | posizione/scala interpolate per identita |
| spawn, hitbox, collisione, evento | mai interpolati come logica |

Il renderer puo ancora produrre frame intermedi quando il refresh supera la
frequenza simulazione supportata dal backend. Nel profilo 60/60, invece, non ci
sono frame intermedi: il numero di update, pose e draw nativi e pari al numero
di refresh.

## Animazioni E Fisica Native A 60 FPS

### Non Inventare Keyframe

CSAB resta l'asset autorevole. Non si duplicano keyframe e non si interpola la
mesh skinnata finale per simulare un update mancante. `SkelAnime` viene
aggiornato realmente a 60 Hz con il mezzo passo temporale nativo; le curve CSAB
ricevono quindi frame frazionari prodotti dal gameplay stesso.

Il runtime corrente dimostra gia che il clock non e semplicemente "un frame
CSAB per frame video": usa 30 display tick al secondo, update rate globale 2 e
scala `1/3`, quindi una clip a play speed 1 avanza 20 frame sorgente al secondo.
Il runtime conserva questa formula e il play speed della singola azione; non
hardcoda 30 frame sorgente al secondo.

### Pipeline

1. decodificare ogni traccia CSAB una sola volta in una cache immutabile;
2. conservare step, linear, Hermite, wrapping e shortest-arc originali;
3. ottenere dal gameplay stato clip, frame corrente, play speed, morph e range;
4. avanzare il frame con il `nativeUpdateRate` autorevole;
5. campionare translation/rotation/scale per joint;
6. comporre la gerarchia e gli override limb OOT3D;
7. caricare una joint palette per istanza nel backend Vulkan;
8. effettuare skinning lato GPU senza rigenerare vertex buffer.

Il campionamento deve usare la semantica delle curve native. Quaternion slerp e
ammesso solo quando equivalente al percorso shortest-arc dimostrato; non deve
sostituire indiscriminatamente le rotazioni native.

### Root Motion, Eventi E Morph

- root motion, gravita, velocita e collision response vengono integrati dal tick
  OOT3D a 60 Hz usando il mezzo passo autorevole;
- hitbox, footstep, invulnerability, attachment gameplay e notify scattano nel
  tick OOT3D, mai da un sampler esclusivamente render;
- i morph fra clip vengono aggiornati a 60 Hz mantenendo durata e curva native;
- gli effetti ancorati ai joint usano la matrice autorevole del tick corrente;
- un cambio clip, wrap o visibility step usa le regole di discontinuita e non
  tenta di fondere stati logicamente incompatibili.

### Efficienza

- cache condivisa per clip, stato minimo per istanza;
- campionamento SoA e job per gruppi di attori visibili;
- culling prima della posa completa;
- frequenza ridotta per attori lontani, con interpolazione a 60 Hz;
- ring buffer persistente per joint palette;
- nessuna allocazione nel frame loop;
- metriche separate per decode, sample, hierarchy, upload e skinning GPU.

## Salvataggi E Stato

Il save resta OOT3D. La UI N64 mostra una proiezione, ma copy, delete, load,
name write, checksum e I/O asincrono sono eseguiti dai workflow nativi definiti
dal contratto. Campi Master Quest, Boss Challenge, quattro slot item e altri
stati esclusivi non possono essere eliminati per adattarsi alle strutture N64.

Autosave o nuove opzioni PC, se introdotti, devono essere estensioni host sopra
una serializzazione nativa valida, non un secondo formato autorevole.

## Organizzazione Dei Target

Separare il prodotto in librerie con dipendenze strette:

```text
oot3d_a32_process          memoria, registri, dispatch AOT, SVC
oot3d_ctr_host_services    FS, thread, timer, HID, save, IPC minimo
oot3d_pica_frontend        command buffer -> pacchetti PICA
oot3d_vulkan_renderer      backend Vulkan esistente
oot3d_native_audio         backend audio esistente
oot3d_ui_contract          snapshot, proiezioni, intenti, action sink
oot3d_ui_n64               HUD/menu N64 single-screen
oot3d_native_app           bootstrap e frame scheduler
```

`oot3d_native_app` non deve dipendere da `soh`, dal renderer Azahar, da Qt o da
Dynarmic. Le demo possono dipendere dai parser e dal renderer, ma il prodotto
non deve dipendere dalla loro orchestrazione fixture-specifica.

L'AOT generato resta in un albero locale hashato. Si rigenera solo quando cambia
`code.bin`, profilo o generatore. Gli shard sono librerie separate per evitare
relink completi; pipeline e asset cache hanno chiavi persistenti. Le protezioni
di `OOT3D_FAST_BUILD_ISOLATION.md` vengono estese al nuovo grafo.

## Strategia Operativa

### Regola Di Velocita

L'unita di lavoro e un workflow end-to-end, non una funzione decompilata:

```text
workflow nativo completo
    -> batch di servizi/confini richiesti
    -> integrazione A32 + Vulkan/audio/UI
    -> una build Release incrementale
    -> una run deterministica
    -> commit
```

Non si torna a implementare singoli attori nel runtime host. Un callback non
supportato deve rimanere nel processo A32; un servizio host mancante viene
chiuso per tutta la sua famiglia. Dump e Azahar si usano solo sul primo punto di
divergenza concreto.

### Milestone 0 - Confine Del Prodotto

- introdurre `oot3d_a32_process` persistente;
- importare una snapshot immutabile aggiornata di `Zelda3drecomp`;
- vietare dipendenze N64 fuori da `oot3d_ui_n64`;
- vietare stato scene/attori host nel target di produzione;
- collegare il contratto UI senza ancora sopprimere la UI nativa.

Accettazione: avvio dell'entrypoint OOT3D con memoria persistente e report dei
soli servizi host mancanti.

### Milestone 1 - Boot OOT3D Completo

- chiudere SVC, thread, filesystem, time e HID raggiunti dal boot;
- arrivare dal logo al file select eseguendo solo codice gameplay OOT3D;
- usare il frontend PICA e Vulkan per ogni draw;
- usare l'audio OOT3D senza dipendere dal frame rate;
- creare direttamente swapchain, viewport e proiezione 16:9, senza stretch.

Accettazione: title/file select funzionanti nella app nativa, senza CPU JIT,
frontend Azahar o gameplay host ricostruito.

### Milestone 2 - UI Ibrida Single-Screen

- collegare il runtime N64 direttamente ai lifecycle UI OOT3D;
- implementare HUD gameplay e pause shell;
- implementare Items, Gear, Quest e mappe;
- ricollocare file select, name entry e gli altri menu frontend OOT3D sul top screen;
- rimappare mouse/touch nel loro spazio nativo inferiore `320x240`;
- usare soli asset OOT3D nella HUD e contenuti OOT3D nella pausa ibrida;
- instradare tutte le mutazioni agli action sink OOT3D;
- disattivare per sottosistema la presentazione UI nativa;
- rendere ogni workflow utilizzabile con il preset mouse+tastiera;
- emettere la UI nello stesso render graph e pass Vulkan dell'applicazione.

Accettazione: un solo framebuffer 16:9 controllabile interamente da gamepad,
save/load nativo, nessun overlay esterno e nessun accesso UI diretto alla memoria
guest.

### Milestone 3 - Kokiri End-To-End

- caricare un save e giocare dalla casa di Link alla Foresta Kokiri;
- verificare tutti gli attori, dialoghi, collisioni, camere, transizioni, audio e
  salvataggio prodotti dal processo OOT3D;
- rimuovere il runtime attori/scene manuale dal percorso di produzione;
- raggiungere 60 update e 60 frame OOT3D distinti al secondo;
- alimentare look, first-person e item aim attraverso l'IMU virtuale mouse.

Accettazione: la scena non usa Room Compilation Unit o profili attore host per
decidere cosa esiste; tali dati restano solo test e diagnostica.

### Milestone 4 - Primo Ciclo Di Gioco Completo

- Kokiri, Deku Tree, boss, reward, uscita e Hyrule Field;
- combattimento, inventory action, cutscene e save/load completi;
- gameplay, collisioni e camera verificati a parita di tempo fra simulazione
  30/60 e, sul percorso source-facing, anche oltre 60 Hz;
- interpolazione coperta come fallback quando presentazione e simulazione hanno
  frequenze diverse, senza modificare lo stato guest;
- budget CPU/GPU rispettato nelle scene piu dense della tratta;
- gameplay della tratta completabile senza gamepad o touch.

### Milestone 5 - Main Quest Completa

- espandere per tratte giocabili, non per asset isolati;
- chiudere dungeon, minigame, item, boss e transizioni in ordine di playthrough;
- mantenere una matrice di servizio/render/audio/UI, non una lista manuale di
  attori supportati;
- completare la matrice temporale dei sottosistemi e usare l'interpolazione
  visiva soltanto oltre la frequenza simulazione disponibile.

### Milestone 6 - Contenuti Esclusivi OOT3D

- Master Quest;
- Boss Challenge/Gauntlet;
- Sheikah Stones e Visions;
- gyro/aim adattato a mouse, stick e sensori PC;
- opzioni PC senza alterare save o gameplay nativo.

## Budget Prestazionale

Il prodotto presenta di default a 60 Hz, ma il renderer non deve essere costruito
intorno a un budget appena sufficiente di 16,67 ms. Il target tecnico su hardware
di sviluppo e un percorso uncapped nell'ordine dei 120 FPS a 1080p nelle tratte
rappresentative, con CPU frame e GPU frame vicini o inferiori a 8 ms. Il requisito
di stabilita resta un 1% low sopra 60 FPS nelle scene peggiori del playthrough.

Il clock di presentazione e indipendente. Il profilo predefinito usa simulazione
e presentazione a 60 Hz; refresh superiori usano il contratto temporale floating
nel percorso source-recompiled oppure, finche un sottosistema resta vincolato
all'ABI intero A32, l'interpolazione visiva esplicitamente dichiarata.

| Area | Budget CPU/GPU iniziale |
| --- | ---: |
| Tick A32 + gameplay nativo | 4,0 ms CPU ogni `1/60` |
| Diagnostica/capture snapshot | 0 ms nel percorso normale |
| Interpolazione oltre simulation rate | 1,5 ms CPU per frame opzionale |
| Preparazione Vulkan | 1,0 ms CPU |
| Rendering completo | 8,0 ms GPU |
| UI | 0,5 ms CPU/GPU |
| Audio | worker separato, zero blocchi sul main thread |

Ottimizzazioni prioritarie:

- guest memory diretta e persistente, non marshaling per callback;
- batch delle transizioni A32/host;
- port C++ tipizzato delle funzioni calde solo dopo profiling;
- command e descriptor buffer persistenti;
- pipeline cache e upload asincroni;
- culling e pose jobs;
- matching O(1) tramite identita e generation stabili, senza ricerca globale fra
  i draw dei due snapshot;
- filesystem e decompressione fuori dal frame loop;
- nessun JSON o scansione cataloghi durante update/draw.

Prima si riproduce la semantica corretta, poi si ottimizza la sua implementazione
senza rimuovere capability. Le scorciatoie che saltano materiali, attori, effetti,
voci o aggiornamenti non contano come raggiungimento del budget.

## Verifica

### Test Automatici

- `scripts/oot3d/Test-Oot3dFrameRateEquivalence.ps1` riparte dallo stesso
  checkpoint gameplay e richiede fingerprint guest identico fra presentazione
  30/60 Hz sullo stesso numero di VBlank; confronta inoltre action, animazione,
  posizione, velocita e collision flags fra simulazione 30/60 Hz allo stesso
  tempo nativo;
- hash deterministico dello stato guest a tick noti;
- equivalenza temporale degli hash guest e della sequenza eventi a 30/60 Hz
  sull'adattatore A32 e a 30/60/120 Hz sul contratto source-facing;
- trace per tempo reale di input, timing state, posizione/velocita player,
  collision flags, action function e frame `SkelAnime`;
- replay raw mouse -> campioni IMU -> mira nativa a presentation rate diversi;
- matrice di copertura mouse+tastiera per ogni modalita gameplay e UI;
- test action sink UI e round-trip save;
- conteggio frame presentati e frame realmente differenti;
- test di discontinuita per cut, teleport, spawn/destroy, visibility e clip wrap;
- hash delle pose a tempi interi e frazionari;
- golden audio su eventi e sequenze, non sul timing video;
- replay di command stream PICA e DSP per ogni nuova capability;
- benchmark uncapped CPU/GPU con conteggio draw, pipeline, pose e voci audio;
- framebuffer capture interna Vulkan, mai screenshot della finestra Windows;
- test che fallisce se il prodotto monta asset N64 fuori da `ui/n64`;
- test che fallisce se un modulo gameplay include codice Ship/N64.

### Uso Di Azahar

Azahar serve a:

- confrontare il primo punto di divergenza di memoria, registri o servizi;
- verificare semantica PICA e DSP;
- produrre trace deterministiche di un workflow;
- confermare timing ed eventi ai checkpoint strategici.

Non serve a produrre valori da copiare, screenshot come obiettivo o uno strato
emulatore permanente nel prodotto.

## Criteri Di Chiusura

Il progetto e giocabile nativamente quando:

1. boot, main loop e ogni gameplay call provengono da A32/decomp OOT3D;
2. l'app non richiede Dynarmic, frontend Azahar o `soh.exe`;
3. scene, attori, asset, collisioni, camera, cutscene e audio provengono dal
   gioco OOT3D dell'utente;
4. UI frontend OOT3D e UI N64/ibrida sono integrate nei lifecycle e nel render
   graph secondo la policy normativa, usando il contratto tipizzato e gli action
   sink OOT3D;
5. esiste un solo stato gameplay autorevole, quello OOT3D;
6. la presentazione renderizza direttamente in single-screen 16:9, senza
   stretching di un framebuffer nativo;
7. ogni frame a 60 Hz esegue un update e un draw OOT3D reali, senza replay dello
   snapshot precedente;
8. gameplay, player, camera, collisioni, timer ed eventi mantengono la stessa
   durata reale della run nativa OOT3D a 30 Hz, salvo la maggiore risoluzione
   numerica esplicitamente verificata dei substep;
9. save/load conserva tutti i campi OOT3D;
10. l'intero playthrough e completabile con mouse e tastiera, inclusa la mira
    gyro OOT3D alimentata dall'IMU virtuale;
11. un playthrough completo non incontra servizi, draw path o callback mancanti.

## Prossimo Blocco Implementativo

Il processo originale supera APT, GSP, RomFS, DSP, CFG, HID e lo shim NDM,
riceve PDC0/PDC1 e riproduce stabilmente la title intro nel backend Vulkan.
Render target fisici, DMA, display transfer GPU, render-to-texture, fog/LUT,
PPF, audio DSP e persistenza dello scanout sono collegati. Il framebuffer top e
presentato direttamente in 16:9 e l'interpolazione visuale conserva anche la
catena di compositing offscreen.

Il prossimo blocco con il miglior rapporto costo/resa e rimuovere dal percorso
caldo il dispatcher `PackedOp` A32, che resta il costo dominante: nella run da
600 refresh il guest impiega ancora 11,0-11,2 secondi, contro circa 1,5 secondi
di submit PICA e 0,6 secondi nel backend. Una prova con 1.000 esecutori statici
che richiamavano ancora i decoder ARM ha peggiorato il tempo guest a 12,28
secondi ed e stata scartata: predecodificare o srotolare l'interprete non
risolve il collo di bottiglia. Il passo successivo e compilare e collegare le
funzioni decompilate host, mantenendo invariati ABI, memoria, servizi e stato
OOT3D. Non va introdotto un secondo gameplay runtime e non va usato un JIT come
architettura finale.

Il profilo deterministico dell'intera title intro (1.201 refresh, campionamento
1:64) ha contato 87.429.490 ingressi nei blocchi A32. La distribuzione non e
piatta: i primi 100 blocchi coprono il 50,09%, i primi 500 il 76,80% e i primi
1.000 l'87,62%. Il profilo compatto in
`tools/oot3d/native_a32_runtime/profiles/title_intro_logo_1201.json` seleziona
quindi il fronte prioritario della conversione a sorgente host. I blocchi si
aggregano in 234 funzioni; `FUN_004a0338`, `FUN_004a022c` e
`MeshCommandPacket_Submit` coprono insieme il 26,70% dei campioni totali. Il
profilo decide soltanto l'ordine di integrazione e non introduce dati o
comportamento specifici della cutscene.

Il primo confine di esecuzione sorgente e ora operativo sulle chiamate A32
`BL/BLX`: una funzione registrata viene eseguita come C++ host completo, mentre
un caso rifiutato continua automaticamente nel corpo ARM originale. Il primo
consumer e il copy helper `0x00371738`, ricostruito dal disassembly e dal
pseudocodice locali. In 600 refresh della title intro sostituisce 7.290 chiamate
e 1.105.552 byte; 288 casi sovrapposti o non rappresentabili restano ARM. Le
catture framebuffer interne al frame 600 con helper compilato e fallback ARM
sono byte-identiche (`SHA-256 DF8375ED116DEBE8DAD86BC400EFD4AABD8F37E8228316D7A2B3D1D2789C5B7F`).
Il guadagno isolato e circa 0,13% del tempo guest, coerente col peso inferiore
all'1% della funzione: il valore principale e il confine generale, che ora va
alimentato in ordine di copertura con funzioni sorgente complete.

Lo stesso confine esegue ora anche tre ulteriori routine complete ricostruite
dal binario locale: `PicaCommandWriter_WriteRegisterRange` (`0x00307BD8`),
`PicaCommandWriter_UploadVertexFloatUniforms` (`0x00307C94`) e
`Mtx3x4_CopyIfDistinct` (`0x00372224`). Non vengono sintetizzati comandi host:
i writer producono nel buffer guest gli stessi word PICA e la copia matrice
mantiene l'ABI originale. Ogni percorso valida prima sorgenti e destinazioni;
se non puo completare atomicamente non scrive nulla e conserva il corpo ARM.
In 600 refresh 4K della title intro, le quattro funzioni compilate gestiscono
205.763 chiamate e 15.167.016 byte, con 288 fallback ARM. Draw count e cattura
framebuffer restano identici al percorso ARM (`18.305` draw e SHA-256
`DF8375ED116DEBE8DAD86BC400EFD4AABD8F37E8228316D7A2B3D1D2789C5B7F`).
Nella stessa coppia di run il tempo guest passa da 9,93 a 9,61 secondi, circa
il 3,2%. Il risultato conferma che i port foglia sono corretti ma insufficienti
per il target: le prossime conversioni devono assorbire intere catene renderer
e animazione per evitare sia il dispatcher sia i numerosi confini di chiamata.

`MeshCommandPacket_Submit` (`0x00466E2C`) e la prima catena renderer completa
collegata allo stesso confine host. Le istruzioni originali mostrano che
`nngxGetCmdlistParameteri(0x208)` e `PicaCommandStats_AddBytes` leggono lo
stesso puntatore guest (`0x0054CC4C`): il port copia quindi il packet CMB nel
command buffer indicato dal gioco e avanza quel puntatore della dimensione
originale, senza stato NNGX parallelo. In 600 refresh gestisce 14.153 submit e
16.989.848 byte aggiuntivi. Dopo aver eliminato una copia host intermedia, due
run 4K consecutive misurano 9,543 e 9,549 secondi guest; la mediana ARM della
serie alternata e 10,06 secondi. La riduzione complessiva e quindi circa il
5,1%, con 18.305 draw e hash framebuffer ancora identici all'oracolo ARM.

La catena trasformazioni usa ora anche `Mtx3x4_Multiply` (`0x0036C174`). Il
port non delega le matrici al `float` x64: usa primitive binary32 esposte dalla
stessa implementazione VFP esatta, rispettando rounding mode, default-NaN,
flush-to-zero e flag cumulativi FPSCR del processo OOT3D. La funzione legge
entrambi gli operandi prima di scrivere, preservando anche l'aliasing consentito
dal corpo originale. In 600 refresh sostituisce 264.061 chiamate ulteriori;
due run 4K consecutive misurano 9,296 e 9,317 secondi guest, circa il 7,5%
sotto la mediana ARM. Draw count, framebuffer e fallback restano invariati.

Le due routine audio dominanti sono ora compilate direttamente dal
pseudocodice completo locale: `AudioEffect_ProcessFourChannelDelay`
(`0x004A022C`) e `AudioEffect_ProcessStereoReverb` (`0x004A0338`). La
traduzione conserva esplicitamente wrap a 32 bit, `MUL`/`MLA` low word, shift
aritmetici e la particolare scalatura signed osservata nel disassembly ARM;
ring, cursori e coefficienti restano strutture guest OOT3D. Un accesso
scrivibile validato permette di elaborare i blocchi da 160 campioni senza una
chiamata al memory bus per ogni word, ma ogni intervallo viene verificato prima
della prima scrittura e i casi non rappresentabili conservano il corpo ARM.
In 600 refresh 4K le otto funzioni host totalizzano 487.986 chiamate; il tempo
guest passa da 10,097 a 7,872 secondi, una riduzione del 22,04%. Restano
identici 18.305 draw, framebuffer
`DF8375ED116DEBE8DAD86BC400EFD4AABD8F37E8228316D7A2B3D1D2789C5B7F` e
l'intero PCM DSP, ora verificato anche con FNV-1a 64
`14397512309130401104`. Questo e un avanzamento misurabile ma non raggiunge
ancora i 60 Hz reali.

L'attuale generatore denominato AOT non e l'architettura finale: emette array
di `PackedOp` predecodificati che vengono ancora attraversati da un dispatcher
e dai decoder ARM/VFP. Il percorso finale deve invece compilare direttamente
il C/C++ decompilato quando disponibile e generare C++ o LLVM operativo per il
residuo ARM, con accessi memoria guest e confini SVC espliciti. L'interprete
rimane soltanto oracolo differenziale e fallback diagnostico. La prova del
sampler `NativeCurve_SampleFloat` tradotto tramite primitive soft-float ha
peggiorato il tempo guest da circa 9,3 a 11,6 secondi ed e stata scartata: il
true-AOT non deve sostituire il dispatcher con chiamate altrettanto costose per
ogni operazione elementare.

Il contratto operativo del sostituto e fissato in
`docs/OOT3D_TRUE_AOT_CONTRACT.md`: compilazione diretta del sorgente disponibile,
traduzione statica del solo residuo A32, ABI guest unica e nessun decoder ARM
nel codice generato. Questo impedisce di considerare conclusa una semplice
predecodifica o un JIT nascosto nel runtime.

La profilazione ripetuta dopo i port audio e renderer e conservata in
`tools/oot3d/native_a32_runtime/profiles/title_intro_post_audio_1201.json`.
Rimuove correttamente dal fronte caldo le funzioni gia compilate e indica come
primo loop integer residuo `0x003247B0`, dentro `BgCheck_LineTestImpl`. Il primo
shard true-AOT traduce direttamente le cinque istruzioni del clear loop e
mantiene il self back-edge dentro la funzione host: 537.754 iterazioni in 600
refresh richiedono 1.082 ingressi AOT, senza `PackedOp` o decoder. Il record del
blocco viene marcato dal manifest versionato
`tools/oot3d/native_a32_runtime/true_aot_blocks.json`; i blocchi normali non
pagano lookup o callback aggiuntivi. Due coppie alternate 4K, nello stesso
eseguibile, misurano sempre lo shard attivo piu veloce: 6,730 contro 6,779
secondi e 7,980 contro 8,736 secondi guest. La variabilita di sistema impedisce
di attribuire una percentuale unica, ma framebuffer, 18.305 draw e PCM FNV-1a
restano identici. Il risultato valida ABI e struttura.

Il corpo dello shard viene ora emesso automaticamente dal nuovo generatore
true-AOT locale. Il manifest dichiara entry, eventuali ingressi interni ed end
esclusivo della regione; il generatore legge le istruzioni da `code.bin`, valida
il subset e produce C++ diretto per ALU integer, shift immediati, load/store,
condizioni e branch interni.
Il file generato non contiene `PackedOp` o decoder, e una istruzione non
supportata interrompe la build. La stessa prova 4K sul corpo generato conserva
1.082 ingressi, 537.754 iterazioni e zero fault, con draw, PCM e framebuffer
identici; il tempo guest osservato e 6,862 secondi. La build senza modifiche
resta a 0,13 secondi grazie alla cache separata. Il prossimo passo e selezionare
dal profilo residuo ulteriori funzioni o regioni CFG ad alto costo ed estendere
soltanto le operazioni necessarie per compilarle.

La granularita predefinita e ora funzione completa o massima regione CFG
chiusa, non basic block. La regione copia `FUN_00303B24` e stata ampliata da sei
istruzioni isolate a 18 istruzioni (`0x00303C70..0x00303CB8`) con un secondo
ingresso a `0x00303CAC`: il dispatcher entra una volta, mentre join, loop interno
e loop esterno restano control flow C++ diretto. Le transizioni true-AOT in 600
refresh scendono da 23.682 a 5.230. Tre coppie alternate 640x360 misurano 6,942
contro 6,973 secondi guest medi; la run 4K registra 6,809 secondi, zero fault,
18.305 draw e gli stessi hash PCM/framebuffer. Due regioni brevi CSAB provate in
precedenza avevano invece peggiorato lievemente la media e sono state rimosse:
la copertura non viene conservata senza resa misurabile.

Il manifest true-AOT non invalida piu l'analisi packed delle 794.924 slot. I bit
`native_candidate` vengono configurati all'avvio dagli entry point del piccolo
artifact generato; una modifica al manifest ha richiesto circa 0,58 secondi di
rigenerazione e ricompila soltanto il translation unit true-AOT. Anche il target
dei test di processo esegue ora obbligatoriamente questa generazione, evitando
di validare artifact obsoleti.

La diagnostica di `Runtime_Memcpy` ha inoltre dimostrato che tutti i 288 fallback
residui erano chiamate con sorgente uguale alla destinazione. Il percorso host
tratta ora esattamente questo alias come copia nulla conservando l'avanzamento
ABI di `r0/r1`; le sovrapposizioni reali restano nel percorso ARM diagnostico.
La title completa passa cosi da 288 a zero fallback delle funzioni compilate e
assorbe altre 17.280 byte senza cambiare memoria osservabile.

Il dispatcher risolve ora le funzioni compilate anche quando il loro entry point
e raggiunto come PC corrente, oltre che come target di `BL/BLX`. Questo chiude
tail call e ingressi diretti che prima attraversavano ancora gli shard packed:
nel caso misurato, i campioni interni di `Runtime_Memcpy` a `0x00371750`
scendono da 8.030 a zero, mentre resta soltanto l'entry `0x00371738` osservata
dal profiler prima del dispatch host. Le chiamate compilate in 600 refresh
passano da 490.996 a 558.981 senza fallback. La run profilata da 1201 refresh
scende da 16,210 a 15,685 secondi guest; la run Vulkan 4K non profilata registra
6,738 secondi con draw, PCM e framebuffer identici. Il profilo residuo e
versionato in `tools/oot3d/native_a32_runtime/profiles/title_intro_post_direct_entry_1201.json`.

Il primo incremento true-AOT VFP compila la regione calda
`NativeCurve_SampleFloat.scan_type2_keys` (`0x00308910..0x0030894C`) direttamente
dalle istruzioni di `code.bin`. I trasporti VMOV/VMRS diventano assegnamenti
statici; conversione signed binary32 e confronto usano primitive esatte FPSCR
gia condivise dal decoder, non operazioni floating point host approssimate. Tre
coppie alternate misurano 6,890 contro 6,940 secondi guest medi con 36.608
ingressi true-AOT totali e zero fault. La verifica Vulkan 4K registra 6,671
secondi, 18.305 draw, PCM FNV-1a `14397512309130401104` e framebuffer SHA-256
`DF8375ED116DEBE8DAD86BC400EFD4AABD8F37E8228316D7A2B3D1D2789C5B7F`.
Il profilo successivo e conservato in
`tools/oot3d/native_a32_runtime/profiles/title_intro_post_vfp_true_aot_1201.json`;
i precedenti hotspot `0x00308920/0x0030893C` non sono piu nel fronte caldo.

La cache degli shard non dipende piu dall'intero manifest di provenienza del
runtime. La provenienza continua a essere validata a ogni invocazione, ma solo
`code.bin`, ExHeader, inventario, boundary audit e sorgenti del generatore
invalidano l'output A32. Aggiungere o correggere una funzione host non puo quindi
rilanciare l'analisi completa del binario.

La baseline residua successiva e conservata in
`tools/oot3d/native_a32_runtime/profiles/title_intro_post_framebuffer_access_1201.json`.
Il confine sorgente include ora anche `PicaMaterialState_EmitFramebufferAccess`
(`0x00313D6C`), una leaf completa che interpreta i campi originali del materiale
e produce direttamente nel command buffer guest le sei word PICA di accesso
color/depth. Non introduce uno stato framebuffer host: tipo `0x6030`, variante
`0x6051`, flag e override restano quelli letti dalla struttura OOT3D. In 600
refresh il port assorbe 2.722 chiamate ulteriori; la run 4K conserva 18.305 draw,
PCM FNV-1a `14397512309130401104` e framebuffer SHA-256
`DF8375ED116DEBE8DAD86BC400EFD4AABD8F37E8228316D7A2B3D1D2789C5B7F`.
Il tempo guest osservato e 7,320 secondi, ma la variabilita tra run non permette
di attribuire alla singola leaf un guadagno isolato affidabile. Il profilo
aggiornato conta 918.138 campioni e conferma che il salto successivo deve essere
la generazione di regioni CFG native, non altri wrapper con numerosi rientri nel
dispatcher.

In parallelo va estesa la suite visuale ai formati color/depth PICA ancora non
osservati e alle transizioni della title intro, mantenendo il present nativo
senza interpolazione come oracolo interno. Dopo il margine stabile sopra 60 fps
va avviata l'integrazione UI attraverso il contratto gia estratto. Nessuno di
questi passaggi deve creare uno stato scena o gameplay alternativo.

Il contratto UI gia importato resta fissato alla snapshot corrente. Eventuali
nuovi allineamenti con repository esterni devono essere richiesti esplicitamente
e non fanno parte del ciclo automatico di implementazione. Lo scheletro
`N64IntegratedUiRuntime` verra registrato nei lifecycle originali con tutte le
route inizialmente guest, integrandolo nel frame reale senza contaminare il
gameplay.

### Decisione Whole-Program AOT Del 18 Luglio 2026

Le prove su funzioni e regioni progressivamente piu grandi hanno dimostrato che
il lowering C++ corrente non scala: conserva accessi virtuali, stato registro
indicizzato e callback ai callsite e puo peggiorare sensibilmente il tempo guest
pur restando bit-identico. Non viene quindi esteso come architettura di
produzione.

Il prossimo blocco implementativo e il compilatore function-level descritto in
`docs/OOT3D_WHOLE_PROGRAM_AOT_STRATEGY.md`. Riusa inventario, CFG, decoder e
semantica pinned, compila direttamente il sorgente OOT3D disponibile e abbassa
il residuo ARM in funzioni host con locali, call dirette e memoria guest veloce.
Il generatore true-AOT attuale resta una fixture differenziale per leaf chiuse.

Il primo corpo del nuovo backend e ora operativo su
`NativeCurve_SampleFloat` (`0x003087A4`, 177 istruzioni, 35 basic block). Il
generatore legge `AotProgram` e `code.bin` soltanto in build, emette branch C++
diretti, campi registro nominati e accessi tipizzati `ReadFast/WriteFast`; nel
sorgente prodotto non compaiono `PackedOp`, `ExecuteBlock` o
`ExecuteVfpScalar`. Le primitive VFP nominate sono aggiunte in un translation
unit locale che include la snapshot semantica pinned senza modificarne gli
hash di provenienza.

Un differenziale di funzione completa confronta il percorso ARM e quello C++
su registri, CPSR/FPSCR, VFP e stack. Nella title 4K da 600 refresh il corpo
gestisce 38.877 chiamate con zero fault o uscite unsupported e conserva 18.305
draw, PCM FNV-1a `14397512309130401104` e framebuffer SHA-256
`DF8375ED116DEBE8DAD86BC400EFD4AABD8F37E8228316D7A2B3D1D2789C5B7F`.
Tre coppie alternate sullo stesso eseguibile misurano `8,5311 s` ARM contro
`8,4201 s` function-level AOT medi, un miglioramento del `1,30%`. Il gate di
copertura del 10% non e ancora chiuso: il passo successivo e abbassare le call
dirette e compilare la closure renderer calda senza rientri nel dispatcher.

Il gate e stato successivamente superato con una selezione profilata automatica:
168 funzioni e 8.380 istruzioni sono ora C++ function-level reale, con call
dirette e 313 rientri dopo confini ARM. La copertura nominale della title e
`52,241%`. Tre coppie alternate 4K con audio completo riducono il tempo guest
medio da `6,8426 s` a `5,8616 s` (`-14,34%`) e il tempo processo da `11,3534 s`
a `9,9654 s` (`-12,22%`). Draw, PCM e framebuffer restano identici, con zero
fault e zero uscite unsupported. La title supera appena 60 refresh/s ma manca
ancora il margine abbondante richiesto; le prossime closure devono rimuovere i
confini VFP, doubleword, SVC e indiretti rimasti.

Il profilo residuo post-closure ha poi guidato un secondo incremento accettato.
Il backend risolve jump table interne e conversione signed binary32 esatta; la
selezione sale a 178 funzioni e 9.949 istruzioni. Due run 4K senza cattura
misurano `5,8082 s` guest e `9,9740 s` processo medi, mantenendo invariati
18.305 draw, PCM e framebuffer. `SVC`, `VSQRT` e `LDRD/STRD` sono decodificati,
ma le 14 funzioni che diventano compilabili con questi ultimi lowering restano
escluse dal PGO predefinito: le prove misurano `6,3189 s` per la coorte completa
e `6,0103 s` per il sottoinsieme senza rientri ARM. Il target supera quindi i
60 refresh/s solo di poco e il criterio di margine abbondante resta aperto.

### Chiusura Del Collo Di Bottiglia GSP/PICA Del 19 Luglio 2026

La closure function-level selezionata e stata portata a 182 funzioni e 10.287
istruzioni. Il dispatch usa direttamente `NativeA32Memory` nel percorso tipizzato
e non ripete RTTI a ogni ingresso; inoltre i confronti con gli hook estesi della
queue restano fuori dal percorso normale quando la diagnostica e disattivata.
Questi incrementi conservano il fallback ARM come oracolo e non introducono
implementazioni gameplay host.

La successiva profilazione opt-in `--profile-a32-runtime` ha indicato che
l'audio non era il limite. La prima implementazione di `--disable-audio`
saltava pero anche processing e completion DSP guest, quindi le misure ottenute
con quel flag non erano un confronto funzionalmente valido. Dei circa 3,0
secondi trascorsi in `svcSendSyncRequest`, quasi tutto apparteneva invece a
3.903 richieste `gsp::Gpu:TriggerCmdReqQueue`. La lettura bulk di 31,75 MiB di
command list richiedeva solo 3,2 ms, mentre `SubmitGspCommand` richiedeva 2,947
secondi. Il collo di bottiglia era quindi nel consumer realtime PICA e non in
A32, audio, copia guest o hardware PC.

Il frontend conservava una cronologia diagnostica completa anche nel player:
ogni draw duplicava registri, programmi e swizzle VS/GS, uniform, LUT fog e
attributi default. Con 18.305 draw si accumulavano oltre 1,2 GiB di snapshot,
piu le ricopie causate dalla crescita dei vettori. La cronologia e ora una
funzione esplicita, attiva per probe e test ma disattivata nel runtime di
produzione. Lo stato PICA corrente, il packet consegnato al sink e la sequenza
degli interrupt restano invariati e sono coperti da un test dedicato.

Il secondo costo era l'ownership della submission queue. Il vecchio
`TakePendingDraws()` spostava fuori l'intero vettore e lasciava la queue senza
capacita; ogni ciclo successivo riallocava e ricopiava packet da circa 70 KiB.
Il nuovo drain a doppio buffer scambia storage riutilizzabile tra producer e
consumer, mentre `SubmitDrawPacket` costruisce la submission direttamente nel
suo slot finale. Non viene omessa o messa in cache alcuna risorsa nativa: indici,
vertex loader e texture vengono ancora snapshotati al confine draw.

Il profilo dopo queste correzioni misura `SubmitGspCommand` a 0,407 secondi e
il sink a 0,310 secondi; l'accodamento scende da 1,228 a 0,138 secondi. Cinque
run normali Vulkan a 3840x2160 con audio completo misurano una mediana di
4,893 secondi di fase per 600 refresh, pari a 122,6 refresh/s steady, e 6,699
secondi per l'intero processo, pari a 89,6 refresh/s includendo bootstrap e
chiusura. Il gate con cattura framebuffer interna misura ancora 117,4
refresh/s steady.

La resa rimane bit-identica: 600 VBlank, 18.305 draw, 2.045 frame DSP, PCM
FNV-1a `14397512309130401104` e framebuffer SHA-256
`DF8375ED116DEBE8DAD86BC400EFD4AABD8F37E8228316D7A2B3D1D2789C5B7F`.
Il requisito di margine abbondante sopra 60 FPS per la title intro 4K e quindi
chiuso sul percorso Vulkan corrente. Restano separati i requisiti di completezza
visiva e temporale dell'intera intro, l'integrazione UI single-screen e la
copertura del resto del gioco; questo risultato non li dichiara implicitamente
completi.

Il gate esteso della prima milestone esegue ora tutti i 1.201 refresh della
fixture title a 3840x2160. Con cattura framebuffer interna impiega 10,314
secondi di fase, pari a 116,4 refresh/s steady, e conserva esattamente i
43.441 draw, 1.185 display transfer, 591 snapshot completi, 579 sample visuali
intermedi e 11 discontinuita/camera cut della baseline precedente. L'audio
produce 4.094 frame DSP e il processo non incontra callback o completion
mancanti.

La cattura esplicita del frame 1200, distinta dal gate iniziale del frame zero,
mostra in 16:9 Hyrule Field, Link adulto ed Epona, cielo, terreno, logo OOT3D
composito con effetto animato e copyright. Il suo SHA-256 e
`655A8C8D30208334CF9EAFD3BF4F2689A5AB99BC83F497D5293AD5AD6FC8B041`.
La prima milestone richiesta, riproduzione title intro/logo completa in Vulkan
16:9 4K con margine sopra 60 FPS, e quindi chiusa. Questo non chiude la
Milestone 1 generale: il prossimo workflow deve far avanzare il processo dal
title al file select e integrare il primo lifecycle UI single-screen previsto
dal contratto.

### HID Nativo E Ingresso Nel File Select Del 19 Luglio 2026

Il runtime implementa ora il producer HID CTR completo sullo shared memory
guest: ring pad e touch, indici, additions/removals, circle pad, timestamp,
eventi one-shot e cadenza nativa di 234 Hz. Le scritture appartengono al
servizio host e non allentano le protezioni viste dal processo guest. Una
timeline deterministica separata dall'input fisico consente gate ripetibili;
mouse e tastiera continuano a produrre lo stesso ABI HID e non chiamano logica
gameplay host.

Il producer HID e verificato fino allo stato input consumato dal gioco: `A`
produce il bit `1`, `Start` il bit `8`, con transizioni held/pressed/released
coerenti. La precedente conclusione secondo cui dieci refresh di `A` tra i
frame 1210 e 1219 raggiungessero il file select era pero errata. Le chiamate a
`FileSelect_Update` non provano l'attivazione: la decompilazione canonica di
`0x0042F9A8` legge prima `gFileSelectState + 0x10` e ritorna quando vale zero.
Le run osservate mantengono quel controller state a zero. Un impulso anticipato
di `A` al frame 900 percorre invece lo skip della intro e non deve essere usato
come gate del file select.

Il gate di produzione a 3840x2160, Vulkan e audio attivo esegue 1.451 refresh in
12,736 secondi di fase, pari a 113,9 refresh/s. Registra 55.066 draw, 1.435
display transfer, 4.946 frame DSP, 1.394.437 campioni canale non nulli e nessuna
submission o completion pendente. Il dato storico di 128,2 refresh/s con audio
disattivato e invalidato: quel percorso sopprimeva anche interrupt e scheduling
DSP guest e, nelle run lunghe, saturava la coda renderer.

`--disable-audio` ora muta esclusivamente il sink host. `ProcessFrame` e
`SignalDspAudioFrame` continuano sempre, preservando il control flow OOT3D. Una
coppia 4K da 600 refresh misura 128,47 refresh/s con output e 128,98 muta
(`+0,40%`), con gli stessi 18.305 draw, 585 transfer, 2.045 frame DSP, PCM
FNV-1a `14397512309130401104` e zero code pendenti. L'audio non e quindi il
collo di bottiglia realtime. Una seconda coppia sul binario finale misura
128,601 refresh/s con output e 128,556 muta (`-0,035%`), confermando che la
differenza e interamente nel rumore di esecuzione.

Il gracchiare osservato nell'output non era un costo del mixer. Il percorso
Vulkan `MAILBOX` lasciava avanzare un VBlank guest per ogni frame host senza
pacing: una run di cinque secondi reali eseguiva 491 refresh e produceva 8,179
secondi di PCM. Il buffer WASAPI si saturava e `DoPlay` troncava ripetutamente
la coda, introducendo discontinuita pur mantenendo corretto il PCM generato.

Il runtime interattivo usa ora un fixed timestep realtime a 60 Hz, con recupero
limitato degli spike e senza recuperare indefinitamente un processo in ritardo.
Le run con un frame limit esplicito restano volutamente non limitate per
misurare il margine prestazionale. Il prebuffer host e calcolato dal sample rate
nativo per almeno 100 ms e WASAPI avvia il device al raggiungimento del valore
`DesiredBuffered`, non a una soglia costante di 1.500 frame.

Il gate realtime da dieci secondi misura 600 refresh in 10,008 secondi, 327.200
frame stereo in 2.045 frame DSP, durata PCM 9,998 secondi, zero osservazioni di
starvation e nessun resync del clock. Il PCM resta identico alla baseline:
FNV-1a `14397512309130401104`, peak `22241`, 18.305 draw e code PICA vuote. Il
gate 4K non limitato resta a 109,64 refresh/s steady con audio guest e sink
attivi, quindi la correzione realtime non nasconde una regressione di capacita.

La seconda causa di interruzioni era la creazione sincrona delle pipeline
Vulkan incontrate per la prima volta. Il backend conserva ora un
`VkPipelineCache` validato su vendor, device, versione driver e
`pipelineCacheUUID`; tutte le pipeline Fast3D, PICA scanout, PICA native e
Shadow2D usano la stessa cache, salvata a shutdown e riaperta al processo
successivo. Dati assenti, corrotti o incompatibili vengono ignorati e
ricostruiti senza cambiare shader o comandi guest.

WASAPI gestisce inoltre un underrun effettivo fermando e resettando il client,
riprimando il buffer fino a `DesiredBuffered` e applicando un fade-in di 128
frame esclusivamente al primo blocco di recovery. Non continua quindi a
riprodurre piccoli blocchi separati da nuovi underrun. Il cold gate 4K esercita
questo percorso durante la compilazione iniziale; il gate immediatamente
successivo misura 600 refresh in 10,008 secondi, zero starvation, hash PCM
`14397512309130401104` e peak `22241`. Il gate warm esteso resta stabile per
1.800 refresh in 30,018 secondi (59,96 Hz), 70.636 draw, zero starvation, zero
resync e zero code renderer residue. I test dell'audio nativo risolvono inoltre
1.524/1.524 sequenze e validano BCWAV, BCSTM, DSP-ADPCM e profili `code.bin`.

Il prossimo gate di navigazione deve identificare dal codice originale la
condizione e la sequenza che attivano realmente `gFileSelectState + 0x10`, poi
richiedere un valore nonzero nel report. Il renderer UI puo avanzare in shadow,
ma nessuna route guest va soppressa sulla sola evidenza delle chiamate inactive.

### Primo Confine UI N64 Integrato Del 19 Luglio 2026

`oot3d_ui_n64` e ora un modulo separato dal processo A32 e dal gameplay. La sua
prima implementazione, `N64IntegratedUiRuntime`, implementa direttamente
`UiBackend`, ma usa il profilo `n64-integrated-shadow`: tutte le responsabilita
restano al guest, non viene consumato input e non viene emessa presentazione
host. Il modulo non include `NativeA32Memory`, layout guest o indirizzi OOT3D;
riceve il file-select e gli altri contenuti esclusivamente attraverso
`UiBackendStateView`.

`Oot3dNativeUiLifecycleBridge` e l'adapter distinto autorizzato a leggere la
memoria. Ricava i 63 entrypoint dall'inventario tipizzato importato, non da una
seconda lista nel player, costruisce le radici verificate e cattura lo stato
semantico una sola volta per host frame in cui viene raggiunto almeno un
lifecycle. Il dispatcher continua poi nel corpo guest. Un test con memoria A32
protetta dimostra sia il passaggio della proiezione file-select al runtime N64,
sia la deduplicazione per frame, senza accessi guest dal modulo UI.

Sul workflow title con lifecycle file-select ancora inattivo il bridge osserva
20.501 ingressi e 919 snapshot, per 533.939 campi letti e zero mancanti. Tutti
gli ingressi sono instradati al guest e nessuno all'host. Il gate Vulkan
3840x2160 con audio completo esegue 1.451 refresh in 12,375 secondi di fase,
pari a 117,25 refresh/s, conservando 55.066 draw, 1.435 transfer, 4.946 frame
DSP e PCM FNV-1a `14467918251690428024`. Questi numeri verificano il confine e
non dichiarano raggiunto il file select.

Il confine e quindi collegato al frame reale, ma la sostituzione non e ancora
iniziata. Il prossimo blocco deve implementare contenuto, navigazione e
presentazione del file select nel runtime N64, aggiungere gli action request
tipizzati verso le operazioni save OOT3D e solo allora promuovere atomicamente
le quattro route `FileSelect` da guest a host.

### Presentazione File Select N64 In Shadow Del 19 Luglio 2026

`N64IntegratedUiRuntime` produce ora una lista tipizzata di primitive per il
file select solo quando il controller state originale e noto e nonzero. Il
layout usa un canvas canonico 320x240 centrato nel 16:9 e nomi semantici per
title, tre slot, name box, copy, erase, options e highlight. Selezione e slot
popolati provengono unicamente da `UiBackendStateView`; non esistono letture di
memoria guest o decisioni gameplay nel modulo UI.

`N64UiFast3dRenderer` traduce le primitive tipizzate in draw Fast3D nello
stesso framebuffer Vulkan/OpenGL. Il precedente ingresso
`--n64-ui-archive` e il relativo fallback OTR sono stati rimossi: le texture
sono risolte esclusivamente dai payload CTXB/PICA OoT3D o da override OoT3D
espliciti; cache, blending, ordinamento e lifetime GPU restano espliciti.

Il lifecycle bridge conserva l'ultimo stato semantico e genera la presentazione
per i subsystem gia attivati dal lifecycle guest. La UI mantiene cosi a ogni
refresh l'ultimo stato autorevole prodotto dal relativo dominio temporale,
senza inventare transizioni o scrivere memoria OOT3D. I test
`oot3d_n64_integrated_ui_runtime_tests`,
`oot3d_native_ui_lifecycle_bridge_tests` e
`oot3d_n64_ui_renderer_tests` coprono layout, gate inactive, adapter e draw.
La route resta `n64-integrated-shadow`: il renderer e pronto, ma il gate visuale
end-to-end richiede ancora l'attivazione nativa nonzero descritta sopra.

### Attivazione Nativa E Compositing File Select Del 19 Luglio 2026

Il passaggio reale e stato ricostruito dal control flow OOT3D, senza usare
`FileSelect_Update` come falso indicatore. Quando il game mode nativo a
`0x00588E3C` vale `2`, il completamento transizione a `0x002E2E60` imposta come
prossimo GameState `FileChoose_Init` (`0x00450B60`, size `0xA64`). Questa init
chiama `FileSelect_Activate(0)` a `0x002E7AE0`, che porta il controller state
globale `0x00504FA0 + 0x10` a un valore nonzero.

La sequenza deterministica minima usa due impulsi `Start`: frame 100..109 per
saltare la intro ed entrare nel title, frame 300..309 per entrare nel
FileChoose. Il controller file-select diventa attivo attorno al frame host 403.
Le fixture versionate sono:

- `tools/oot3d/native_game_runtime/input_timelines/title_to_file_select.json`;
- `tools/oot3d/native_game_runtime/input_timelines/title_to_file_select_second_slot.json`.

La seconda attende la conclusione dell'animazione d'ingresso e applica il
Circle Pad verso il basso ai frame 520..523. Il codice guest cambia quindi lo
slot selezionato da `0` a `1`; un input anticipato durante l'ingresso viene
ignorato dal comportamento originale.

Il file-select N64 e ora realmente visibile sopra lo scanout PICA Vulkan. Il
renderer usa lo shader Fast3D standard con matrice clip-space esplicita; il
backend ripresenta l'ultimo display transfer e apre un render pass overlay con
`LOAD_OP_LOAD`, quindi non cancella ne viene cancellato dallo scanout. I
semafori render-finished appartengono alle immagini di swapchain e acquire e
present sono serializzati; una run con `VK_LAYER_KHRONOS_validation` non produce
errori. La cattura diretta della swapchain tramite
`VK_LAYER_LUNARG_screenshot`, distinta dagli screenshot Windows, mostra sia lo
slot 1 sia lo slot 2 correttamente evidenziati.

Il gate Vulkan 3840x2160 da 560 refresh con audio completo misura 4,917 secondi
nelle fasi realtime, pari a 113,89 refresh/s. Esegue 15.288 draw PICA e compone
1.570 primitive N64 in 157 frame file-select, con slot guest finale `1`, zero
texture UI mancanti e nessun adattamento degli asset OOT3D.

Questo chiude attivazione, stato semantico, presentazione e navigazione guest
del primo workflow UI. Non chiude ancora la sostituzione UI: la route resta
shadow finche navigazione N64, dispatch tipizzato delle operazioni native
load/copy/delete e soppressione atomica del composite OOT3D non sono verificati
end-to-end.

### Diagnostica PCM E Trasporto WASAPI Lossless Del 19 Luglio 2026

Il runtime dispone ora di `--audio-pcm-dump <capture.wav>` e misura continuita,
clipping e salti alle frontiere dei frame DSP da 160 campioni. La cattura prima
di WASAPI ha confermato che il mixer OOT3D produce PCM stereo continuo a 32.728
Hz: zero campioni full-scale, zero delta sopra 4.096, delta massimo 3.494 e
delta massimo alle frontiere DSP 2.617. Il gracchiare residuo non giustificava
quindi modifiche a decoder, mixer, sample rate o clock guest.

Il difetto era nel trasporto push WASAPI: quando il buffer device aveva meno
spazio del blocco ricevuto, il backend copiava solo il prefisso disponibile e
scartava definitivamente la coda. `WasapiAudioPlayer` mantiene ora una coda PCM
software, include quei frame in `Buffered()` e consuma un blocco solo dopo che
WASAPI lo ha accettato. Il recovery da underrun gia esistente resta invariato e
non altera i campioni conservati.

Il gate Vulkan 3840x2160 successivo esegue 1.801 refresh in 30,024 secondi,
6.140 frame DSP e 982.400 frame stereo, con zero starvation, zero resync del
clock e code PICA vuote. La suite audio continua a risolvere 1.524/1.524
sequenze e a validare BCWAV, BCSTM, DSP-ADPCM e profili derivati da `code.bin`.

### Creazione File E Primo Avvio Nativo Del 19 Luglio 2026

File Select e Name Entry N64 sono ora composti nel framebuffer a partire dallo
stato semantico OOT3D. Nome, cursore, pagina, selezione e conferma provengono
dal guest; l'alfabeto visualizzato usa le tabelle UTF-16 localizzate OOT3D e le
risorse grafiche N64. Le operazioni restano native OOT3D: `FSUSER CreateFile`,
write, resize, flush e `ControlArchive` materializzano `save00.bin` nella
directory persistente passata con `--save-data`.

La fixture `title_to_new_file_start.json` percorre title, File Select, Name
Entry, conferma, creazione del file e primo avvio. Il gate Vulkan ha prodotto
un `save00.bin` da 5.340 byte, poi riletto e validato dal guest, ed e arrivato
al prologo originale "In the vast, deep forest of Hyrule..." senza IPC non
gestite. Le catture framebuffer di riferimento sono in
`I:\oot3dre_work\native_game\new_file_gate_20260719k`.

I binding interattivi correnti sono: `Enter` Start, `Space` o `Z` A, `X` B,
`C` X, `V` Y, `Q`/`E` L/R, frecce D-pad, `WASD` Circle Pad, `Tab` o
`Backspace` Select ed `Esc` uscita. Il launcher usa per default
`I:\oot3dre_work\native_game\oot3d_native_savedata`, cosi i file creati
sopravvivono alle run successive.

### Puntatore Dei Menu OOT3D Single-Screen Del 19 Luglio 2026

File Select, Name Entry e gli altri frontend nativi ricevono ora il click del
mouse come touch OOT3D. Backend e input condividono un unico aspect-fit: Vulkan
colloca il framebuffer inferiore `320x240` nel target host e HID applica la
trasformazione inversa alla posizione del puntatore. I pillarbox non sono
interattivi, i bordi destro/inferiore non producono coordinate fuori range e un
puntatore esterno o rilasciato genera `TouchPressed = false`.

Il mapping e attivo soltanto quando il lifecycle riconosce un frontend nativo;
non introduce hitbox host, coordinate per-menu o sostituzioni della tastiera.
L'hit-test, la selezione, le animazioni e le operazioni restano nel codice
OOT3D. Un evento touch promosso dal backend finestra attraversa lo stesso
percorso pointer; le timeline deterministiche continuano invece a possedere
interamente il frame HID durante i gate automatici.

I test coprono 4:3, 720p, 1080p, 4K, centro, estremi inclusi, pillarbox esclusi,
release e dimensioni host non valide. Il gate Vulkan da 560 refresh raggiunge
File Select con 155 overlay nativi, 2.474 draw di compositing e nessuna coda PICA
residua.
