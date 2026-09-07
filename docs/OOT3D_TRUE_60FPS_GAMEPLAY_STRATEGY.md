# OOT3D true 60 FPS gameplay strategy

Il lavoro e sospeso al checkpoint funzionale `30a1b92fa`. Stato,
cronologia completa, evidenze decompilate e procedura di ripresa sono in
`docs/OOT3D_TRUE_60FPS_HANDOVER.md`.

## Decisione

La feature non deve essere implementata come semplice
`--simulation-rate 60`, come raddoppio indiscriminato di `GameState_Update` o
come dimezzamento globale delle costanti. Queste soluzioni fanno funzionare
correttamente solo il sottoinsieme del codice che consuma il rate nativo e
accelerano timer, state machine, cutscene, eventi audio e RNG che avanzano di
una unita per update.

La soluzione raccomandata e un percorso source-recompiled opzionale con:

1. simulazione continua realmente eseguita a 60 Hz;
2. `nativeUpdateRate = 1`, valore previsto dall'ABI temporale OOT3D;
3. timeline dei frame logici originali che avanza di `0.5` per update;
4. timer ed eventi discreti preservati nelle loro unita originali;
5. nessun replay o interpolazione tra due frame PICA;
6. un draw OOT3D nuovo per ogni update a 60 Hz;
7. fallback A32 trattati come copertura transitoria esplicita, non come
   soluzione finale.

Questo e vero gameplay a 60 Hz: input, movimento, gravita, collisione,
animazione, root motion, camera continua e rendering vengono calcolati ogni
`1/60` di secondo. Un evento discreto progettato per avvenire dopo 30 frame
originali continua ad avvenire dopo un secondo, non dopo mezzo secondo.
Conservare l'unita temporale di un evento non e interpolazione visiva.

Il primo target deve essere esattamente 60 Hz. Frequenze arbitrarie o superiori
possono essere affrontate dopo la chiusura del percorso 60 Hz: il campo A32
originale rappresenta esattamente i rate interi `2` e `1`, corrispondenti a 30
e 60 Hz, mentre il percorso C++ potra in seguito usare rate frazionari.

## Stato verificato

### Runtime di integrazione

Stato letto sul branch `renderer/oot3d-native-integration`, commit
`562e54232f84`.

Il runtime possiede gia:

- un clock CTR da 60 unita al secondo;
- `NativeFrameRateContract`;
- il rate originale `2` a 30 Hz e il rate `1` a 60 Hz;
- un prototipo CLI `--simulation-rate 60`;
- uno scheduler che separa presentazione, refresh guest e simulazione;
- interpolazione e replay PICA per presentare a 60 Hz la simulazione a 30 Hz;
- primitive gameplay C++ che accettano un rate floating;
- checkpoint e savestate sufficienti per confronti deterministici.

Il prototipo attuale non e una feature 60 Hz completa. Scrivere `1` a
`gOot3dGameState + 0x110` scala correttamente molti helper, ma non modifica il
significato dei contatori aggiornati con `+1` o `-1`.

Le prove storiche in
`native_game/frame_rate_equivalence/` mostrano:

| Caso | Update | Draw | Presentazioni |
| --- | ---: | ---: | ---: |
| 30 Hz, 60 refresh | 30 | 1.350 | 60 |
| 60 Hz, 60 refresh | 60 | 2.673 | 60 |

Il test `Test-Oot3dFrameRateEquivalence.ps1` confronta circa un secondo e un
insieme limitato di campi Player. Non verifica la durata di timer e cutscene,
la sequenza RNG, gli eventi degli attori, l'audio o le transizioni di scena.
Il suo esito positivo dimostra che alcune primitive scalate sono valide a rate
`1`; non certifica il gioco a 60 Hz.

Una misura storica della cutscene Kokiri ha raggiunto circa 57,82
presentazioni al secondo con 480 update e 78.800 draw. E una prova utile di
workload, non una misura aggiornata del renderer NRI e non un gate di
correttezza. Indica comunque che il percorso 60 Hz deve essere profilato
separatamente: genera quasi il doppio del lavoro guest rispetto ai 30 Hz, ma
deve anche eliminare il costo di matching, snapshot e replay usato
dall'interpolazione.

### Decompilazione C/C++

Stato letto in sola lettura da `I:\oot3decomp`:

- branch `main`;
- ultimo commit osservato `84df7e1`;
- 9.242 funzioni con pseudocodice C-like riproducibile;
- 342 funzioni con C mantenuto e collegato al target, pari al 3,7005%;
- 393 funzioni con C mantenuto o anchor esatto, pari al 4,2523%;
- 8.849 funzioni ancora senza C mantenuto o anchor esatto;
- working tree in evoluzione con ulteriore lavoro non ancora committato su
  matematica e collisione.

Il working tree sporco della decompilazione non deve essere modificato o usato
come dipendenza live. Le importazioni nel runtime devono provenire da una
revisione committata e devono registrare revisione, hash e provenienza.

La percentuale di C pulito e ancora bassa, ma la copertura attuale e
strategicamente utile per il 60 Hz. Sono gia leggibili:

- `SkelAnime_Update`, campionamento CSAB, morph e frame crossing;
- movimento base, gravita e posizione degli attori;
- spawn, inizializzazione e lifecycle degli attori;
- collisione Player e varie primitive di collisione;
- matematica `Approach`, `Step`, `SmoothStep`, trigonometria e RNG;
- submission di skeleton e modelli;
- parti semantiche di camera, cutscene, ambiente e intro.

Restano invece incompleti proprio alcuni owner graph necessari per una
conversione temporale globale:

- update proprietario `GameState/PlayState`;
- confine completo `Actor_UpdateAll`, ancora fuso;
- update principale del Player;
- ownership completa di cutscene, camera, ambiente ed effetti;
- molti update specifici degli attori.

Il simbolo attuale `oot3d_player_action_turn_in_place` a `0x00250AD0` non
descrive la sua vera estensione: il corpo decompilato ha 1.647 righe, 88
chiamate e contiene gran parte dell'update Player. Nello stesso corpo convivono
letture del rate `+0x110`, decrementi interi, state machine e chiamate RNG. E
un owner da promuovere e tipizzare come grafo completo, non una leaf da
agganciare al dispatcher.

Un inventario grezzo dell'intero export Ghidra rileva:

- 9.242 file funzione;
- 445 funzioni candidate che accedono all'offset temporale `+0x110`;
- 369 funzioni con la forma di conversione floating osservata per quel rate;
- 652 funzioni che citano le API RNG gameplay;
- 2.747 righe di chiamata candidate alle API RNG;
- migliaia di incrementi, decrementi e confronti interi.

Gli ultimi conteggi sono intenzionalmente solo candidati: includono loop e
indici che non sono timer. Servono a dimostrare che una sostituzione testuale
globale non e affidabile e che occorre classificare i siti nel call graph.

## Evidenze temporali native

### Rate autorevole

`GameState_Init` inizializza a `2` il campo temporale a offset `+0x110`.
OOT3D esegue normalmente 30 update al secondo:

```text
30 update/s * 2 unita/update = 60 unita native/s
```

A 60 Hz:

```text
60 update/s * 1 unita/update = 60 unita native/s
```

Il valore `1` non e una costante inventata dal port. E la conseguenza diretta
del dominio temporale osservato nel codice OOT3D.

### Sottosistemi gia predisposti

`SkelAnime_Update` moltiplica la velocita per il rate nativo. I percorsi queued
usano una scala `0.5`, quelli direct una scala `1/3`. I frame CSAB sono
floating e il campionatore usa interpolazione lineare, Hermite e quaternion
secondo il formato originale. A rate `1` la posa puo quindi essere valutata a
mezzo passo senza inventare frame o modificare gli asset.

`Animation_OnFrameImpl` ricostruisce l'intervallo fra frame precedente e frame
corrente. Questa e la semantica da conservare per notifiche e SFX: un evento si
attiva quando il suo frame viene attraversato, non perche il frame corrente e
esattamente uguale a un intero.

`Actor_MoveForward` scala gravita e spostamento con il rate. Due passi a rate
`1` mantengono la velocita reale di un passo a rate `2`. L'integrazione
semi-implicita puo produrre una traiettoria numericamente non identica: un vero
substep a 60 Hz e piu fine e deve essere confrontato con tolleranze fisiche,
non imponendo uguaglianza bit per bit.

Le primitive `Math_*` leggibili scalano gli step con il rate. Anche in questo
caso due mezzi passi non sono sempre bit-identici a un passo intero, in
particolare per approcci esponenziali e arrotondamenti signed-16.

### Sottosistemi non predisposti globalmente

`GameState_Update` chiama l'owner virtuale e incrementa `GameState+0xF8` di
uno. Eseguendolo 60 volte, quel contatore raddoppia la propria velocita.

La collisione Player mantenuta contiene contatori `u8` per stabilita di
pavimento e ledge, ma il binario mostra che non appartengono allo stesso
dominio temporale:

- `Player+0x2489` e il timer del tipo di pavimento, mentre `Player+0x248B`
  conserva il tipo precedente. La coda della collisione lo incrementa di uno
  quando il tipo non cambia e lo azzera immediatamente quando cambia. Il
  consumer usa le soglie statiche `120` e `60`: questo e un timer logico
  originariamente a 30 Hz e a 60 Hz deve avanzare solo attraversando un frame
  logico;
- `Player+0x2279` e il timer ledge. Il codice calcola il limite come
  `round(300.0 / nativeUpdateRate)` e incrementa il byte a ogni tick di
  simulazione. Il grafo dei consumer e ora ricostruito: le tre decisioni
  osservate dipendono da durate native di `26`, `18` e `8` unita temporali.
  Il source state e quindi allargato e misurato in unita temporali native;
  l'adattatore A32 mantiene il byte ABI senza sottoporlo al gate del timer
  pavimento.

L'update Player a `0x00250AD0` contiene gia nelle prime righe numerosi timer
byte decrementati di uno, poi altri contatori e chiamate RNG. La stessa
funzione contiene anche operazioni continue scalate da `+0x110`.

Il frame owner della cutscene a `0x00321F50` incrementa il frame `u16` di uno e
chiama `Cutscene_ProcessCommands`. L'interprete a `0x002C5BA0` contiene molti
confronti di uguaglianza e azioni one-shot, fra cui audio, BGM, flag, camera e
illuminazione. Chiamarlo due volte senza cambiare semantica dimezza la durata
della cutscene e puo duplicare o saltare eventi.

`Camera_Update` a `0x002D84C4` combina solve continuo, helper scalati dal rate,
contatori incrementali e timer decrementati di uno. La camera non puo essere
corretta con un solo moltiplicatore esterno.

### Primo sottografo temporale Player implementato

Il primo sottografo chiuso e il timer del tipo di pavimento. L'evidenza
binaria usata e:

```text
code.bin: E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin
image base: 0x00100000
size: 4567040 byte
SHA-256: 16A6B0AA4C4784680220A6F780F7F8A73CFB205557AA9F9F0E705179E0613220
Player owner: 0x00250AD0
scene collision owner: 0x0032EEB4
callsite/return A: 0x00251704 / 0x00251708
callsite/return B: 0x00251DD8 / 0x00251DDC
floor timer/type: Player+0x2489 / Player+0x248B
consumer threshold table: 0x0053A092 = { 120, 60 }
```

`PlayerUpdateFloorTypeTimer` contiene ora la semantica C++ tipizzata:
l'identita del pavimento cambia su qualunque tick di simulazione, mentre la
durata di stabilita avanza soltanto su `CrossedLogicalFrame`. In attesa della
promozione completa dell'owner Player, `NativeA32PlayerTemporalBridge` e un
adattatore temporaneo e circoscritto al confine della collisione. In modalita
Enhanced60:

1. acquisisce timer e tipo all'ingresso di `0x0032EEB4`;
2. lascia eseguire senza modifiche collisione, movimento e risposta continui;
3. riconcilia il timer ai due soli return osservati;
4. riscrive il byte esclusivamente se la mutazione A32 coincide esattamente
   con la semantica nativa riconosciuta;
5. conserva mutazioni sconosciute e le conta come mismatch;
6. non installa hook ne aggiunge costo nel percorso Native30.

Il confronto dallo stesso savestate, con 40 presentazioni e delta fisso
`1/60`, ha prodotto:

| Misura | Native30 | Enhanced60 |
| --- | ---: | ---: |
| update gameplay | 20 | 40 |
| ingressi/return collisione instrumentati | bridge disattivo | 40 / 40 |
| avanzamenti logici timer | 20 | 20 |
| timer finale / tipo precedente | 193 / 0 | 193 / 0 |
| action finale | 4957496 | 4957496 |
| posizione finale | 154.194473, 0, 415.596008 | 154.194473, 0, 415.596008 |
| frame animazione finale | 133.999847 | 133.999802 |

In Enhanced60 il bridge ha contato 20 return logici, 20 intermedi e 20
riconciliazioni, senza mismatch, return non abbinati o errori di memoria. Gli
artifact sono:

```text
I:\oot3dre_work\native_game\frame_rate_audit\
  player_floor_timer_native30.json
  player_floor_timer_enhanced60.json
```

Questa e una prova semantica del primo campo, non una misura prestazionale:
entrambi i report registrano `whole_aot_requested=true` ma
`whole_aot_available=false`. Il bridge resta una compatibility island e non
vale come completamento dell'owner Player; verra eliminato quando collisione e
Player saranno interamente nel percorso C++ tipizzato.

### Secondo sottografo temporale Player implementato

Il secondo sottografo chiuso e l'eta del contatto con una sporgenza, composta
da `Player+0x2278` (tipo) e `Player+0x2279` (timer). Sullo stesso `code.bin`
verificato per il timer pavimento, il writer in `0x0032EEB4`:

```text
reset timer:                    0x0032F6C8
confronto/cambio tipo:          0x0032F6CC / 0x0032F6D8
reset condizionale:             0x0032F70C
lettura timer:                  0x0032F738
incremento/store:               0x0032F75C / 0x0032F760
costanti limite:                0x0032FA24 = 300.0
                                0x0032FA28 = 0.5
```

Il limite calcolato e `round(300.0 / nativeUpdateRate)`: vale `150` a 30 Hz,
cioe cinque secondi, e nominalmente `300` a 60 Hz. Il consumer completo
`0x0023C7AC` contiene tre gate:

| Gate | Confronto OOT3D | Primo timer valido a 30 Hz | Durata nativa |
| --- | --- | ---: | ---: |
| prompt parete dinamica | `0x0023C8A4`, `timer > 12` | 13 | 26 |
| sporgenza alta | `0x0023C8F0`, `round(15/rate) < timer` | 9 | 18 |
| salto sporgenza bassa | `0x0023CAB0`, `round(6/rate) < timer` | 4 | 8 |

Le durate includono l'ordine originale: il consumer legge il timer prima che
la collisione lo incrementi nello stesso update. A 60 Hz i gate diventano
quindi `26`, `18` e `8` tick, conservando gli stessi istanti reali senza
riusare le diverse costanti N64.

`PlayerLedgeContactTimerState` conserva l'eta fino a `300` in un campo C++
allargato. `PlayerProjectLedgeContactTimerToWire` espone ancora il byte ABI:
a 60 Hz lo satura a `255` invece di lasciarlo avvolgere. Tutti i consumer
provati hanno soglie molto inferiori a `255`, quindi questa proiezione e
semanticamente equivalente per il grafo noto; la serializzazione dello stato
allargato diventera necessaria quando l'owner Player sara promosso per intero.

Nel frattempo `NativeA32PlayerTemporalBridge`:

1. riconosce cambio tipo, reset, reset seguito da incremento e incremento
   ordinario nel writer originale;
2. interviene sul byte solo per impedire il wrap `255 -> 0`;
3. valuta i tre gate in unita temporali native e lascia eseguire i branch e
   tutte le relative azioni originali;
4. espone contatori distinti sotto `player_temporal_bridge`;
5. resta installato soltanto in Enhanced60.

I test isolati coprono entrambe le frequenze, i sei confini appena prima/sulla
soglia, saturazione, overflow ABI, reset e cambio tipo. Il confronto dallo
stesso savestate ha inoltre confermato 20/40 update a parita di 20 frame logici,
stesso stato Player finale e nessun mismatch, ma quel checkpoint idle non
attraversa naturalmente un ledge:

```text
I:\oot3dre_work\native_game\frame_rate_audit\
  player_ledge_timer_native30.json
  player_ledge_timer_enhanced60.json
```

Serve ancora un replay naturale di staccionata/sporgenza per validare la
sequenza completa di action, contatto e animazione. I report indicano inoltre
`whole_aot_available=false`: anche questo incremento dimostra semantica, non
prestazioni, e non rende completo il grafo Player.

### Classificazione del body-shock timer

Il campo `Player+0x227D` non richiede un adattatore a frame logici. I due
writer OOT3D osservati sono:

```text
Player_UpdateDamageAndHazards:          0x00132DD0
Player_Action_UpdateTimedHealthDrain:  0x004C02A4
valore assegnato:                      round(120.0 / nativeUpdateRate)
decremento nell'owner Player:          0x00251394 / 0x002513A0
gate interazione aggiuntivo:           0x00374BE8
accumulatore effetti associato:        Player+0x227E
```

Il valore iniziale e `60` update a 30 Hz e `120` update a 60 Hz: in entrambi
i casi dura due secondi. Il decremento a ogni simulation tick e quindi parte
del contratto OOT3D, non un contatore 30 Hz sfuggito alla conversione. Anche
la cadenza visiva usa l'accumulatore adiacente, il rate nativo, RNG e le
costanti originali (`5.0`, `16.9`). Non e stato aggiunto alcun hook o
moltiplicatore. Un eventuale lavoro futuro su questa zona riguardera la
classificazione degli effetti/RNG, non la durata del timer ne i suoi gate.

### Terzo sottografo temporale Player implementato

`Player+0x2248` e un campo `s16` condiviso da domini diversi. Le evidenze
OOT3D impongono di non trattarlo come un timer generico:

| Dominio | Stato/azione | Semantica osservata |
| --- | --- | --- |
| Deku Stick acceso | held-item action `6` | countdown scalato nativamente |
| pesca | held-item action `2`, valore negativo | recupero `-N -> 0` in frame logici |
| arco/fionda/hookshot | valori `-1`, `-2`, `-3` | transizioni discrete di readiness |
| pesca attiva | valori `0`, `1`, `2`, `3`, `-5` | stati discreti dell'action |

Il percorso torcia conferma il dominio continuo gia corretto. In
`ObjSyokudai_Update` (`0x002868D4`) i refresh sono
`round(630.0 / nativeUpdateRate)` e
`round(600.0 / nativeUpdateRate)`, rispettivamente 10,5 e 10 secondi a
entrambe le frequenze. Anche l'estinzione nell'owner Player usa
`round(3.0 / nativeUpdateRate)`. Questi writer e il decremento del burn timer
restano invariati.

Il solo sito non rate-aware e il recupero negativo della pesca:

```text
owner Player:                         0x00250AD0
test held-item action == 2:           0x0025136C / 0x00251370
load/cmp/add/store Player+0x2248:     0x00251374..0x00251380
continuazione:                        0x00251384
registri verificati dal prologo:
  r4 = Player
  r6 = Player + 0x2000
  r7 = Player + 0x2200
```

`PlayerWireState` espone ora il campo col nome neutro
`ItemActionStateOrBurnTimer`; la funzione
`PlayerAdvanceFishingItemStateTowardReady` possiede esclusivamente la
variante pesca del sum type e avanza soltanto su `CrossedLogicalFrame`.

Il blocco ARM `0x00251374` e stato promosso direttamente nel dispatcher C++
tipizzato. Non viene usata una riconciliazione postuma:

1. il dispatcher valida azione, registri base e indirizzo wire;
2. legge una sola volta il valore `s16`;
3. applica la semantica tipizzata;
4. conserva `r0`, `NZCV` e la continuazione ARM a 30 Hz;
5. dichiara il blocco come observable exit, cosi anche il whole-AOT non puo
   inglobarlo ignorando il port;
6. espone chiamate, avanzamenti logici e hold intermedi nella telemetria
   `typed_gameplay_*`.

La parita Native30 e verificata contro l'esecuzione del blocco A32 originale
per valori negativo, zero e positivo, confrontando memoria, registri, `CPSR`
e PC di uscita. La prova temporale verifica inoltre `-3 -> 0` in tre update a
30 Hz e in sei update a 60 Hz, con tre soli attraversamenti logici. I test
dedicati e l'intera suite `oot3d_native_game_runtime_tests` passano. Il source
N64 e stato usato soltanto per corroborare il ruolo storico del campo
condiviso; valori, azioni e timing applicati provengono dal binario OOT3D.

### Quarto sottografo temporale Player implementato

`Player+0x2224` e il timer di immersione completa (`u16`). La semantica e
determinata direttamente dall'owner OOT3D:

```text
owner Player:                         0x00250AD0
confronto profondita/soglia:          0x00252018..0x0025202C
reset dopo dispatch runtime:          0x00252038..0x00252040
load/cmp limite:                      0x0025204C..0x00252058
incremento/store:                     0x0025205C / 0x00252060
continuazione:                        0x00252064
limite OOT3D da literal 0x00252144:   450
registri verificati:
  r4 = Player
  r7 = Player + 0x2200
```

La condizione usa `Actor+0x88` (`DepthInWater`) e
`PlayerMovementContext+0x2C` (`SurfaceReference`). Se la profondita e sotto la
soglia, il timer viene azzerato nello stesso simulation tick; oltre la soglia
incrementa fino a `450`. I root `0x002B7FD0` e `0x004BE20C` citati
nell'inventario di layout non accedono al campo: provano soltanto la copertura
della struttura Player e non sono consumer temporali.

Un solo controllo architetturale sul sorgente N64 conferma lo stesso ruolo e lo
stesso algoritmo (`underwaterTimer`), ma non fornisce il valore: N64 satura a
`300` update a 20 Hz, mentre OOT3D satura nativamente a `450` update a 30 Hz.
Entrambi rappresentano 15 secondi. Il runtime usa quindi esclusivamente il
limite OOT3D `450`.

`PlayerWireState` espone ora `UnderwaterTimer` a `0x2224` e
`PlayerUpdateUnderwaterTimer` separa i due tipi di evento:

1. l'uscita dall'immersione azzera immediatamente il timer;
2. la permanenza immersa incrementa soltanto su `CrossedLogicalFrame`;
3. la saturazione resta esattamente `450`.

Il bridge non sostituisce il confronto di profondita ne la chiamata
`GlobalRuntimeFlags_UpdateAndDispatch` (`0x0032EB60`), che continua a essere
eseguita a ogni simulation tick con argomento `0` o `0x20`. Sono promossi solo
i blocchi puri successivi alla chiamata:

- `0x00252038` possiede reset e store;
- `0x0025205C` possiede incremento e store;
- entrambi proseguono a `0x00252064` e sono observable exit per il whole-AOT.

La parita Native30 confronta i blocchi tipizzati con la sequenza A32 originale,
inclusi memoria Player, tutti i registri core, `CPSR` e PC. La prova Enhanced60
verifica hold sui substep intermedi, incremento sui soli frame logici, reset
immediato e la stessa durata di 15 secondi. Probe e report espongono inoltre
`underwater_timer` e quattro contatori `typed_gameplay_underwater_timer_*`.

### Quinto sottografo temporale Player implementato

`Player+0x2228` e un timer firmato della finestra melee e
`Player+0x2229` e il relativo stato/contatore combo. L'owner OOT3D esegue
questa transizione:

```text
owner Player:                         0x00250AD0
load/cmp Player+0x2228:               0x002518B4 / 0x002518B8
clear Player+0x2229 se timer == 0:    0x002518BC..0x002518C4
step firmato verso zero:              0x002518C8 / 0x002518CC
store Player+0x2228:                  0x002518D0
continuazione:                        0x002518D4
registri verificati:
  r4 = Player
  r6 = Player + 0x2000
  r7 = Player + 0x2200
```

Il timer non viene semplicemente decrementato: un valore positivo scende
verso zero, uno negativo sale verso zero e lo stato combo viene azzerato
soltanto nell'update successivo a quello che porta il timer a zero. Questa
sequenza e importante per non anticipare di un frame la fine della catena.

Le evidenze OOT3D indipendenti delimitano il dominio:

- `Player_BuildMeleeWeaponTipPositions` (`0x00313C18`) legge e incrementa
  `Player+0x2229` quando il contatore e almeno `3`, modificando la lunghezza
  dei segmenti usati dai collisori melee;
- l'inizializzazione Player a `0x0036B1C0..0x0036B1C4` azzera entrambi i
  campi;
- il byte successivo `Player+0x222A` e invece un indice circolare su due
  storie input da sei campioni e deve continuare ad avanzare a ogni
  simulation tick.

Il confronto architetturale mirato col sorgente N64 trova lo stesso algoritmo
su `unk_844/unk_845`, inclusi setup a `8`, inversione di segno al rilascio e
uso nella catena d'attacco. La corrispondenza non sostituisce l'evidenza
OOT3D: indirizzi, layout, blocco eseguito e consumer provengono dal binario
3DS.

`PlayerWireState` espone ora `MeleeWeaponActionTimer` e
`MeleeWeaponComboState`. `PlayerUpdateMeleeActionTiming` mantiene immediate
le transizioni di stato ma applica lo step verso zero soltanto su
`CrossedLogicalFrame`. Il bridge promuove il blocco puro `0x002518B4`,
conserva a 30 Hz `r0`, `NZCV`, memoria e uscita `0x002518D4`, e lo dichiara
observable exit per il whole-AOT.

I test differenziali coprono timer negativo, zero e positivo contro i blocchi
A32 originali. La prova Enhanced60 verifica:

1. hold sui substep intermedi;
2. step su ogni frame logico;
3. identica durata di otto frame logici per il valore iniziale `8`;
4. conservazione dello stato combo nel frame terminale;
5. clear immediato nell'update successivo.

Probe e report espongono i due byte e i contatori
`typed_gameplay_melee_action_*`. Il writer/collider
`Player_BuildMeleeWeaponTipPositions` costituisce un secondo confine
temporale dello stesso campo ed e convertito separatamente qui sotto.

### Sesto sottografo temporale Player implementato

`Player_BuildMeleeWeaponTipPositions` (`0x00313C18`) costruisce tre punti
melee trasformati e aggiorna i collisori del Player. Il suo prologo copia
l'anchor base, poi legge `Player+0x2229`. Per valori almeno `3`, il binario
nativo esegue:

```text
load/cmp combo state:                 0x00313C38..0x00313C44
add +1:                               0x00313C48
truncate a u8:                        0x00313C4C
store Player+0x2229:                 0x00313C50
inizio matematica VFP nativa:         0x00313C54
continuazione/basic-block successivo: 0x00313C74
```

La parte temporale e quindi un confine di tre istruzioni, separato dalla
matematica e dalle trasformazioni. Il bridge intercetta
`0x00313C48` e rientra al vero basic-block successivo `0x00313C74`:

1. valida `r0 = Player + 0x2000`, il byte wire e `r1`;
2. su un frame logico replica `combo = (combo + 1) & 0xFF`;
3. su un substep intermedio conserva il byte;
4. esegue `VCVT`, `VMLA` e `VMUL` tramite le primitive VFP bit-exact
   condivise col runtime A32, inclusi rounding mode, NaN e flag `FPSCR`;
5. aggiorna gli stessi `r0`, `r1`, `s0..s3` e anchor del blocco originale;
6. lascia invariato `CPSR` e passa alle trasformazioni OOT3D originali.

In questo modo il calcolo
`base * (1 + (9 - combo) * 0.1)` e le tre trasformazioni native vengono
ancora eseguiti a ogni simulation tick. Il calcolo non usa l'aritmetica float
host: riusa le operazioni ARM/VFP gia convalidate, mentre trasformazioni e
collider restano nel codice OOT3D. Non esistono una correzione renderer o un
collider sostitutivo. Soltanto l'orologio del byte discreto resta nel dominio
dei frame logici.

La parita Native30 e verificata contro il blocco A32 per gli stati `3`, `9` e
`255`, incluso il wrap a `0`, confrontando memoria, registri, `CPSR` e
continuazione, oltre a tutti i registri VFP e `FPSCR`. Enhanced60 verifica un
hold intermedio seguito da un singolo avanzamento logico. Il confine e
inoltre un observable exit whole-AOT e i
contatori `typed_gameplay_melee_weapon_tip_combo_*` rendono visibile la sua
cadenza reale.

### Settimo sottografo temporale Player implementato

`Player+0x227C` e il countdown della finestra damage-run. L'evidenza OOT3D
delimita l'intero dominio del campo:

- `Player_ApplyDamageResponse` (`0x0035D304`) lo azzera sempre a
  `0x0035D370`;
- lo stesso owner lo inizializza al valore OOT3D `30` a `0x0035D6E4`
  soltanto nel ramo di risposta al danno in movimento;
- il common Player owner lo decrementa, se non zero, a
  `0x00250C30..0x00250C40`;
- subito dopo chiama
  `Player_UpdateContextActionAndSequenceState` (`0x003C45F4`).

Il confronto mirato col sorgente N64 identifica la stessa architettura:
`Player_ApplyDamageResponse` azzera il campo omologo `unk_890`, lo avvia nella
risposta al danno in corsa e il common update lo decrementa. Il valore N64 e
diverso e non viene importato: writer, durata e flusso restano quelli OOT3D.

`PlayerWireState` espone quindi `DamageRunTimer` a `0x227C` e
`PlayerUpdateDamageRunTimer` ne conserva la durata nel dominio dei frame
logici. Il bridge intercetta solo `0x00250C30`; dopo l'eventuale decremento
ricostruisce esattamente `CMP`, registri argomento e `BL` nativi:

```text
r0 = PlayState
r1 = Player
lr = 0x00250C4C
pc = 0x003C45F4
```

Non viene introdotto un rientro a meta basic-block e nessun consumer e
sostituito. Il valore `30` continua a essere prodotto esclusivamente dal
writer OOT3D. I test differenziali Native30 coprono `0`, `1`, `30` e `255`
con confronto di memoria, registri e `CPSR`; Enhanced60 verifica hold
intermedio, singolo decremento per frame logico e scadenza senza underflow.
Probe e report espongono `damage_run_timer` e i contatori
`typed_gameplay_damage_run_timer_*`.

### Ottavo sottografo temporale Player implementato

`Player+0x2488` e il timer signed nativo di invincibilita. Il common Player
owner a `0x00250BE4..0x00250C2C` ne definisce entrambe le semantiche:

- un valore negativo avanza verso zero e mantiene attivo il collider AT;
- un valore positivo avanza verso zero e impedisce la registrazione del
  collider AC;
- zero non viene modificato;
- quattro action Player sospendono soltanto il countdown positivo.

Questa interpretazione non deriva dai nomi N64. I consumer OOT3D nello stesso
owner selezionano direttamente AT e AC in base al segno, mentre
`Player_ApplyDamageResponse` (`0x0035D304`) legge e scrive lo stesso byte
signed. Il confronto N64 e stato usato solo dopo questa delimitazione e
conferma l'architettura `invulnerability < 0` / `intangibility > 0`.

Le quattro eccezioni non sono replicate come costanti gameplay. Il bridge
legge in ordine i puntatori action dai literal OOT3D:

```text
0x00251314 -> 0x004886F4
0x00251318 -> 0x004BC22C
0x0025131C -> 0x004C3064
0x00251320 -> 0x00495C30
```

Come il codice ARM originale, interrompe le letture al primo confronto uguale.
`PlayerWireState` espone `ActionFunction` a `0x1708` e
`InvincibilityTimer` a `0x2488`; `PlayerUpdateInvincibilityTimer` possiede
soltanto la regola temporale signed e riceve dal bridge il risultato
data-driven del confronto action.

Il confine tipizzato `0x00250BE4` riproduce `LDRSB`, tutti i `CMP`, gli
effetti osservabili su `r0`, `r1`, `r2` e `CPSR`, quindi continua al successivo
owner nativo `0x00250C30`. In `Enhanced60` questi confronti e consumer restano
eseguiti a ogni simulation tick; solo la scrittura che avvicina il timer a
zero attende `CrossedLogicalFrame`.

I test differenziali Native30 coprono valore negativo, zero, countdown
positivo e ciascuna delle quattro action di hold, confrontando memoria,
registri e `CPSR` con l'intero sottografo A32 originale. Enhanced60 verifica
separatamente i due segni, gli hold intermedi e l'eccezione positiva. Probe e
report espongono `invincibility_timer` e i contatori
`typed_gameplay_invincibility_timer_*`.

### Nono sottografo temporale Player implementato

`Player+0x227B` e il contatore wrapping usato da `Player_Draw`
(`0x004BF618`) per l'oscillazione fog durante l'intangibilita positiva.
L'evidenza e interamente OOT3D:

- il ramo viene raggiunto soltanto dal percorso draw nativo che ha gia
  verificato il timer signed a `Player+0x2488`;
- il passo preparato in `r11` viene limitato all'intervallo signed `8..40`;
- il passo viene sommato al byte con il wrap nativo;
- il blocco successivo a `0x004BF704` converte il contatore in un angolo e
  alimenta i parametri fog usati dal draw Player.

Il bridge intercetta il confine basic-block reale
`0x004BF6D8..0x004BF6F4`, senza promuovere l'intera funzione draw e senza
creare un ingresso a meta blocco. Riproduce i due `CMP` signed, il clamp,
`r0`, `r11`, `CPSR` e il wrap del byte, quindi continua a
`0x004BF6F8`. Passo, condizione di ingresso, trigonometria, fog e render
rimangono quelli OOT3D.

In `Enhanced60` il draw e tutti i consumer successivi continuano a essere
eseguiti a ogni simulation tick, mentre la fase persistente avanza una volta
per frame logico. Questo chiude il raddoppio della frequenza fog senza
introdurre uno stato renderer sostitutivo. Il campionamento frazionario della
fase tra due frame logici resta un possibile raffinamento puramente visivo:
non e necessario per la parita temporale e non viene simulato con stato
nascosto in questo incremento.

I differenziali Native30 coprono clamp basso, entrambi i limiti, passo
interno, clamp alto e wrap `250 + 40 -> 34`, confrontando memoria, registri e
`CPSR` con l'A32 originale. Enhanced60 verifica hold intermedio e avanzamento
singolo. `PlayerWireState` e il probe espongono ora sia
`damage_flicker_animation_counter` a `0x227B` sia il contatore adiacente
`textbox_button_cooldown_timer` a `0x227A`; la conversione del suo owner
completo e descritta nel passaggio seguente.

### Decimo sottografo temporale Player implementato

Il basic block naturale `0x00250B50..0x00250BB0` possiede quattro countdown
Player. Non viene introdotta un'entry artificiale al decremento del solo
textbox timer: il bridge converte l'intero owner e conserva entrambe le uscite
native `0x00250BB4` e `0x00250BE4`.

I campi sono stati delimitati prima di assegnarne la cadenza:

- `Player+0x247E` e il cooldown associato alle azioni item. Il sorgente N64
  omologo lo avvia dopo il distacco dell'attore tenuto e il lancio del
  boomerang; questa evidenza viene usata soltanto per il ruolo, non per layout,
  durata o writer OOT3D;
- `Player+0x227A` e il cooldown textbox che maschera A/B/C-up nel vero
  `Player_Update`;
- `Player+0x249F` e il grace timer avviato da
  `Player_StartFairyHealingCutscene` (`0x0034AE64`). Il writer OOT3D produce
  `round(60 / updateRate)` a `0x0034B018`; i consumer saltano damage/hazard
  update mentre e attivo e ritardano il collider AC finche il valore non
  scende sotto `15`;
- `Player+0x2482` limita la ripetizione del collision SFX. Il consumer legge
  l'offset dal literal OOT3D `0x001D0358`, chiama `Actor_PlaySfx` soltanto a
  zero e riscrive il valore nativo `4` a `0x001D0314`.

Queste origini impongono una cadenza mista. I valori fissi `+0x247E`,
`+0x227A` e `+0x2482` scendono una volta per frame logico, conservando durata
e frequenza degli eventi a 30 Hz. `+0x249F` scende invece a ogni simulation
tick: il suo writer e gia rate-aware e a 60 Hz crea `60` tick invece dei `30`
creati a 30 Hz. Trattarlo come timer logico ne raddoppierebbe la durata.

`PlayerUpdateCommonCountdowns` esprime questa distinzione senza modificare i
writer. Il bridge ricostruisce inoltre:

- `r5 = Player + 0x1000`;
- `r1 = PlayState + 0x208C`;
- lo store di `r1` in `[sp + 0x3C]`;
- `r0 = Player.stateFlags1`;
- i due `TST`, `CPSR` e la scelta del ramo successivo.

I differenziali Native30 coprono zero, decrementi, byte wrap signed del campo
`+0x2482` e tutte le combinazioni dei due state flag, confrontando memoria,
registri, stack e `CPSR` con il blocco A32. Enhanced60 verifica separatamente
gli hold dei tre timer logici e l'avanzamento a ogni tick del timer Fairy.
Probe e report espongono tutti e quattro i campi e la telemetria del nuovo
owner.

### Undicesimo sottografo temporale Player implementato

`Player+0x249E` e lo stato signed che coordina il danno al respawn. La
classificazione deriva dal grafo OOT3D completo, non dal vecchio nome
`unk_249e`:

- `Player_Init` scrive il valore fisso `0xFE` (`-2`) a `0x00192130` quando
  il respawn state nativo e `1` oppure `-1`;
- il basic block naturale `0x00250B08..0x00250B18` avvicina il valore
  negativo a zero;
- al crossing devia a `0x00250B1C`, imposta lo stato a `1` e chiama
  `Audio_PlaySoundGeneral` con l'identificatore OOT3D `0x010004E6` e i
  literal nativi;
- `Player_UpdateDamageAndHazards` consuma lo stato nonzero e lo azzera a
  `0x00132B1C` dopo il percorso di danno ammesso dal blocking-cutscene gate.

Una scansione ARM word-by-word dell'intero `code.bin` trova soltanto questi
quattro siti che usano l'offset `0x49E` rispetto alla base `Player+0x2000`:
`0x00132B1C`, `0x00192130`, `0x00250B14` e `0x00250B28`. Non esiste un
writer che moltiplichi la durata per `nativeUpdateRate`; il valore `-2` e
quindi nel dominio dei frame logici originali.

Il sorgente N64 contiene la stessa macchina a stati in `Player.unk_A86` e
permette di confermare il ruolo respawn/damage. Viene usato soltanto per il
nome semantico: offset, signedness, writer, durata, sound id e control flow
implementati restano quelli verificati nel binario OOT3D.

`PlayerAdvanceRespawnDamageState` possiede esclusivamente la transizione
signed negativa. Il bridge intercetta `0x00250B08`, che e gia un entry di
basic block nel registry A32:

- su un substep intermedio conserva valore, `r0` e risultato del confronto
  signed, quindi converge a `0x00250B44`;
- su un frame logico avanza di uno;
- soltanto per `-1 -> 0` devia al vero blocco OOT3D `0x00250B1C`.

Il bridge non riproduce il suono e non anticipa il consumer hazard. Il blocco
originale continua a scrivere `1`, caricare i propri literal e chiamare il
backend audio esistente, percio l'evento resta nel ledger e nel call graph
OOT3D una sola volta.

I differenziali Native30 coprono `-128`, `-2` e `-1`, confrontando memoria,
registri, `CPSR` e successore con A32. Enhanced60 verifica la sequenza
`-2, -2, -1, -1, 0` e un solo dispatch audio. Probe e report espongono
`respawn_damage_state` e quattro contatori dedicati. Il catalogo gameplay
tipizzato contiene ora `59` entry; whole-AOT riporta `138839` safe entry e
`655` boundary esclusi.

### Dodicesimo sottografo temporale Player implementato

`Player_UpdateRandomTurnTimer` (`0x0025342C`) viene chiamata una volta da
`Player_Update` con il puntatore a `Player+0x1220` e gli argomenti OOT3D
`0x14`, `0x50` e `6`. Il grafo e completo nel binario:

- `Player+0x1220` e lo stato signed prodotto (`0`, `1` o `2`) e sommato dal
  consumer a `Actor.shape.face`;
- `Player+0x1222` e il timer signed;
- a ogni chiamata con timer nonzero il codice esegue un decremento fisso di
  uno e una truncation `SXTH`;
- allo zero chiama `Rand_S16Offset_003702C8`, salva il risultato nel timer e
  ricalcola lo stato;
- ne il writer ne la routine contengono una moltiplicazione per
  `nativeUpdateRate`.

Eseguire questo owner due volte per frame logico dimezzerebbe l'intervallo
delle variazioni del volto e, soprattutto, consumerebbe due volte la sequenza
RNG globale. Non e stato necessario inferire il comportamento dal sorgente
N64: funzione, unico caller, consumer e callee sono tutti delimitati nelle
evidenze OOT3D.

Il bridge intercetta due entry gia naturali nel registry A32:

- `0x0025344C` possiede decremento, `SXTH`, store e confronto. Nei substep
  intermedi mantiene il timer e converge a `0x0025346C`; nei frame logici
  riproduce anche wrap e `CPSR` originali e, soltanto allo zero, entra nel
  blocco di refresh;
- `0x00253460` possiede il `BL` RNG. Nei substep intermedi salta
  all'epilogo `0x002534A0`, conservando anche lo stato prodotto in precedenza.
  Nei frame logici imposta il vero return address `0x00253468` e devia alla
  routine OOT3D `0x003702C8`.

Il runtime non implementa un RNG alternativo e non sintetizza il risultato:
la routine originale continua a possedere stato globale, distribuzione,
truncation e store. Il dominio tipizzato decide soltanto se il frame corrente
puo consumare quell'evento.

I differenziali Native30 coprono timer negativi, positivi, crossing e wrap
`INT16_MIN -> INT16_MAX`, confrontando memoria, registri, `CPSR`, `LR` e
successori. Enhanced60 verifica hold, avanzamento logico e un solo dispatch
RNG. Probe e report espongono `random_turn_state`, `random_turn_timer` e sei
contatori dedicati. Il catalogo gameplay tipizzato contiene ora `61` entry;
whole-AOT riporta `138837` safe entry e `657` boundary esclusi.

### Tredicesimo sottografo temporale Player implementato

`Player+0x174F` e un contatore unsigned della persistenza attention/retarget.
La classificazione deriva dall'intero insieme degli accessi OOT3D. Una
scansione ARM word-by-word di `code.bin` trova esattamente sei istruzioni con
l'offset `0x74F` rispetto all'anchor `Player+0x1000`:

```text
reset a 0xFF:                    0x00250C74
reset a 0:                       0x00250E44
consumer unsigned < 4:           0x00250FF4
load/increment/store/saturazione: 0x0025101C, 0x0025102C, 0x00251034
```

Il consumer mantiene il focus precedente durante la breve finestra in cui
l'attore target viene ricercato di nuovo nelle liste. Il blocco
`0x0025101C..0x00251038` incrementa il byte una volta per `Player_Update`,
con due proprieta osservate direttamente:

- `0xFF + 1` effettua wrap a `0`;
- `0xFE + 1` produce temporaneamente `0xFF`, imposta i flag dal confronto
  unsigned con `0xFE` e viene poi clampato a `0xFE`.

Nessun writer moltiplica la durata per `nativeUpdateRate`. Incrementare il
campo a ogni substep Enhanced60 renderebbe quindi due volte piu breve il gate
`< 4`, modificando acquisizione/rilascio del target e indirettamente la
camera.

`PlayerAdvanceAttentionPersistenceCounter` possiede soltanto questo
avanzamento discreto. Il bridge intercetta la boundary naturale
`0x0025101C`, valida gli anchor Player originali e:

- conserva il byte nei substep intermedi;
- avanza sui frame logici;
- riproduce wrap, saturazione, `r0` e i flag `NZCV` calcolati sul valore
  pre-clamp;
- converge sempre al successore OOT3D `0x00251044`.

Reset, ricerca attore, confronto `< 4` e aggiornamento dei puntatori focus
restano nel grafo originale. I differenziali Native30 coprono `0`, `3`,
`0xFD`, `0xFE` e `0xFF`; Enhanced60 verifica che ogni valore utile al gate
resti stabile per due substep. Probe e report espongono
`attention_persistence_counter` e quattro contatori dedicati. Il catalogo
gameplay tipizzato contiene ora `62` entry; whole-AOT riporta `138836` safe
entry e `658` boundary esclusi.

### Primo confine del frame owner cutscene implementato

`Cutscene_UpdateFrameAndCommands` (`0x00321F50`) non possiede un unico
avanzamento temporale. Il controllo sul `code.bin` OOT3D distingue due
percorsi:

- `0x00322028` e il loop di catch-up gia temporizzato dal backend. Converte
  `PlayState+0x7C60` in secondi, lo moltiplica per il literal nativo `30.0` e
  chiama `Cutscene_ProcessCommands` soltanto per i frame interi non ancora
  raggiunti;
- `0x00322054` e il percorso normale: incrementa sempre
  `CutsceneContext+0x20`, carica il payload attivo da
  `PlayState+0x229C`, ripristina il frame ARM e tail-calla
  `Cutscene_ProcessCommands` (`0x002C5BA0`).

Il primo percorso e gia indipendente dal numero di invocazioni e rimane
interamente OOT3D. Modificarlo con un gate aggiuntivo perderebbe frame durante
il catch-up. Il secondo, invece, raddoppierebbe velocita e comandi discreti se
invocato a ogni substep Enhanced60.

Il nuovo modulo `oot3d_gameplay_cutscene` espone i wire layout minimi
verificati e separa due contratti:

- `CutsceneFrameCursor` esprime `previous/current` frazionari per i consumer
  continui futuri, mantenendo il frame `uint16_t` osservabile da ABI e
  salvataggi;
- `CutsceneNormalFrameAdvance::DispatchDiscreteCommands` autorizza il
  dispatcher originale soltanto al crossing di un frame logico.

Il bridge intercetta esclusivamente la boundary naturale `0x00322054`.
Nei substep intermedi conserva il frame e devia all'epilogo originale
`0x00321FB8`; nei crossing riproduce incremento con wrap `uint16_t`, carico
del payload, ripristino di `r4-r8/lr` e stack, quindi entra nel vero
`Cutscene_ProcessCommands`. Record QDB, 127 command ID, ordine dei side effect
e percorso wall-clock non vengono reimplementati.

Il differenziale Native30 esegue il basic block A32 reale e confronta frame,
registri, stack e `CPSR`, inclusi `0xFFFF -> 0` e payload nullo/non nullo.
Enhanced60 verifica l'hold del frame, il ritorno tramite epilogo e l'assenza
di dispatch intermedi. La valutazione frazionaria di camera, cue attori e
ambiente non e ancora dichiarata completa: il cursore source-facing e ora
disponibile, ma tali consumer devono essere promossi individualmente senza
duplicare gli eventi del dispatcher. Il catalogo gameplay tipizzato contiene
ora `63` entry; whole-AOT riporta `138835` safe entry e `659` boundary
esclusi.

### Primo consumer continuo delle cue attori implementato

L'audit del dispatcher OOT3D `Cutscene_ProcessCommands` (`0x002C5BA0`) mostra
che lo stesso switch contiene categorie temporalmente incompatibili:

- trigger su frame esatto per audio, messaggi, tempo e transizioni;
- scritture dei puntatori alle cue attive in `CutsceneContext`;
- setup delle tracce camera;
- azioni ambientali con incrementi discreti per frame;
- interpolazioni di fade e selezione di stato.

Richiamare l'intero dispatcher anche nel subframe Enhanced60 duplicherebbe
quindi side effect e accelererebbe gli incrementi. Il dispatcher resta
autorevole solo a 30 Hz; il tempo frazionario viene fornito ai consumer
continui gia richiamati a ogni simulation tick.

Il primo confine promosso e
`EnvironmentPath_InterpolateActorPosAndRotation` (`0x00361F00..0x0036205F`).
Il corpo nativo:

- risolve una delle cue attore da `PlayState+0x22DC+index*4`;
- legge start/end frame e le due posizioni `s32` dal record da `0x24` byte;
- usa `PlayState+0x22B8` con `Environment_LerpWeight` (`0x00361490`);
- interpola `Actor.world.pos`;
- opzionalmente ricava pitch e yaw dal vettore della cue.

L'helper e condiviso da quattro caller OOT3D verificati (`0x001403A0`,
`0x00141F24`, `0x001423E0`, `0x0015D388`), quindi non e una correzione
specifica della title intro. Il bridge intercetta la sua entry solo quando il
frame ha una parte frazionaria. Materializza il prologo ARM originale
(`r4/r5/lr`, `d8-d10` e locals), converte le coordinate native, fornisce il
peso della cue al subframe e rientra nel tail originale a `0x00361F7C`.
Scritture di posizione, orientamento, epilogo e ABI restano cosi nel codice
OOT3D. Native30 e ogni crossing logico eseguono invece il corpo A32 completo.

Il gate legge anche i campi owner nativi `PlayState+0x101`, `+0x6028`,
`+0x6029` e `+0x7C60`: quando e attivo il clock backend/catch-up, o il suo
owner non puo avanzare, non viene aggiunta la fase del frame normale. Il test
Enhanced60 esegue il tail A32 reale e verifica posizione a mezzo frame,
ripristino stack/VFP, fallback Native30 non distruttivo ed esclusione del
percorso backend-clock. Il catalogo contiene ora `64` entry; whole-AOT riporta
`138834` safe entry e `660` boundary esclusi.

### Primo consumer continuo della camera cutscene implementato

L'analisi del `code.bin` delimita il percorso camera senza richiamare una
seconda volta il dispatcher cutscene. Il comando camera principale seleziona
il segmento CAAD/MADS/CMAD, costruisce una coppia temporanea di puntatori e
chiama:

```text
CameraAnimation_ApplyFrame: 0x0033CB90
CameraAnimation_Attach:     0x0033CB1C
Camera_Demo1:               0x00200118
```

`CameraAnimation_ApplyFrame` inizializza lo stato da CAAD e applica i record
CMAD al frame intero. `CameraAnimation_Attach` collega tale stato a
`Camera+0x16C` e seleziona il ramo diretto tramite il flag `4`.
`CAM_SET_CS_0` (`0x25`) usa `Camera_Demo1`: il ramo `101` copia lo stato
collegato nei campi camera a ogni vero `Camera_Update`. Il solve camera era
quindi gia eseguito a ogni substep Enhanced60; era il suo input CMAD a restare
fermo sul frame logico precedente.

Il nuovo modulo isolato `oot3d_gameplay_camera_animation` descrive i wire
layout letti dal formato nativo e implementa il valutatore usato da
`NativeCurve_SampleFloat` (`0x003087A4`):

- curva `1`: keyframe lineari da `0x08` byte;
- curva `2`: keyframe Hermite da `0x10` byte con tangent in/out e loop nativo;
- curva `3`: step/hold da `0x08` byte;
- record CMAD `1/2/3/7/8` verso i nove output nativi a `+0x80..+0x94`,
  `+0xD0`, `+0x144` e `+0x1A2`;
- scale native esatte `0x42200000`, `0x4622F983` e `0x42652EE1`.

Il bridge non usa le tabelle statiche ricostruite dall'export: legge a ogni
sample header, offset relativi e keyframe direttamente dagli indirizzi guest
installati dagli asset OOT3D. La conversione finale float-to-`s16` usa lo
stesso helper VFP/FPSCR del runtime.

Il binding viene osservato all'entry di `CameraAnimation_ApplyFrame`, ma il
corpo A32 originale viene sempre conservato. Poiche la coppia di risorse vive
sullo stack del dispatcher, il bridge salva soltanto i due puntatori CAAD e
MADS/CMAD, mai l'indirizzo della coppia temporanea. L'osservazione viene
accettata esclusivamente quando:

- l'output e `PlayState+0x232C`, cioe lo stato camera del
  `CutsceneContext` principale;
- `PlayState+0x229C` contiene ancora la cutscene attiva;
- il frame passato coincide con il frame camera nativo a `0x0051B310`;
- il percorso backend-clock/catch-up non e attivo;
- risorse e output sono mappati con le protezioni attese.

Nel solo substep frazionario, prima del vero `Camera_Demo1`, il bridge
ricampiona CAAD/MADS/CMAD a `N+0.5`, scrive lo stato collegato e lascia poi che
il corpo OOT3D esegua copia, clamp e solve camera. Crossing logici, Native30,
setup, cambio segmento, attach, eventi e comandi restano interamente A32.
Le output actor-owned (`0x0059B964` e `0x0059BB20`) vengono instradate
separatamente: possiedono un clock distinto e non possono essere scambiate
per `PlayState+0x232C`.

Il binding e volatile, viene azzerato all'avvio e dopo ogni restore di un
savestate, ed e rivalidato prima di ogni scrittura contro cutscene, frame e
stato allegato; un cambio scena non puo quindi applicare una traccia non piu
attiva. La telemetria
separa binding accettati/rifiutati, sample frazionari, curve lette e failure.
I test coprono linear/Hermite/hold, default CAAD, tre curve CMAD live,
conversione `s16`, assenza di scritture ai crossing e routing separato delle
output actor-owned. A questo stadio il catalogo contiene `66` entry;
whole-AOT riporta `138832` safe entry e `662` boundary esclusi.

### Primo clock camera actor-owned promosso

Il simbolo diagnostico `ActorCutsceneCamera_Update` assegnato in precedenza a
`0x00347AEC` era fuorviante. La verifica sui load/store del `code.bin` mostra
che la funzione e un inizializzatore:

- imposta il cursore dell'owner a `Actor+0xF18 = 1`;
- seleziona l'indice segmento a `Actor+0xF1C`;
- installa CAAD e CMAD a `Actor+0xF24/+0xF28`;
- campiona il frame `1` nei due stati globali `0x0059B964` e `0x0059BB20`;
- porta il cursore a `2` e collega lo stato alla camera.

L'unico consumer ricorrente di questi campi e il blocco
`0x001E0474..0x001E04DC` dentro `EnZl4_Update` (`0x001E042C`). Quando
l'animazione e attiva, il corpo originale confronta il cursore con
`CAAD+0x0C`, chiama `CameraAnimation_ApplyFrame`, incrementa il cursore e
riattacca lo stato. Eseguire questo blocco a ogni tick Enhanced60
raddoppierebbe sia il clock sia il detach di fine segmento.

Il modulo camera descrive ora anche i layout nativi verificati:

- owner: cursore `s32` a `+0xF18`, indice `s32` a `+0xF1C`, coppia risorse a
  `+0xF24`;
- CAAD: frame finale `s32` a `+0x0C`;
- PlayState: quattro puntatori camera da `+0xA54` e indice attivo `s16` a
  `+0xA64`.

Il bridge osserva la vera chiamata intera verso `0x0059BB20`, deriva
l'indirizzo owner dalla coppia risorse e conserva solo identita, indice,
frame e puntatori nativi. Nel substep `N+0.5` intercetta il boundary interno
di `EnZl4_Update`, rivalida:

- owner e risorse ancora attivi;
- cursore esattamente a `N+1` e non oltre la fine CAAD;
- contesto `PlayState` conservato nei registri nativi;
- camera indicizzata realmente attiva;
- stato `0x0059BB20` ancora collegato con flag `4`.

Se il contratto e valido, valuta le stesse curve native a `N+0.5`, scrive lo
stato e salta a `0x001E04E0`, dopo il solo blocco di advance/attach. Il resto
di `EnZl4_Update` continua a 60 Hz. Ai crossing il bridge non interviene:
sample intero, incremento, confronto di fine segmento e detach restano nel
codice OOT3D a 30 Hz. Anche quando il cursore ha appena raggiunto il frame
finale viene prodotto l'ultimo mezzo frame; il detach viene rinviato al
crossing successivo, come richiesto dal clock originale.

Il binding e volatile e viene invalidato dai restore. Telemetria distinta
misura priming, binding, rifiuti, hold intermedi, curve e failure. I test
verificano stato a mezzo frame, cursore e lifecycle immutati, fallback
Native30, frame finale, reset e boundary whole-AOT. Il catalogo contiene ora
`67` entry; whole-AOT riporta `138831` safe entry e `663` boundary esclusi.

### Primi timer diretti di Camera_Update promossi

`Camera_Update` (`0x002D84C4`) non e una singola trasformazione continua:
mescola query del pavimento, callback indirette del modo camera, interfaccia,
quake, distorsione dell'acqua e submission finale con contatori discreti. Un
port monolitico prima di aver tipizzato i callback avrebbe quindi un rischio
troppo alto. La prima separazione conserva il corpo originale e intercetta
soltanto due mutazioni dirette dimostrate dal `code.bin`:

```text
0x002D86F0  ldr r0, [r7, #0x9C]
0x002D86F4  add r0, r0, #1
0x002D86F8  str r0, [r7, #0x9C]

0x002D89D0  sub r0, r1, #1
0x002D89D4  str r0, [r7, #0x28]
```

In entrambi i casi `r7` e la struttura globale camera nativa
`0x00516E9C`; il prologo conserva la camera in `r4` e `Camera+0x100` in `r5`.
Il primo campo conta i frame consecutivi senza un pavimento valido ed e
confrontato con `200`; il percorso con pavimento valido lo azzera. Il secondo
e un delay nonzero prima di aggiornare l'interfaccia camera con il comando
`0x3200`.

Il modulo `oot3d_gameplay_camera` conserva i campi `u32` e il wrap ARM
originale. Il bridge convalida `r4/r5/r7` e, per il delay, anche il valore
caricato in `r1` e il comando in `r8`. Al crossing logico esegue lo stesso
`+1/-1`; al substep intermedio lascia il campo invariato e riprende
rispettivamente da `0x002D86FC` e `0x002D89D8`. Cosi il resto di
`Camera_Update` continua realmente ogni tick Enhanced60.

Questa promozione non dichiara completo `Camera_Update`. Rimangono da
classificare e portare separatamente:

- timer e `animation_state` nei callback indiretti dei modi camera;
- stato temporale dei quake.

Test dedicati coprono crossing, mezzo frame, wrap `u32`, continuation PC,
telemetria e fallback non distruttivo quando il contesto registri non
corrisponde. I due ingressi sono observable exit whole-AOT. Il catalogo
contiene ora `69` entry; whole-AOT riporta `138830` safe entry e `665`
boundary esclusi.

### Countdown nativo di Camera_CheckWater promosso

`Camera_CheckWater` (`0x002D06A0`) continua a essere eseguita ogni tick:
interroga i water box, cambia setting e luce, mantiene il quake e gestisce le
transizioni entrata/uscita. Il solo countdown temporale diretto e il campo
`s16 Camera+0x198`, inizializzato a `80` quando la camera entra in acqua:

```text
0x002D0A80  ldrsh r0, [r4, #0x98]  ; r4 = Camera+0x100
0x002D0A84  cmp   r0, #0
0x002D0A88  ble   0x002D0AB0
0x002D0A8C  sub   r0, r0, #1
0x002D0A90  strh  r0, [r4, #0x98]
```

Il nuovo boundary convalida `r5=Camera`, `r4=Camera+0x100` e
`r6=Camera+0x168`. Per un timer positivo esegue il decremento soltanto al
crossing logico, poi riprende a `0x002D0A94` per conservare l'aggiornamento
dei flag di distorsione. Valori zero o negativi restano nel corpo A32 perche
selezionano il ramo dipendente dalla scena. In questo modo la durata nativa
resta `80/30` secondi senza ridurre a 30 Hz collisione acqua, transizioni o
quake.

Il campo intero viene letto successivamente da `Camera_Update` nei percorsi
`0x002D8E70`, `0x002D8FB4` e `0x002D9120` per derivare l'ampiezza visuale.
Il consumer frazionario descritto sotto non altera il countdown ABI con
valori artificiali. Test dedicati coprono `80 -> 79`, hold intermedio,
terminale affidato ad A32,
continuation e telemetria. Il catalogo contiene ora `70` entry; whole-AOT
riporta `138829` safe entry e `666` boundary esclusi.

### Ampiezza acqua valutata sul subframe

I tre consumer di `Camera+0x198` sono lineari rispetto al countdown:

- flag `4`: moltiplica il timer per il literal a `0x002D8FA4`;
- flag `8`: moltiplica il timer per il literal a `0x002D9388`;
- configurazione custom: divide il timer per il divisore del record attivo.

Sul solo substep intermedio il modulo camera valuta
`timer - fractional_logical_frame`. Il bridge ricostruisce esclusivamente il
breve tratto VFP che converte quel valore in ampiezza, leggendo i literal dal
`code.bin`, e riprende il corpo originale a `0x002D9140`. Tutti i parametri
restanti, le tabelle custom, seno/coseno, orientamento, scala e submission
rimangono nel percorso OOT3D. Al crossing logico i blocchi originali leggono
il timer intero appena decrementato.

Questa separazione produce quindi `79.5` fra gli stati nativi `80` e `79`
senza scrivere un float nel campo `s16`, cambiare salvataggi o alterare la
durata. I test verificano separatamente i tre registri VFP di uscita,
continuation, assenza di intervento ai crossing e telemetria per ramo. Il
catalogo contiene ora `73` entry; whole-AOT riporta `138827` safe entry e
`669` boundary esclusi.

### Lifecycle dei quake camera promosso

`Quake_Calc` (`0x004787E8`) non possiede direttamente il clock delle quattro
request. A ogni update azzera un output temporaneo, seleziona le request attive
da `0x005A543C`, invoca un callback attraverso la tabella a `0x0053CAE8` e
compone il risultato massimo. Countdown, forma del segnale e RNG appartengono
ai sei callback:

| indice | callback | segnale OOT3D |
| ---: | ---: | --- |
| 1 | `0x0015F2CC` | seno e un campione casuale |
| 2 | `0x00144EE8` | due campioni casuali |
| 3 | `0x001344F0` | seno con envelope `countdown / initial` |
| 4 | `0x0015F25C` | due campioni casuali con lo stesso envelope |
| 5 | `0x00111CB0` | seno |
| 6 | `0x00150CFC` | seno/casuale perpetuo con fase modulo 16 |

I tipi `1..5` valutano soltanto un countdown positivo, producono il segnale e
poi eseguono `request+0x1C -= 1`; il valore restituito decide se
`Quake_Calc` rimuove la request. Il tipo `6` decrementa con wrap `s16`, usa il
valore risultante per la fase e restituisce sempre `1`. La request e larga
`0x24`; conserva countdown iniziale a `+0x02`, callback a `+0x08`, ampiezze a
`+0x0A..+0x10`, speed a `+0x18`, flag relativo a `+0x1A`, countdown a
`+0x1C` e camera slot a `+0x1E`.

Il modulo `oot3d_gameplay_quake` ricostruisce queste sei forme e l'LCG usato
da `Rand_ZeroOne`:

```text
seed = seed * 0x0019660D + 0x3C6EF35F
sampleBits = 0x3F800000 | (seed >> 9)
sample = bitcast<float>(sampleBits) - 1.0
```

Ai crossing logici il bridge:

1. legge la request nativa e il seno dalla stessa LUT OOT3D;
2. avanza il seed globale a `0x0050C0C4` esattamente zero, una o due volte
   secondo il callback;
3. calcola la coppia di fattori nello stesso ordine floating del callback;
4. decrementa il countdown una sola volta;
5. delega la composizione spaziale al helper originale `0x00369D44`.

Nel substep intermedio non viene inventato un nuovo segnale e non viene
consumato RNG. Una cache transiente, indicizzata dai quattro slot e validata
con request id, callback, camera, countdown iniziale e countdown corrente,
riusa la coppia di fattori del crossing precedente. Il helper originale viene
comunque rieseguito: una request relativa alla camera viene quindi ricomposta
contro eye/at/orientamento aggiornati a 60 Hz, mentre la forma frame-driven
conserva la cadenza OOT3D. Il ritorno comune `0x004788DC` ricostruisce il
`CMP r0, 0` e riprende rispettivamente dai branch nativi `0x00478904` o
`0x004788E4`.

Dopo un reset o restore la cache viene invalidata. Se il primo tick e un
mezzo frame, quel solo sample resta nullo e non anticipa il RNG; il crossing
successivo ricostruisce lo stato. La serializzazione della cache appartiene
alla futura ABI savestate Enhanced60. Restano inoltre da correggere nei
rispettivi owner gli eventuali producer `Quake_Add` eseguiti due volte: il
lifecycle consumer ora non accelera, ma non puo rendere one-shot un producer
attore ancora non classificato.

I test coprono i sei piani, wrap perpetuo, envelope, LCG, crossing,
intermedio, riuso del segnale, cache miss senza consumo RNG, zero terminale,
registri di ritorno e fallback non distruttivo. I sette nuovi ingressi sono
observable exit whole-AOT. Il catalogo contiene ora `80` entry; whole-AOT
riporta `138820` safe entry e `676` boundary esclusi.

### Countdown mode-specifici del setting gameplay promossi

Il dispatch indiretto di `Camera_Update` e ora chiuso sui dati nativi. La
tabella setting a `0x00516FF0` contiene un record zero seguito da 77 setting;
la tabella callback a `0x00517260` contiene 82 slot. Il setting gameplay
osservato `setting=1` risolve il record mode a `0x005192D4`. I suoi 21 mode
usano undici callback unici; dieci contengono dodici countdown verificati
direttamente nelle rispettive istruzioni. `Normal1` ne possiede tre: oltre al
countdown iniziale, la transizione speed a `Camera+0x4E` e quella rate a
`Camera+0x3E`.

Il bridge usa una tabella ABI unica per `Jump1`, `Jump2`, `Battle1`,
`Battle4`, `KeepOn1`, `KeepOn3`, `Normal1`, `Unique1`, `Subj3` e `Parallel1`.
Per ogni boundary convalida program counter, registro camera, registro/base
mode, offset del campo e rappresentazione `LDRH` o `LDRSH` in `r0`. Al
crossing applica il `SUB/STRH` nativo con wrap a 16 bit; nel substep
intermedio conserva campo e registro. La continuation specifica mantiene il
resto di ogni callback, inclusi solve e collisione, a 60 Hz.

`Camera_Special5` resta separata dalla tabella omologa ma la sua macchina
temporale e ora promossa. Il boundary `0x0025B6C8`, dopo `LDRSH/MOV/CMP`,
tratta positivo, zero e sentinel negativo come stati distinti. Al mezzo frame
rinvia sia il decremento sia la transizione zero; al crossing decrementa il
positivo o inoltra lo zero al corpo evento OOT3D `0x0025B6DC`. Validazione
target, collisione, RNG e posizionamento restano quindi nativi e one-shot,
mentre il solve comune a `0x0025B80C` puo continuare a 60 Hz.

Restano separati gli altri accumulatori non omologhi dentro gli undici
callback. L'inventario discreto ha verificato in particolare che
`Battle1 Camera+0x4C` non e un timer persistente in questo binario:
`0x00234720` lo riscrive incondizionatamente a `60` prima dei rami successivi.
Non viene quindi alterato per imitare una semantica classica non dimostrata.
Gli incrementi `animation_state` sono invece transizioni di inizializzazione
che abbandonano immediatamente lo stato guardia e non si ripetono nel secondo
substep.

La matrice completa e in
`tools/oot3d/decomp_support/analysis/camera_mode_countdown_timing_verify.md`;
la ricostruzione approfondita del primo caso resta in
`camera_normal1_timing_verify.md`, mentre la macchina speciale e verificata
in `camera_special5_timing_verify.md`. La classificazione completa dei
mutatori discreti e in `camera_setting1_discrete_mutator_inventory.md`. I test
coprono i dodici descriptor e i
tre stati Special5, crossing, hold, wrap, registri, continuation, condizione
ARM e ownership. Il catalogo contiene ora `93` entry tipizzate.
Whole-AOT riporta `138820` safe entry e `689` boundary esclusi.

### Primo kernel temporale di Actor_UpdateAll promosso

Il prossimo owner ad alto impatto e `Actor_UpdateAll` a `0x00461344`. Il corpo
OOT3D resta per ora autorevole per spawn differiti, object readiness, ordine
delle dodici categorie, targeting, collision registration, callback indiretti
e delete. E stato pero promosso come un unico kernel temporale il cluster che
decide quando tali callback possono avanzare:

| Stato | Layout nativo | Boundary | Semantica Enhanced60 |
| --- | --- | ---: | --- |
| freeze globale | `ActorContext+0x02`, `u8` | `0x00461460` | decremento al crossing |
| freeze istanza | `Actor+0x118`, `u16` | `0x00461730` | hold e skip intermedio; `1 -> 0` apre il callback |
| color filter | `Actor+0x11A`, `u16` | `0x00461784` | decremento nonzero al crossing |
| throttle SFX | `Actor+0x19C`, `s16` | `0x00461784` | decremento solo signed-positive |

Il freeze per istanza e una state machine, non un semplice campo da
dimezzare. Il corpo nativo decrementa, zero-estende e usa il risultato per
decidere se saltare l'update dell'attore. Nel substep intermedio il bridge
conserva il valore e continua sul ramo skip `0x004617C4`; al crossing inoltra
al gate callback `0x00461750` soltanto un valore gia zero o appena arrivato a
zero. Gli attori non congelati possono quindi aggiornarsi a 60 Hz senza
dimezzare la durata dei freeze.

La coppia color/SFX e trattata nello stesso basic block e conserva signedness,
registri e flag ARM. La telemetria conta separatamente advance, hold e
decisioni del gate. Il contratto completo e in
`tools/oot3d/decomp_support/analysis/actor_update_all_timing_verify.md`.
I test coprono i tre confini, inclusi zero, `1 -> 0`, sentinel SFX negativo,
continuation e fallback ABI. Il catalogo contiene ora `96` entry tipizzate;
whole-AOT riporta `138819` safe entry e `692` boundary esclusi.

Questa promozione chiude il kernel temporale comune dell'owner, non il grafo
completo: lifecycle e callback specifici degli attori restano compatibility
island finche non vengono portati per famiglia.

### Primo sottografo temporale della famiglia EnKo promosso

La trace source-coverage della Foresta Kokiri seleziona
`EnKo_Update@0x001B5F14` con `284` ingressi campionati distribuiti su `30`
basic block. Il primo sottografo specifico portato e la macchina blink/RNG
nativa:

| Stato | Layout OOT3D | Boundary | Semantica Enhanced60 |
| --- | --- | ---: | --- |
| timer occhi | `EnKo+0x2B8`, `s16` | `0x001B6028` | decremento al crossing |
| indice sequenza | `EnKo+0x2BA`, `u16` | `0x001B6028` | avanza quando il timer raggiunge zero |
| ritorno RNG | `r0 -> EnKo+0x2B8`, `strh` | `0x001B6074` | commit del risultato nativo |

Il blocco originale decrementa il timer nonzero con wrap `s16`. Se arriva a
zero, avanza la sequenza nello stesso frame. L'indice `4` viene azzerato e
chiama `Rand_S16Offset@0x003702C8` con base e range nativi entrambi uguali a
`30`. Il bridge conserva registri, flag `CMP`, link register, scrittura
halfword e continuation `0x001B6130`; non genera numeri casuali propri. Nei
substep intermedi conserva entrambi i campi, lasciando il resto dell'update
EnKo libero di avanzare a 60 Hz.

La chiamata animazione di EnKo usa
`SkelAnime_UpdateNoPlayState@0x003731E0`, che imposta `r1=0` e tail-branch a
`SkelAnime_Update@0x0036B4EC`, gia tipizzato e rate-aware. Non e quindi
giustificato un secondo correttore temporale specifico per l'animazione EnKo.

La classificazione focalizzata di
`EnKo_UpdateTrackingAndAnimation@0x00171ED4` non trova un secondo timer,
accumulatore, RNG o incremento di frame locale. Il helper compone invece
primitive gia tipizzate: `Npc_UpdateTrackingByPreset@0x0034C664` effettua
cinque chiamate a `Math_SmoothStepToS@0x00375A18`, rate-aware;
`Animation_OnFrameImpl@0x003736FC` conserva gli eventi sui frame authored; i
cambi animazione passano dal percorso `Animation_Change` nativo. Le query di
facing e scena non mutano tempo, mentre il builder fidget legge
`PlayState+0x5BF4` senza esserne il writer.

Il tracking EnKo e quindi chiuso temporalmente per composizione e deve
continuare a girare a ogni substep Enhanced60. Introdurre un gate locale
duplicherebbe gli owner gia corretti e renderebbe il tracking visibilmente
meno fluido. Restano separati action callback a `EnKo+0x228`, dialoghi, writer
del contatore fidget e lifecycle completo della famiglia EnKo.

Il contratto binario, la correzione dell'omonimia con
`EnHoll_Update@0x001F692C` e la matrice test sono in
`tools/oot3d/decomp_support/analysis/enko_blink_timing_verify.md`; la
classificazione del tracking e l'export Ghidra focalizzato sono in
`tools/oot3d/decomp_support/analysis/enko_tracking_timing_verify.md`. Il
catalogo contiene ora `98` entry tipizzate; whole-AOT riporta `138817` safe
entry e `694` boundary esclusi.

### Primo sottografo temporale EnKanban promosso

`EnKanban_Update@0x0022C284` e un callback nativo di `5136` byte, identificato
dal record ActorInit `0x0052B490` dell'attore `0x0141`. Non viene gateato come
blocco unico: contiene contemporaneamente fase authored, smoothing, timer,
fisica dei pezzi, collisioni, RNG ed eventi.

Il primo sottografo completo promosso separa questi owner:

| Owner | Layout OOT3D | Boundary | Semantica Enhanced60 |
| --- | --- | ---: | --- |
| fase authored | `EnKanban+0x1A8`, `u8` | `0x0022C2C4` | incremento al crossing, hold intermedio |
| gate ripple | `TST mask, phase` | `0x0022D1E0` | evento solo sul crossing |
| consumo oscillatorio | letture a `0x0022D12C/0x0022D164` | primitive comuni | smoothing a ogni substep |

L'incremento conserva la differenza ARM tra risultato `ADD` a 32 bit e
scrittura `STRB`: da `0xFF`, `r0` diventa `0x100` mentre il campo torna a
zero. Il gate ripple riproduce `r1`, flag `N/Z`, preserva `C/V` e segue la
condizione nativa sul crossing; sul substep intermedio sopprime solo
l'evento duplicato. I target sinusoidali convergono tramite
`Math_SmoothStepToSUpdateRate@0x00370084`, gia rate-aware, quindi non ricevono
un secondo correttore ad hoc.

Il secondo sottografo promosso copre i tre countdown dello stato integro:

| Owner | Layout OOT3D | Boundary | Effetto nativo conservato |
| --- | --- | ---: | --- |
| countdown stato zero | `+0x1B2`, `s16` | `0x0022C31C` | decremento nonzero con wrap |
| countdown flag attore | `+0x1F2`, `s16` | `0x0022C32C` | clear bit 0 solo al risultato `1` |
| cooldown interazione | `+0x1F5`, `u8` | `0x0022C374` | `1 -> 0` resta bloccante per quel frame |

I boundary iniziano dopo i `CMP` nativi e ne validano valore esteso e flag
`N/Z/C/V`. La nuova primitiva generica effettua il decremento signed tramite
la rappresentazione binaria unsigned, evitando overflow C++ indefinito ma
conservando il wrap halfword ARM. Branch ed effetti laterali rimangono
site-specific nel bridge.

Il terzo sottografo promosso copre la coppia `+0x1EE/+0x1F0`: un timer
signed guida una salita di `0xFF` e una discesa di `0x41`, con clamp
nell'intervallo `0..0xFF`. `EnKanban_Draw@0x0022BE4C` usa tuttavia
`+0x1F0` solo come predicato nonzero per il modello effetto a `+0x254`;
la grandezza non alimenta colore, alpha o scala. La coppia e quindi un
draw-gate authored, mantenuto sui valori wire originali tra i crossing,
non una rampa visiva da interpolare.

Il quarto sottografo copre la lifetime `s16` a `+0x1AA` nel `case 2`.
Assegnazioni native `0x96/0x69` alimentano un countdown che, sia da zero
preesistente sia sul crossing `1 -> 0`, porta `+0x1AC` allo stato `3`.
Timer e transizione avanzano atomicamente solo sul crossing authored; il
bridge conserva wrap halfword, `r0`, flag e scritture native.

Il quinto sottografo copre l'oscillatore angolare fisico sui due assi:

| Asse | Displacement/velocity/direction | Boundary | Continue |
| --- | --- | ---: | ---: |
| X | `+0x1C0/+0x1C6/+0x1CC` | `0x0022CA9C` | `0x0022CB38` |
| Y | `+0x1C4/+0x1CA/+0x1CD` | `0x0022CB48` | `0x0022CBB4` |

Il codice nativo integra `x += v; v += -0xC00`, invertendo il delta di
posizione secondo il byte direction, azzera asse e velocita sul crossing
dello zero a contatto col terreno e satura la velocita a `-0x1200`. Un
semplice Euler dimezzato cambierebbe l'endpoint originario. Il kernel
tipizzato compone invece il passo discreto su una frazione `h`:

```text
delta_x = v*h + 0.5*a*h*(h-1)
delta_v = a*h
```

Due substep Enhanced60 raggiungono cosi lo stesso endpoint halfword di un
update Native30, ma espongono uno stato fisico intermedio reale a collisione
e draw. Non viene aggiunto stato host.

La decompilazione corregge inoltre la classificazione precedente di
`+0x1CE/+0x1D0`: sono ampiezze residue di rimbalzo decrementate di `5`
soltanto quando l'asse torna a zero, non timer. I relativi eventi RNG,
collision response, audio e particelle restano nativi e possono attivarsi su
qualunque simulation tick; gatearli ai crossing authored sarebbe errato.

Il contratto e la matrice test sono in
`tools/oot3d/decomp_support/analysis/enkanban_phase_timing_verify.md`; export
e decompilato focalizzati sono in
`tools/oot3d/decomp_support/analysis/enkanban_timing_ghidra_export/`. Il
contratto dei countdown e in
`tools/oot3d/decomp_support/analysis/enkanban_state0_timing_verify.md`; il
draw gate e verificato in
`tools/oot3d/decomp_support/analysis/enkanban_draw_gate_timing_verify.md`; la
lifetime in
`tools/oot3d/decomp_support/analysis/enkanban_piece_lifetime_timing_verify.md`;
l'oscillatore in
`tools/oot3d/decomp_support/analysis/enkanban_oscillator_timing_verify.md`.
Il catalogo contiene ora `107` entry tipizzate; whole-AOT riporta `138816`
safe entry e `703` boundary esclusi.

## Definizione della feature

Il runtime deve esporre modalita semantiche, non una coppia libera di numeri:

```cpp
enum class GameplayTimingMode {
    Native30Interpolated,
    Native30NoInterpolation, // diagnostica
    Enhanced60,
};
```

`Enhanced60` significa obbligatoriamente:

- 60 update gameplay autorevoli al secondo;
- 60 valutazioni di animazione e collisione al secondo;
- 60 frame PICA nativi distinti al secondo;
- interpolazione/replay del frame PICA disattivati;
- nessuna accelerazione delle durate espresse in frame originali;
- nessuna duplicazione di eventi, audio o RNG gameplay;
- nessun owner attivo falsamente dichiarato 60 Hz mentre viene ancora eseguito
  interamente a 30 Hz.

La modalita deve essere selezionabile da configurazione prima del boot. Un
toggle live puo essere aggiunto solo dopo che clock, phase e savestate sono
serializzati; non e necessario per la prima versione.

Configurazione proposta:

```json
{
  "gameplay_timing": {
    "mode": "enhanced60",
    "report_compatibility_islands": true
  }
}
```

La baseline resta `native30_interpolated` finche i criteri di completezza
elencati in fondo non sono soddisfatti.

## Modello temporale

Il contesto temporale source-facing deve essere esplicito:

```cpp
struct Oot3dTimeContext {
    uint64_t simulationTick;
    double deltaSeconds;
    float nativeUpdateRate;

    double previousLogicalFrame;
    double currentLogicalFrame;
    uint32_t logicalFrameIndex;
    bool crossedLogicalFrame;
};
```

Valori:

| Modalita | `deltaSeconds` | `nativeUpdateRate` | delta frame logico |
| --- | ---: | ---: | ---: |
| Native 30 | `1/30` | `2` | `1` |
| Enhanced 60 | `1/60` | `1` | `0.5` |

Il frame logico e espresso nelle unita in cui sono stati progettati timer,
cutscene e soglie originali. Il tick di simulazione identifica invece ogni
vero substep a 60 Hz.

### Tre domini

Ogni mutazione temporale deve appartenere a uno dei seguenti domini.

**Continuo**

Eseguito ogni update a 60 Hz:

- campionamento input held;
- accelerazione, velocita, gravita e posizione;
- collisione e risposta fisica;
- rotazione e targeting continui;
- avanzamento e campionamento delle animazioni;
- root motion;
- solve continuo della camera;
- interpolazione nativa di luce, materiali e ambiente;
- aggiornamento e draw dei sistemi gia espressi in rate o secondi.

**Frame logico originale**

Avanza di una unita ogni due update Enhanced60:

- timer interi `++` e `--`;
- timeout e debounce progettati in frame;
- transizioni one-shot;
- comandi discreti delle cutscene;
- spawn, kill, danno, invulnerabilita e flag quando frame-driven;
- notifiche di animazione e SFX;
- chiamate RNG legate a un evento o a un frame logico;
- repeat rate di UI o input quando espresso in frame.

**Clock esterno**

Non dipende dal numero di update gameplay:

- clock DSP e sample audio;
- polling host;
- filesystem;
- UI host e strumenti;
- pacing della swapchain.

Questa classificazione deve essere fatta per sito di mutazione. Una funzione
puo contenere tutti e tre i domini e non puo essere classificata in blocco.

### Timer senza rompere ABI e salvataggi

Non si devono convertire indiscriminatamente i timer guest da interi a float:
si romperebbero layout, salvataggi e codice residuo.

Nel C/C++ tipizzato usare wrapper che conservino il campo intero osservabile:

```cpp
struct NativeFrameTimer {
    bool TickDown(uint8_t& value, const Oot3dTimeContext& time);
    bool TickUp(uint8_t& value, uint8_t limit,
                const Oot3dTimeContext& time);
};
```

In Enhanced60 il wrapper modifica il campo solo quando viene attraversato un
nuovo frame logico. Deve preservare esattamente:

- signedness;
- saturazione o wrap;
- valore letto prima e dopo la mutazione;
- ordine rispetto alle altre istruzioni;
- condizione di trigger sul passaggio a zero.

Non e sempre corretto separare un owner in una grande fase continua e una
grande fase discreta: l'ordine originale puo essere osservabile. La prima
conversione deve quindi sostituire timer ed eventi nel punto originale del
grafo. Fasi separate sono ammesse solo dopo analisi delle dipendenze.

Per il residuo A32 si puo usare temporaneamente un sidecar di fase associato a
identita stabile `owner + generation + field`, ma solo per confini noti e
strumentati. Non deve esistere un intercettore generico di tutte le scritture
`+1/-1`: distinguere un timer da un indice richiede semantica.

## Loop Enhanced60

Il loop raccomandato e:

```text
poll input host e accoda i fronti fisici
accumula wall time

per ogni step 1/60 dovuto:
    costruisci Oot3dTimeContext
    pubblica held input e consuma ogni fronte una sola volta
    imposta nativeUpdateRate = 1 per il residuo A32
    esegui un update source-recompiled completo
    esegui un draw OOT3D nuovo
    avanza simulation tick e frame logico di 0.5

presenta il frame nativo piu recente
```

In questa modalita:

- `Oot3dPicaVisualFrame` non deve produrre sample intermedi;
- history matching e snapshot replay devono essere bypassati;
- `interpolated_draws` e `samples_presented` devono restare zero;
- non deve esistere il ritardo intenzionale di un frame usato dal percorso
  interpolato.

Il campionamento fra keyframe CSAB o curve camera e parte della valutazione
nativa dell'asset e resta attivo. Non e interpolazione fra due frame
renderizzati.

Il runtime non deve scartare silenziosamente tick di simulazione per recuperare
un frame lento. Se non mantiene 60 Hz puo saltare una presentazione, ma deve
continuare a contabilizzare il debito simulazione entro un limite protettivo e
segnalare la condizione. Il limite evita nuovi cicli di catch-up senza fine,
ma non deve essere mascherato come esecuzione corretta a velocita reale.

## Strategia source-recompiled

### Principio

L'unita di migrazione e un owner graph completo, non una leaf A32.

Le 48 leaf tipizzate attuali sono utili come contratti e test, ma ogni
transizione fra C++ e dispatcher frammenta il flusso. Per il 60 Hz occorre che
l'owner possieda direttamente:

- stato tipizzato;
- ordine di update;
- chiamate a movimento, collisione e animazione;
- mutazioni dei timer;
- eventi e RNG;
- accesso a renderer, audio e input tramite servizi tipizzati;
- serializzazione e telemetria.

Il codice committato in `I:\oot3decomp` va importato per copia con manifest di
provenienza. `I:\oot3decomp` resta sola lettura e non viene incluso direttamente
nella build.

### Owner graph prioritari

1. Identificare e importare il target virtuale `PlayState` chiamato da
   `GameState_Update`.
2. Recuperare il confine fuso `Actor_UpdateAll` e il suo ordine per categoria.
3. Rinominare e promuovere il vero owner Player a `0x00250AD0`.
4. Collegare direttamente movimento, root motion, collisione e animazione
   Player gia leggibili.
5. Promuovere l'owner cutscene `0x00321F50` e
   `Cutscene_ProcessCommands`.
6. Promuovere `Camera_Update` mantenendo separati solve e timer nel loro ordine.
7. Promuovere ambiente, effetti e notifiche audio.

Queste decompilazioni hanno piu valore per la feature di ulteriori leaf
matematiche isolate. Le leaf recenti di matematica e collisione riducono il
rischio implementativo, ma da sole non risolvono l'accelerazione.

### Isole di compatibilita

Un owner non ancora migrato puo restare temporaneamente A32:

- viene aggiornato con la semantica originale a 30 Hz;
- viene dichiarato nella telemetria come `legacy30_compatibility_island`;
- non viene contato nella percentuale di gameplay true-60;
- non blocca lo sviluppo degli owner gia convertiti;
- deve scomparire prima che la feature perda l'etichetta sperimentale.

Eseguire un owner A32 completo ogni due tick non rende quell'attore 60 Hz.
Eseguire lo stesso owner due volte a rate `1` puo invece accelerarne timer ed
eventi. Non esiste un fallback generico che soddisfi entrambe le proprieta.

La telemetria deve elencare le isole attive nella scena corrente, idealmente
per attore e funzione. Questo evita di interpretare una scena apparentemente
fluida come copertura completa.

## Trattamento per sottosistema

### Player

Il Player e il primo vertical slice consigliato perche produce un beneficio
giocabile immediato e concentra i rischi principali.

Da convertire nello stesso grafo:

- acquisizione input e fronti;
- selezione action;
- accelerazione e velocita;
- rotazione;
- root motion;
- animazione;
- collisione ambiente;
- ladder, ledge, jump, roll, swim e danno;
- timer e invulnerabilita;
- notifiche SFX;
- RNG Player.

Il target iniziale e la Foresta Kokiri con:

- idle, walk e run;
- partenza e arresto;
- cambio direzione;
- salto e atterraggio;
- staccionata e piccoli ledge;
- scala verticale;
- entrata e uscita da acqua;
- interazione con attore e porta.

Non basta confrontare la posizione finale. Vanno confrontati durata delle
action, contatti collisione, frame di transizione, root delta e sequenza SFX.

### Animazione e root motion

Usare `SkelAnime_Update` ogni tick a rate `1`.

Non modificare:

- play speed degli asset;
- lunghezza CSAB;
- frame count;
- curve o matrici;
- origine dello skeleton.

Le notifiche usano attraversamento di intervallo. Il root motion usa il delta
fra due pose consecutive a 60 Hz. Due root delta a 60 devono comporre, entro
tolleranza numerica, lo spostamento reale di un update a 30 Hz.

Vanno testati:

- loop e once;
- playback reverse;
- morph e taper;
- wrap del frame zero;
- frame finale;
- cambio animazione nello stesso tick;
- eventi multipli attraversati da un passo;
- animazioni che cambiano action.

### Fisica e collisione

Movimento, gravita e collisione vengono eseguiti ogni `1/60`.

La parita richiesta e semantica:

- stessa velocita per secondo;
- stessa altezza e durata nominale di salto entro tolleranza;
- stessi contatti e stesse superfici;
- nessun tunneling o doppia risposta;
- stessa action finale;
- nessuna accelerazione di timer di ledge o floor stability.

Una lieve divergenza numerica dalla traiettoria 30 Hz e attesa per il substep
piu fine. Non va corretta reintroducendo una simulazione a 30 Hz nascosta.

### Attori

`Actor_UpdateAll` deve mantenere:

- ordine per categoria;
- spawn e kill differiti;
- parent/child;
- object readiness;
- collision registration;
- ordine relativo Player/NPC/effect;
- generation identity.

Per ogni famiglia di attori classificare separatamente:

- moto continuo;
- timer action;
- animazione;
- emissione effetti;
- RNG;
- audio;
- collisione e danno.

La priorita dopo il Player e sugli attori presenti nella Foresta Kokiri e nelle
prime cutscene. La metrica non e il numero assoluto di funzioni convertite, ma
la quota di tempo runtime eseguita dentro owner graph source completi.

### Cutscene

Introdurre un cursore floating in frame originali:

```cpp
struct CutsceneFrameCursor {
    double previous;
    double current;
    uint16_t legacyVisibleFrame;
};
```

A 60 Hz `current += 0.5`.

L'interprete QDB deve essere separato logicamente in:

- selezione delle cue attive;
- valutazione continua di camera, attori, luce e colore al tempo floating;
- dispatch una sola volta degli eventi attraversati;
- gestione delle mutazioni intenzionali del frame.

Un evento al frame `N` deve usare:

```text
previous < N && N <= current
```

con la variante corretta per playback reverse o salti espliciti. Non deve
dipendere da `current == N`.

Le modifiche esplicite del frame osservate in `Cutscene_ProcessCommands`
richiedono un'API di seek che:

- aggiorni previous/current;
- invalida o ricostruisca le cue attive;
- non riemetta eventi gia consumati salvo rewind intenzionale;
- serializzi il proprio stato.

### Camera

La camera normale deve essere calcolata ogni tick usando Player e collisione
aggiornati. I suoi timer interi restano sulla timeline frame-logica.

La conversione deve coprire nello stesso owner:

- mode selection;
- camera data index;
- collisione camera;
- solve eye/at/up;
- smoothing rate-scaled;
- timer e quakes;
- cut e transizioni;
- interface side effects.

Camera cut e teleport azzerano qualsiasi history del renderer, anche se
Enhanced60 non usa l'interpolatore PICA.

### Ambiente, materiali ed effetti

I parametri continui possono usare `deltaSeconds` o `nativeUpdateRate`.
Le sequenze indicizzate per frame usano `currentLogicalFrame`.

Da distinguere:

- day time e transizioni luce;
- fog e sky;
- texture/material animation;
- acqua;
- particelle;
- lens flare e altri effetti screen-space.

Per le particelle e preferibile:

- emettere secondo la cadenza originale se l'emissione e frame-driven;
- simulare le particelle esistenti a 60 Hz;
- mantenere la vita in secondi o frame logici;
- non raddoppiare le chiamate al RNG gameplay.

Una casualita puramente visiva puo usare uno stream separato e deterministico,
ma solo dopo aver provato dal codice OOT3D che non influenza gameplay o ordine
del RNG globale.

### RNG

Il generatore gameplay e globale e ad alto fan-in. Raddoppiare le chiamate
cambia non soltanto un effetto, ma tutta la sequenza successiva.

Regole:

- le chiamate legate a eventi discreti avvengono una sola volta;
- il test registra callsite, ordine e valore restituito;
- nessuna chiamata viene aggiunta per rendere un effetto piu fluido;
- i sistemi visual-only possono avere un flusso separato solo con ownership
  dimostrata;
- il numero di chiamate gameplay a tempo equivalente deve coincidere fra 30 e
  60 Hz, salvo deviazioni esplicitamente approvate.

### Audio

Il DSP e il mixer restano sul clock dei sample nativo e non vengono chiamati
due volte per compensare il gameplay.

La feature deve garantire:

- un solo SFX per notifica;
- stessa durata e pitch;
- stessa sequenza BGM;
- fade espressi in tempo reale, non in numero di update non normalizzato;
- nessun doppio enqueue fra due substep.

Il confronto deve includere un event ledger audio. Il confronto PCM e utile per
continuita, ma non sostituisce il controllo dei trigger.

### Input e UI

Il polling host e indipendente dal tick gameplay. Lo stato held viene
pubblicato a ogni update; ogni fronte fisico viene accodato e consumato una
sola volta.

Questo riusa la lezione del bug Start: un input non deve essere perso in un
frame host senza update ne ripetuto perche il tasto resta premuto.

Menu, HUD e dialoghi non devono accelerare. I loro timer vengono classificati
come frame logici o wall-clock secondo il consumer originale. La modalita 60
Hz non cambia asset, layout o profilo UI.

## Inventario temporale automatico

Prima della conversione massiva va aggiunto un tool, per esempio:

```text
tools/oot3d/frame_rate_audit/
```

Input:

- `analysis/functions_enriched.json`;
- `analysis/callgraph.json`;
- pseudocodice Ghidra;
- sorgente C mantenuto;
- symbol map;
- trace runtime.

Output:

```text
temporal_inventory.json
temporal_inventory.csv
temporal_inventory.md
```

Ogni sito deve registrare:

- funzione e address;
- owner graph;
- campo letto o scritto;
- tipo di operazione;
- uso diretto di update rate;
- confronto equality/range;
- chiamata RNG;
- chiamata audio;
- evento actor/cutscene/animation;
- classificazione `continuous`, `logical_frame`, `external`, `unknown`;
- evidenza e confidenza;
- stato di migrazione e test.

Le euristiche producono candidati; una tabella di override versionata contiene
le classificazioni confermate. Il gate va applicato solo al grafo che si sta
promuovendo: non deve bloccare il lavoro per migliaia di candidati estranei.

La trace runtime aggiunge priorita:

- hit count per funzione e sito;
- attori attivi;
- timer realmente mutati;
- RNG e audio effettivamente chiamati;
- compatibility island attraversate.

Questo consente di risolvere prima il codice che influenza la scena di test
senza perdere la prospettiva globale.

## Verifica differenziale

### Metodo

Eseguire 30 e 60 Hz dallo stesso checkpoint con lo stesso input timestamped.
Confrontare gli stati ogni `1/30` di secondo, cioe dopo due update Enhanced60.

Non usare come unico oracolo il fingerprint completo della memoria:

- `GameState+0xF8` e altri contatori possono avere semantica diversa;
- il substep fisico produce differenze floating legittime;
- renderer e allocazioni possono cambiare indirizzi o ordine interno.

Usare due canali.

**Event ledger, confronto esatto**

- input edge consumati;
- Player action enter/exit;
- actor spawn/kill con identity e generation;
- flag e inventario;
- danno, health e invulnerabilita;
- collision contact begin/end;
- animation notify;
- cutscene command;
- SFX/BGM;
- RNG callsite, ordine e risultato;
- room e scene transition.

**Stato continuo, confronto con tolleranza**

- posizione, velocita e accelerazione;
- yaw/pitch;
- frame animazione;
- root motion;
- eye/at/up camera;
- parametri luce, fog e materiale;
- coordinate degli attori.

Ogni divergenza deve riportare il primo owner e il primo sito temporale, non
solo lo screenshot finale.

### Scenari minimi

1. Kokiri: idle, walk, run, stop e cambio direzione.
2. Kokiri: staccionata, piccolo ledge e scala verticale.
3. Kokiri: collisione, acqua, porta e interazione NPC.
4. Volo di Navi attraverso la foresta.
5. Sogno iniziale di Link.
6. Combattimento con attacco, danno, invulnerabilita e proiettile.
7. Piattaforma mobile e dyna collision.
8. Cutscene con camera, BGM, luce e spawn.
9. Dialogo, pausa e transizione stanza.
10. Run lunga da 60 secondi con RNG e audio attivi.

Il vecchio test da un secondo resta un unit smoke, non il gate della feature.

### Invarianti strutturali Enhanced60

- `game_state_updates_observed == 60` per ogni secondo stabile;
- un nuovo frame PICA per ogni update;
- zero draw interpolati o riprodotti;
- nessun frame history delay;
- zero tick simulazione scartati nelle run di convalida;
- stessa durata wall-clock di action e cutscene;
- stesso numero e ordine di eventi discreti;
- stesso numero e ordine di chiamate RNG gameplay;
- nessun SFX duplicato;
- nessuna compatibility island nel percorso dichiarato completo.

## Savestate e compatibilita

I normali salvataggi del gioco non devono contenere la preferenza 60 Hz e
restano compatibili.

Il savestate host attuale serializza solo:

- frame count;
- refresh remainder;
- next VBlank;
- process run kind.

Per Enhanced60 va introdotta una nuova ABI che aggiunga:

- `GameplayTimingMode`;
- simulation tick;
- previous/current logical frame;
- fase half-frame;
- fronti input pendenti;
- sidecar dei timer ancora necessari;
- event-dispatch state delle cutscene;
- identity/generation delle compatibility island.

Il caricamento di un vecchio savestate puo inizializzare la fase a un confine
intero solo in modalita 30 Hz. Per passare a Enhanced60 e preferibile creare un
nuovo checkpoint o effettuare una conversione esplicita a un confine
quiescente. Non dedurre silenziosamente una fase che potrebbe riemettere un
evento.

## Prestazioni

La correttezza temporale e la prestazione vanno misurate separatamente.

Profilo corretto:

- release build;
- diagnostica estesa disattivata salvo trace campionata;
- VSync e frame limit disattivati per misurare headroom;
- intervallo di misura dopo caricamento e shader warm-up;
- FPS calcolati sul tempo wall-clock della run effettiva;
- update, draw e presentazioni riportati separatamente;
- audio attivo in una seconda matrice;
- stessa scena, camera e durata per ogni confronto.

Budget da separare:

- gameplay source;
- fallback A32 e transizioni;
- costruzione command list PICA;
- upload/copia texture;
- backend NRI;
- audio;
- presentazione;
- eventuale telemetria.

Enhanced60 deve rimuovere i costi di:

- matching draw fra frame;
- cattura della history usata solo dall'interpolazione;
- campionamento degli snapshot;
- replay dei draw intermedi.

Deve invece sostenere circa il doppio di:

- owner update;
- collisione;
- animazione;
- command generation;
- draw submission nativa.

Il criterio di rilascio non e "tocca 60 una volta", ma almeno 60 FPS con
headroom nella matrice di scene target e senza VSync che nasconda il limite.
Una build che non raggiunge il target conserva la correttezza e segnala il
deficit; non accelera, non salta timer e non entra in catch-up infinito.

## Piano operativo

### Fase 0: infrastruttura

1. Sostituire le opzioni numeriche libere con `GameplayTimingMode`.
2. Introdurre `Oot3dTimeContext` e serializzarne la fase.
3. Bypassare completamente l'interpolatore PICA in Enhanced60.
4. Aggiungere event ledger e compatibility-island telemetry.
5. Costruire l'inventario temporale automatico.
6. Conservare test bit-identici della baseline Native30.

Uscita: la modalita si avvia e produce 60 frame nativi, ma e marcata
sperimentale e riporta chiaramente i grafi non convertiti.

### Fase 1: vertical slice Player/Kokiri

1. Importare un commit stabile della decompilazione.
2. Promuovere il vero owner Player `0x00250AD0`.
3. Collegare direttamente primitive di moto, collisione e SkelAnime.
4. Tipizzare i timer Player usati nello scenario.
5. Normalizzare notifiche, audio e RNG.
6. Chiudere la matrice locomotion/ledge/ladder/acqua/interazione.

Uscita: Player e collisione realmente a 60 Hz nella Foresta Kokiri, senza
accelerazione e senza rientro A32 nel grafo Player.

### Fase 2: owner attori

1. Recuperare `Actor_UpdateAll`.
2. Portare liste, lifecycle e order.
3. Convertire gli attori Kokiri e gli oggetti interattivi.
4. Espandere per famiglie in base alla trace runtime.

Uscita: nessuna compatibility island attiva nella Foresta Kokiri.

### Fase 3: cutscene e camera

1. Implementare `CutsceneFrameCursor`.
2. Separare valutazione continua e dispatch one-shot QDB.
3. Convertire `Camera_Update`.
4. Verificare volo di Navi e sogno iniziale.
5. Verificare luce, ambiente, BGM e spawn lungo le cutscene.

Uscita: le prime cutscene hanno 60 stati reali al secondo e conservano durata
ed eventi.

### Fase 4: ambiente, effetti e audio

1. Materiali e texture animate.
2. Acqua, fog, cielo e luce.
3. Particelle e visual RNG.
4. Audio trigger e fade.
5. UI e transizioni.

Uscita: nessun sottosistema della scena target accelera o duplica eventi.

### Fase 5: chiusura globale

1. Espandere la matrice a dungeon, boss e overworld.
2. Eliminare le compatibility island attive.
3. Eseguire run lunghe e audit RNG.
4. Chiudere prestazioni NRI con headroom.
5. Rendere Enhanced60 non sperimentale.

## Ordine richiesto alla decompilazione

Per massimizzare il rapporto costo/resa, le prossime promozioni C utili sono:

1. target virtuale PlayState chiamato da `GameState_Update`;
2. `Actor_UpdateAll` e ordine delle liste;
3. owner Player a `0x00250AD0`, con campi timer nominati;
4. owner cutscene `0x00321F50`;
5. `Cutscene_ProcessCommands` con record QDB tipizzati;
6. `Camera_Update` e timer camera tipizzati;
7. update ambiente ed effetti;
8. owner audio gameplay.

Per ogni owner servono:

- layout e signedness dei campi;
- call graph completo;
- globali e tabelle native;
- siti `+1/-1`;
- confronti equality;
- RNG e audio callsite;
- side effect su PlayState, SaveContext e Actor;
- test contro il binario originale.

Il sorgente N64 puo suggerire un nome o un ruolo quando il codice OOT3D e
ambiguo, ma non determina timing, layout o costanti. L'autorita resta il
codice e il formato OOT3D.

## Approcci da escludere

- Impostare globalmente `updateRate=1` e considerare concluso il lavoro.
- Chiamare tutto il gameplay due volte senza classificare i contatori.
- Alternare rate `1/0`: il passo zero non impedisce timer, RNG e side effect.
- Eseguire tutto a 30 Hz e renderizzare due volte: non e gameplay a 60 Hz.
- Interpolare transform nel renderer: e precisamente il percorso da rendere
  opzionalmente superfluo.
- Dimezzare tutte le costanti o le velocita delle animazioni.
- Modificare CSAB o altri asset.
- Convertire tutti i timer guest in float rompendo ABI e salvataggi.
- Applicare regex ai decrementi dell'export Ghidra.
- Usare il fingerprint memoria completo come unico gate.
- Aggiungere patch per scena o attore senza contratto generale.
- Aspettare la decompilazione completa prima di iniziare gli owner prioritari.
- Ottimizzare ulteriormente A32 come architettura finale della feature.

## Criteri di completezza

Enhanced60 puo essere dichiarata completa soltanto quando:

1. il percorso di presentazione non usa frame interpolation o replay;
2. ogni frame visibile deriva da un update e draw OOT3D nuovi;
3. Player, collisione, animazione e camera vengono aggiornati a 60 Hz;
4. gli owner attivi non contengono compatibility island;
5. timer e cutscene conservano durata wall-clock;
6. event ledger, audio e RNG non mostrano duplicazioni o omissioni;
7. i normali salvataggi restano compatibili;
8. i savestate serializzano il nuovo clock;
9. la baseline 30 Hz non cambia;
10. il renderer NRI mantiene almeno 60 FPS con headroom negli scenari target.

## Riferimenti locali

Runtime:

```text
tools/oot3d/native_game_runtime/oot3d_native_frame_rate.*
tools/oot3d/native_game_runtime/oot3d_native_a32_window.cpp
tools/oot3d/native_game_runtime/oot3d_native_pica_visual_frame.*
tools/oot3d/native_game_runtime/oot3d_native_a32_savestate.*
tools/oot3d/typed_gameplay/oot3d_gameplay_time.*
tools/oot3d/typed_gameplay/oot3d_gameplay_quake.*
tools/oot3d/native_game_runtime/oot3d_typed_gameplay_bridge.*
scripts/oot3d/Test-Oot3dFrameRateEquivalence.ps1
native_game/frame_rate_equivalence/
```

Decompilazione:

```text
I:\oot3decomp\docs\RECONSTRUCTION_STATUS.md
I:\oot3decomp\docs\DECOMPILATION_STRATEGY.md
I:\oot3decomp\analysis\functions_enriched.json
I:\oot3decomp\analysis\callgraph.json
I:\oot3decomp\src\code\z_skel_anime.c
I:\oot3decomp\src\code\z_skel_anime_api.c
I:\oot3decomp\src\code\z_actor_core.c
I:\oot3decomp\src\overlays\actors\ovl_player_actor\z_player_collision.c
I:\oot3decomp\ghidra_export\decompiled\
  02370_00250ad0_oot3d_player_action_turn_in_place.c
I:\oot3decomp\ghidra_export\decompiled\
  03447_002c5ba0_Cutscene_ProcessCommands.c
I:\oot3decomp\ghidra_export\decompiled\
  03629_002d84c4_Camera_Update.c
I:\oot3decomp\ghidra_export\decompiled\
  04768_00321f50_FUN_00321f50.c
I:\oot3decomp\ghidra_export\decompiled\
  07589_00417014_GameState_Update.c
```

Questo documento sostituisce le conclusioni troppo forti dei precedenti smoke
test 30/60: quelle prove restano valide per le primitive coperte, ma non
dimostrano equivalenza temporale dell'intero gameplay.
