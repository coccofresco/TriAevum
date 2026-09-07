# Differenze Di Gameplay OOT N64 / OOT3D E Prerogative Native

## Scopo

Questo documento identifica le differenze di gameplay tra `The Legend of Zelda: Ocarina of Time` per Nintendo 64 e `The Legend of Zelda: Ocarina of Time 3D`. Serve a impedire che il semantic rehosting assuma erroneamente che tutto il comportamento N64 sia autoritativo anche quando OOT3D contiene una modifica deliberata, una correzione selettiva o un sistema esclusivo.

La regola di progetto resta:

- OOT N64/Ship descrive la struttura semantica del gioco, le cause degli eventi e la progressione;
- asset e codice OOT3D possiedono la realizzazione concreta e ogni delta verificato;
- un comportamento esclusivo OOT3D non puo avere un fallback N64 mascherato da implementazione completa;
- HUD, menu e logiche direttamente collegate restano temporaneamente N64 per decisione di scope, ma le differenze OOT3D vengono comunque censite;
- l'emulatore valida il risultato, ma i dump non sono dati runtime;
- il rendering stereoscopico 3DS non e un obiettivo del backend OpenGL corrente.

Ricerca web aggiornata al **13 luglio 2026**.

## Affidabilita Delle Evidenze

| Livello | Significato | Uso nel progetto |
| --- | --- | --- |
| `A - confermato` | Manuale Nintendo, intervista allo staff o comunicazione Nintendo | Il delta deve essere rappresentato nell'architettura. I valori esatti devono comunque provenire dai dati o dal codice OOT3D. |
| `B - corroborato` | Documentazione indipendente dettagliata, possibilmente con riferimenti verificabili | Diventa un caso di test e una pista di decompilazione; non giustifica da solo costanti runtime. |
| `C - da verificare` | Segnalazione comunitaria non sufficientemente corroborata o esplicitamente marcata come incerta | Non si implementa finche asset, `code.bin` o una prova controllata in Azahar non la confermano. |

Le fonti Nintendo descrivono intenzioni e differenze, non necessariamente layout binari o formule. Una conferma di livello A autorizza la ricerca del comportamento nativo, non l'invenzione di parametri plausibili.

## Conclusione Architetturale

OOT3D non e semplicemente il gameplay N64 riprodotto con asset nuovi e un frame rate maggiore. Lo staff conferma che l'aumento del frame rate ha richiesto interventi sulle assunzioni temporali e che i controlli di collisione sono passati da 20 a 30 al secondo; la maggiore precisione aveva gia cambiato la difficolta percepita e fu necessario ritoccare il comportamento. Lo stesso staff conferma che le proporzioni nuove di Link avevano alterato il contatto tra torcia e ragnatela, e che alcuni bug furono conservati deliberatamente mentre altri furono corretti. [Nintendo, collisioni e bug][src-iwata-dev-2] [Nintendo, proporzioni e contatto][src-iwata-dev-1]

Ne conseguono cinque classi di autorita da rendere esplicite nel catalogo semantico:

| Classe | Significato |
| --- | --- |
| `shared_semantics` | Il sorgente N64 puo guidare il flusso, ma il comportamento resta soggetto a verifica OOT3D. |
| `oot3d_override` | Esiste lo stesso concetto, ma OOT3D modifica dati, timing, geometria o regole. |
| `oot3d_exclusive` | Il sistema non ha un omologo runtime N64 utilizzabile. Va ricostruito da dati e codice OOT3D. |
| `n64_ui_scope_exception` | Differenza nota, temporaneamente esclusa perche HUD/menu e logiche collegate restano Ship/N64. |
| `native_evidence_required` | La differenza e plausibile o segnalata, ma non ancora dimostrata. Nessun fallback implicito. |

Ogni route o capability dovrebbe dichiarare almeno `authority_class`, `evidence`, `verification_state` e, quando necessario, `oot3d_decompilation_target`.

## Prerogative Native Non Negoziabili

| Dominio | Evidenza | Prerogativa OOT3D da tutelare | Conseguenza per il rehosting |
| --- | --- | --- | --- |
| Clock e collisioni | A | Update e collisioni con semantica temporale OOT3D, inclusi i ritocchi introdotti per il passaggio 20/30 | Non moltiplicare semplicemente velocita N64 per `30/20`; separare render, simulazione, animazione e timer. |
| Corpo di Link e contatti | A | Skeleton, proporzioni, pose, hit point, weapon tip e test di interazione coerenti con i modelli OOT3D | Le trasformazioni N64 non possono guidare torce, armi, mani, piedi o collider del modello nativo. |
| Compatibilita dei bug | A | Politica selettiva per singolo comportamento | Non assumere ne `tutti i bug N64` ne `tutti corretti`; costruire una matrice verificata. |
| Scene e progressione locale | B, con forte impatto | Geometrie, collisioni, actor list, switch e camere OOT3D possono differire e bloccare sequence break N64 | La semantica N64 seleziona la variante; ZSI e codice OOT3D ne definiscono contenuto e regole concrete. |
| Camera e mira | A | Camera OOT3D, mira analogica/gyro, comportamento durante movimento e targeting | La camera N64 e solo un riferimento semantico; input PC deve alimentare la stessa intenzione nativa. |
| Equipaggiamento rapido | A | Iron/Hover Boots e accesso agli oggetti hanno una semantica OOT3D distinta | E una eccezione UI temporanea, ma non va classificata come comportamento condiviso. |
| Boss Challenge | A | Modalita, unlock, selezione boss, timer, record e gauntlet esclusivi | Richiede stato, flusso e tabelle OOT3D; non esiste un control flow N64 completo da riusare. |
| Visions/Sheikah Stones | A | Sistema di hint contestuale con oltre 130 filmati e condizioni di disponibilita | Richiede indici, flag di progressione, messaggi e timeline native. |
| Master Quest OOT3D | A | Unlock, mondo specchiato, doppio danno, contenuto MQ e regole associate | Va trattato come modalita OOT3D distinta, non come semplice selezione di asset GameCube/N64. |
| Salvataggio | A/B | Default di conferma diverso e stato nativo per slot oggetti, MQ e sistemi esclusivi | Il bridge deve preservare i campi OOT3D anche se la UI resta temporaneamente N64. |

## Differenze Confermate

### 1. Timing, Frame Rate E Collisioni

La presentazione OOT3D e documentata a 30 FPS contro i 20 FPS dell'originale. Piu importante, Nintendo conferma direttamente che:

- il codice N64 conteneva valori che presupponevano ritardi di elaborazione e frame rate variabili;
- aumentare il frame rate fu uno dei problemi tecnici maggiori del remake;
- i controlli di collisione fra Link e i nemici passarono da 20 a 30 al secondo;
- la maggiore precisione rese inizialmente il gioco percepibilmente piu difficile e il comportamento fu corretto per recuperarne il feeling. [Nintendo, sviluppo e collisioni][src-iwata-dev-2] [Zelda Wiki, 30/20 FPS][src-zelda-wiki]

**Implicazioni:**

- il tick gameplay OOT3D va trovato in `code.bin`, non dedotto dal refresh OpenGL;
- collisioni, invulnerability timer, velocita, accelerazioni, turn rate, gravita, animazioni e timer attore vanno auditati separatamente;
- un valore N64 convertito aritmeticamente e solo un'ipotesi diagnostica;
- l'interpolazione grafica non deve modificare la frequenza della simulazione;
- i test devono confrontare traiettorie, contatti e transizioni di stato su tempo reale, non solo frame con lo stesso indice.

**Target di decompilazione:** scheduler/update globale, `Player` update, collision check, damage resolution, timer attore, animation update e conversioni frame/secondi.

### 2. Proporzioni, Animazioni E Punti Di Contatto

Grezzo modifico le proporzioni di Link, allungando arti e alzando la vita. Questo cambio altero l'angolo della torcia rispetto al terreno e ruppe un modo valido di incendiare una ragnatela; lo staff dovette ripristinare l'interazione. [Nintendo, sviluppo del modello di Link][src-iwata-dev-1]

Questo dimostra che modelli e animazioni OOT3D hanno conseguenze gameplay:

- weapon tip e punti di emissione non possono essere copiati dalla skeleton N64;
- collider e test di contatto devono seguire i joint e le trasformazioni native quando il codice OOT3D lo prevede;
- mount socket, mano, piede, torcia, scudo, hookshot e attachment richiedono ownership OOT3D;
- la correttezza visiva della posa non basta a dimostrare la correttezza dell'interazione.

**Test canonico:** riprodurre tutti i metodi validi per accendere la prima ragnatela, inclusa la posa con scudo citata dagli sviluppatori, usando esclusivamente skeleton, animazioni e formule OOT3D.

### 3. Bug Conservati E Bug Corretti

Lo staff dichiara che alcuni bug innocui e divertenti furono reimplementati intenzionalmente come specifiche, mentre quelli non accettabili furono corretti. [Nintendo, politica sui bug][src-iwata-dev-2]

Pertanto il sorgente N64 non e autoritativo sui suoi edge case. Va mantenuto un registro per comportamento:

| Stato | Azione |
| --- | --- |
| Presente anche in OOT3D | Conservare il comportamento con formula nativa o equivalenza dimostrata. |
| Corretto in OOT3D | Inserire un `oot3d_override` e decompilare il nuovo ramo. |
| Sostituito da un bug diverso | Riprodurre il risultato OOT3D, non entrambi. |
| Non verificato | Lasciare il comportamento N64 solo come scaffold provvisorio e marcare il gap. |

La ricca documentazione di tecniche specifiche OOT3D mostra inoltre che il remake possiede un proprio insieme di glitch e sequence break, non una mera copia di quello N64. [ZeldaSpeedRuns, indice tecnico OOT3D][src-zsr]

### 4. Camera, Targeting E Mira

OOT3D introduce input giroscopico opzionale per la mira e per guardarsi attorno. Nintendo specifica che:

- gyro e controllo analogico possono essere alternati senza soluzione di continuita;
- l'opzione motion puo essere disabilitata;
- la mira in prima persona di slingshot e bow usa questo sistema;
- fuori dal targeting, mentre Link si muove, la camera continua a seguirlo e il gyro modifica la linea di vista;
- il marker di targeting e stato adattato alla profondita stereoscopica. [Nintendo, camera e gyro][src-iwata-dev-4] [Manuale Nintendo, controlli][src-manual]

Per il port PC la stereoscopia resta fuori scope, ma la semantica di input no. Mouse, right stick o sensore opzionale devono alimentare il canale nativo di look/aim senza sostituire il controller camera con una free camera generica.

Il manuale conferma anche opzioni native per targeting `HOLD`/`SWITCH`, inversione verticale e motion control. [Manuale Nintendo, opzioni][src-manual]

**Target di decompilazione:** camera mode/state machine, accumulatori analogici e gyro, clamp, smoothing, recenter, interazione con L-targeting e first-person item state.

### 5. Touch Screen, Inventario Ed Equipaggiamento

Nintendo descrive la nuova interfaccia come una modifica con effetto diretto sul gameplay. Iron Boots e Hover Boots possono essere equipaggiati rapidamente; sono disponibili quattro slot oggetto, due fisici (`X`, `Y`) e due touch (`I`, `II`), l'Ocarina ha accesso dedicato e gear/map/inventory sono immediatamente accessibili. [Nintendo, Water Temple e touch][src-iwata-dev-3] [Manuale Nintendo, item e gear][src-manual]

La documentazione tecnica indipendente del save OOT3D mostra record separati per equipaggiamento child/adult/current, quattro slot `X/Y/I/II`, slot boots dedicati e campi coerenti con questa semantica. E una pista di reverse engineering, non una specifica ufficiale completa. [CloudModding, formato save OOT3D][src-save]

**Scope corrente:** HUD, menu e logiche direttamente collegate restano N64. Queste differenze vanno marcate `n64_ui_scope_exception`, non `shared_semantics`. Il formato di salvataggio e il runtime non devono perdere campi OOT3D, cosi da non rendere irreversibile l'eccezione.

### 6. Guidance, Navi E Salvataggio

OOT3D aggiunge:

- Hint Movies accessibili tramite Sheikah Stones;
- oltre 130 filmati, intenzionalmente incompleti per suggerire senza risolvere;
- un promemoria di pausa di Navi ogni 60 minuti;
- conferma di salvataggio con `Save` come default e protezione contro conferme accidentali da button mashing. [Nintendo, Hint Movies e salvataggio][src-iwata-dev-5] [Nintendo, quantita dei filmati][src-iwata-miyamoto]

Questi sistemi richiedono logiche native per disponibilita, progressione, flag visto/completato, selezione messaggi, timer di sessione e persistenza. La UI puo essere rinviata, ma il loro stato non deve essere schiacciato sul save N64.

### 7. Master Quest OOT3D

Nintendo conferma che Master Quest:

- si sblocca dopo il completamento della main quest;
- usa dungeon e puzzle differenti;
- modifica posizione di oggetti e nemici;
- specchia il mondo;
- applica doppio danno a Link. [Nintendo, Master Quest][src-iwata-dev-5] [Nintendo, annuncio delle modalita][src-nintendo-news]

Fonti indipendenti riportano inoltre slot di salvataggio separati e assenza delle Visions in MQ. Questi dettagli sono `B` e vanno verificati nel save e nel codice nativo. [Zelda Wiki, Master Quest][src-zelda-wiki]

**Conseguenza:** `quest_mode` deve essere una dimensione del contesto semantico e delle route. Non basta specchiare la matrice di rendering: collisioni, camere, orientamenti, input direzionali, actor placement, cutscene, scene transition e sistemi di danno devono usare il comportamento OOT3D MQ.

### 8. Boss Challenge E Boss Gauntlet

Boss Challenge e una modalita esclusiva OOT3D confermata da Nintendo. Permette di riaffrontare boss gia sconfitti o affrontarli in sequenza. [Nintendo, Boss Challenge][src-nintendo-news]

La documentazione indipendente descrive:

- unlock dopo Forest Temple e dialogo con Sheik;
- accesso dal letto nella casa di Link;
- selezione limitata ai boss gia sconfitti;
- loadout e cuori specifici per boss;
- timer, conteggio vittorie e record;
- Boss Gauntlet, ricompense fra incontri e regole MQ differenti. [Zelda Wiki, Boss Challenge][src-boss-challenge]

I dettagli di livello B devono diventare casi di estrazione da `code.bin`, tabelle native e save. Il control plane N64 puo riusare le battaglie concettuali, ma non possiede menu, unlock, loadout, sequenza, record o completamento della modalita.

## Delta Di Mondo E Dungeon Da Verificare Nei Dati Nativi

La seguente lista e riportata da Zelda Wiki e va trattata come backlog di livello B, non come fonte di valori. [Zelda Wiki, changes and additions][src-zelda-wiki]

| Area | Delta riportato | Impatto | Evidenza nativa richiesta |
| --- | --- | --- | --- |
| Kokiri Forest | Kokiri visibili a distanza maggiore | Visibilita e percezione gameplay | Draw-distance/cull policy in attore o renderer OOT3D. |
| Castle Town alleys | Camera segue Link invece di restare fissa | Controllo e navigazione | Camera setting ZSI e state machine OOT3D. |
| Zora's River / Kakariko | Geometrie e pendenze modificate | Movimento, collisione, salti | CMB collisione e setup OOT3D. |
| Bottom of the Well | Boulder blocca l'entrata nel passato e scompare dopo il drenaggio | Progressione e world state | Actor list per setup, switch/event flag e actor params. |
| Graveyard | Muro piu alto impedisce uno skip verso Shadow Temple | Progressione emergente | Geometria/collisione OOT3D, nessuna patch ad hoc. |
| Forest Temple | Crystal Switch sostituito da Floor Switch | Puzzle e sequence break | Actor ID/params/setup nativi e ramo di progressione. |
| Forest Temple | Non si puo saltare dall'ascensore in movimento | Movimento/collisione attore dinamico | Dynamic collision e player state OOT3D. |
| Water/Spirit Temple | Switch dietro grate spostati perche OOT3D non consente di colpirli attraverso pareti | Projectile collision e puzzle | Geometria, actor placement e trace projectile OOT3D. |
| Water Temple | Segnaletica colorata e camere cambiate per mostrare i livelli dell'acqua | Navigazione e cutscene | Materiali/CMAB, camera data e trigger nativi. |
| Forest Temple | Nuova cutscene iniziale che mostra il chest sull'albero | Progressione/camera | QDB/timeline, trigger e flag OOT3D. |
| Royal Family's Tomb | Porta aggiunta all'inizio | Transizione/collisione | Setup e door actor OOT3D. |

Questi delta dimostrano perche il setup N64 non puo essere usato come popolazione concreta. La variante semantica N64 seleziona il momento di gameplay; ZSI, collisione, actor list, object list, params e codice OOT3D definiscono cio che esiste davvero.

## Delta Di Oggetti, Combattimento E Attori Da Verificare

Questi comportamenti sono riportati da documentazione indipendente e richiedono conferma da `code.bin` o test differenziali controllati. Nessuno va implementato come costante ad hoc. [Zelda Wiki, items and miscellaneous changes][src-zelda-wiki]

| Delta riportato | Livello | Dove cercare | Test di accettazione |
| --- | --- | --- | --- |
| Boomerang richiamabile immediatamente | B | Player item state, boomerang actor, input branch | Richiamo in piu fasi del volo e con target/ostacoli. |
| Hookshot usa laser e anello per target raggiungibile | B | Aim state, line trace, range test, HUD effect | Target valido/non valido a soglia di distanza e occlusione. |
| Jump Slash con Deku Stick infligge meno danno | B | Damage table e attack descriptor | Matrice arma/mossa/nemico confrontata con Azahar. |
| Hover Boots hanno animazione di corsa piu rapida senza maggiore velocita | B | Animation selection/rate separato dalla speed | Confronto distanza e phase animation nello stesso intervallo. |
| Biggoron's Sword non ruota leggermente Link nello swing standard | B | Player melee movement/turn impulse | Orientamento prima/dopo attacco su terreno piano. |
| Roll conserva piu controllo e termina meno bruscamente | B | Player roll state, deceleration e input steering | Traccia velocita/heading su sequenze input ripetibili. |
| Roll dopo una caduta ha condizioni piu restrittive | C | Landing/roll transition guards | Sweep di altezza, velocita e timing input. |
| Club Moblin ha cutscene di incontro, death animation e invincibility differente | B | Actor init/update/damage, CSAB e trigger | Primo incontro, hit cadence e morte. |
| Fairies vengono consumate una alla volta nelle fontane | B | Fairy pickup/health actor loop | Sovrapposizione simultanea con health differenti. |
| Bug del Deku Nut upgrade e altri exploit sono corretti selettivamente | A per la policy, B per il caso | Save flags, reward actor e conditionals | Matrice per revisione e stato quest. |
| Bugs dei Magic Bean Hole non sono ricatturabili | B | Insect actor/capture state | Spawn, rilascio e tentativo di ricattura. |
| Targeting viene percepito piu sensibile e puo selezionare dietro Link | C | Target candidate ranking | Sweep angolare e di distanza con scene controllate. |

## Delta Di Feedback E Presentazione Con Effetto Gameplay

| Sistema | Differenza | Classe | Nota di scope |
| --- | --- | --- | --- |
| Shard of Agony | Sostituisce il rumble con icona lampeggiante e tono | `oot3d_override` | Va preservata almeno la semantica di prossimita; feedback HUD e soggetto all'eccezione corrente. |
| Ocarina | Melodie visibili durante l'esecuzione e input fisico/touch rimappato | `n64_ui_scope_exception` | Il riconoscimento nativo delle note va comunque censito. |
| Item descriptions | Possono essere saltate rapidamente | `n64_ui_scope_exception` | Incide sul pacing ma appartiene al layer UI corrente. |
| Testo dialoghi | Scorre piu rapidamente e piu testo e skippabile | `oot3d_override` / UI | Verificare se il clock e nel message engine o nel frontend. |
| Trading quest | Descrizioni aggiungono indicazioni sulla destinazione | `oot3d_override` | I messaggi nativi possono contenere semantica assente in N64. |
| Footstep | Suoni cambiati per alcuni materiali | `oot3d_override` | Richiede mapping surface type OOT3D, non solo audio differente. |
| Camera dialoghi | Molti zoom e angoli sono cambiati | `oot3d_override` | Camera/timeline native possiedono l'inquadratura. |

Le fonti di questa sezione sono di livello B salvo dove gia confermate dal manuale o dalle interviste Nintendo. [Zelda Wiki, other changes][src-zelda-wiki]

## Sistemi Esclusivi E Priorita Di Decompilazione

### P0 - Bloccanti Per Il Playthrough Main Quest

1. **Clock e update domains:** determinare tick di gameplay, collisione, animazione, camera, particelle e timer senza assumere un unico 30 Hz.
2. **Player controller OOT3D:** accelerazione, velocita, turn policy, roll, salto, landing, nuoto, climb, damage e action transitions.
3. **Collisione OOT3D:** floor/wall/ceiling, dynamic collision, projectile trace, invulnerability e risoluzione contro attori.
4. **Attachment e attack geometry:** joint matrices, weapon tips, hitbox, hurtbox, mount sockets e punti effetto nativi.
5. **Varianti di scena:** progression state semantico -> setup/room/actor/object list/params OOT3D verificati.
6. **Camera:** setting nativi, trigger, dialoghi, first-person, targeting e comportamento durante il movimento.
7. **Actor behavior:** usare semantica N64 solo finche un delta OOT3D non e dimostrato; registrare provenance per attore/stato.
8. **Dungeon deltas:** switch, ostacoli, camere e collisioni che cambiano puzzle o progressione.

### P1 - Fedelta Di Gameplay OOT3D

1. Matrice danni e mosse OOT3D.
2. Boomerang, Hookshot, Boots, roll e altri item/player deltas.
3. Selective bug compatibility registry.
4. Shard of Agony e mapping dei surface feedback.
5. Pacing di dialoghi/cutscene e nuove cutscene OOT3D.
6. Save context capace di conservare tutti i campi nativi anche dietro UI N64.

### P2 - Sistemi Esclusivi Della Main Quest

1. Sheikah Stones come attori, trigger e punti di accesso.
2. Catalogo Visions, condizioni di unlock, stato visto/completato e timeline.
3. Navi break timer e suggerimenti verso le Sheikah Stones.

### P3 - Modalita Aggiuntive

1. Boss Challenge e Boss Gauntlet completi.
2. Master Quest come `quest_mode`, incluse mirror semantics, doppio danno, setup e save separati.
3. Regole Boss Challenge specifiche MQ.

La priorita P2/P3 non significa che questi sistemi possano essere esclusi dall'architettura. Catalogo, save schema e route context devono poterli rappresentare fin dall'inizio.

## Piano Di Verifica Operativo

### Tracce Deterministiche

Per movimento, collisioni e combattimento:

1. fissare save, entrance, setup, posizione e input script;
2. catturare da Azahar ogni tick gameplay, non soltanto screenshot;
3. registrare posizione, velocita, heading, action state, animation frame, floor/wall contact, collider result, health e timer;
4. riprodurre la stessa traccia nella demo;
5. confrontare eventi nel tempo reale e classificare il primo punto di divergenza.

Questo evita di compensare un errore di clock con velocita, turn rate o animation speed sbagliati.

### Matrice Scene/Varianti

Per ogni route semanticamente rilevante salvare:

- scena e ingresso concettuali N64;
- variante di progressione richiesta;
- global/local entrance OOT3D;
- setup e room risolti dai dati nativi;
- hash di actor/object list e collisione;
- camera setting e trigger;
- flag OOT3D letti per scegliere la variante;
- stato `verified`, `provisional` o `needs_oot3d_decompilation`.

### Matrice Comportamenti

Ogni differenza deve diventare una riga machine-readable con:

```text
semantic_key
domain
authority_class
oot3d_evidence
n64_scaffold_reference
native_asset_or_code_owner
verification_state
acceptance_test
```

Il documento e il registro umano; il catalogo generato deve impedire che una route `oot3d_override` o `oot3d_exclusive` venga dichiarata completa mentre usa ancora formule N64 non verificate.

## Criteri Di Chiusura

Una prerogativa OOT3D e chiusa soltanto quando:

1. il comportamento e associato a dati o codice originale OOT3D riproducibile;
2. il runtime non usa asset N64 ne valori catturati dall'emulatore;
3. il ruolo del sorgente N64 e esplicitamente semantico, oppure assente per i sistemi esclusivi;
4. esiste un test che distingue concretamente il comportamento OOT3D da quello N64;
5. save/load e transizioni di scena conservano lo stato;
6. la validazione Azahar conferma il risultato entro tolleranze dichiarate;
7. eventuali lacune sono marcate, non coperte da fallback silenziosi.

## Limiti E Punti Da Non Sovrainterpretare

- Il valore `30 FPS` non dimostra che ogni sottosistema aggiorni a 30 Hz. Va decompilato per dominio.
- `Remake from scratch` e l'uso del vecchio programma, entrambi descritti da Nintendo, non dicono quali funzioni siano state portate, riscritte o emulate concettualmente.
- Una differenza visiva puo avere conseguenze gameplay, come dimostra il caso torcia/ragnatela; va verificata prima di classificarla come cosmetica.
- Le liste comunitarie sono utili come suite di test, non come specifiche binarie.
- Il comportamento osservato in Azahar dimostra l'output di una build, non l'origine del valore. La provenance deve terminare in asset o codice OOT3D.
- La versione/regione OOT3D target deve essere fissata nei report: testo, asset, codice e possibili revisioni possono differire.

## Fonti

### Primarie Nintendo

- [Iwata Asks, Development Staff 1: A 13-Year Gap][src-iwata-dev-1]
- [Iwata Asks, Development Staff 2: The Idealized Borderline][src-iwata-dev-2]
- [Iwata Asks, Development Staff 3: Water Temple][src-iwata-dev-3]
- [Iwata Asks, Development Staff 4: gyro e camera][src-iwata-dev-4]
- [Iwata Asks, Development Staff 5: hints, save e Master Quest][src-iwata-dev-5]
- [Iwata Asks, Miyamoto: remake, gyro e Hint Movies][src-iwata-miyamoto]
- [Nintendo 3DS Electronic Manual][src-manual]
- [Nintendo UK: Master Quest e Boss Challenge][src-nintendo-news]

### Tecniche E Secondarie

- [Zelda Wiki: Ocarina of Time 3D changes and additions][src-zelda-wiki]
- [Zelda Wiki: Boss Challenge][src-boss-challenge]
- [CloudModding: struttura del save OOT3D][src-save]
- [ZeldaSpeedRuns: indice tecnico OOT3D][src-zsr]

[src-iwata-dev-1]: https://iwataasks.nintendo.com/interviews/3ds/zelda-ocarina-of-time/3/0/
[src-iwata-dev-2]: https://iwataasks.nintendo.com/interviews/3ds/zelda-ocarina-of-time/3/1/
[src-iwata-dev-3]: https://iwataasks.nintendo.com/interviews/3ds/zelda-ocarina-of-time/3/2/
[src-iwata-dev-4]: https://iwataasks.nintendo.com/interviews/3ds/zelda-ocarina-of-time/3/3/
[src-iwata-dev-5]: https://iwataasks.nintendo.com/interviews/3ds/zelda-ocarina-of-time/3/4/
[src-iwata-miyamoto]: https://iwataasks.nintendo.com/interviews/3ds/zelda-ocarina-of-time/4/5/
[src-manual]: https://www.nintendo.com/eu/media/downloads/games_8/emanuals/nintendo_3ds_2/the_legend_of_zelda__ocarina_of_time/ElectronicManual_Nintendo3DS_TheLegendOfZeldaOcarinaOfTime_EN.pdf
[src-nintendo-news]: https://www.nintendo.com/en-gb/News/2011/A-legend-returns-in-magical-3D-this-June-252755.html
[src-zelda-wiki]: https://zeldawiki.wiki/wiki/The_Legend_of_Zelda%3A_Ocarina_of_Time_3D
[src-boss-challenge]: https://zeldawiki.wiki/wiki/Boss_Challenge
[src-save]: https://cloudmodding.com/zelda/oot3dsave
[src-zsr]: https://www.zeldaspeedruns.com/oot3d
