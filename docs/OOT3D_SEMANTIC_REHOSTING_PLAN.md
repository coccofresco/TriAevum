# Piano Funzionale Di Semantic Rehosting OOT3D Su Ship

## Obiettivo

Usare il gameplay ricompilato di Ocarina of Time N64 come impalcatura di controllo e mappa concettuale del gioco, sostituendo progressivamente ogni contenuto osservabile con il corrispondente contenuto nativo OOT3D. Il risultato perseguito e un playthrough completo di OOT3D dall'inizio alla fine; HUD, menu e logiche direttamente collegate restano temporaneamente quelli Ship/N64.

Il gameplay host deve poter esprimere richieste come "entra nella Foresta Kokiri", "avvia la cutscene di apertura", "Link corre" o "la cutscene e terminata". Un layer semantico risolve queste richieste in scene, setup, room, attori, animazioni, camere, materiali, ambiente e audio OOT3D. Dopo la risoluzione, tempi e resa appartengono a OOT3D.

La sostituzione semantica si applica a ogni confine del gameplay, non soltanto alle cutscene. Per esempio, l'ingresso N64 "Foresta Kokiri dall'uscita della casa di Link" identifica l'intento; la route deve risolvere la voce d'ingresso globale OOT3D e lasciare che lo ZSI OOT3D fornisca spawn, rotazione, parametri del player e selezione camera. Coordinate, indici camera, pose e animazioni N64 non sono valori di fallback e non vengono trasferiti alla scena nativa.

La stessa regola vale per le versioni temporali e di progressione del mondo. Il sorgente N64 descrive semanticamente quale fase e attiva, quali ruoli devono esistere, quali condizioni li abilitano e quale conseguenza gameplay producono. Questa descrizione seleziona la variante omologa OOT3D; setup, actor/object list, parametri, trasformazioni, asset e animazioni concrete vengono poi letti dai dati OOT3D. Un indice setup N64 non viene mai riutilizzato come indice OOT3D per semplice coincidenza.

## Regola Di Autorita

| Dominio | Autorita | Uso dell'altra versione |
| --- | --- | --- |
| Progressione, cause degli eventi, conseguenze e stato globale | Gameplay Ship/N64 | OOT3D puo sostituire il comportamento quando `code.bin` dimostra un delta |
| Identita concettuale di scena, attore, azione e cutscene | Bridge semantico verificato | Gli ID numerici non sono assunti equivalenti senza evidenza |
| Variante di progressione, popolazione attesa e ruolo funzionale | Semantica ricostruita dal sorgente N64 | Seleziona una variante OOT3D verificata, ma non ne fornisce setup, actor list, params o trasformazioni |
| Setup, room, popolazione concreta e configurazione attori | ZSI e codice OOT3D | N64 descrive il significato della variante, non i valori runtime nativi |
| Geometria, texture, skeleton, animazioni, materiali e visibilita | Asset OOT3D | Nessuna sostituzione runtime di asset N64 |
| Durata e timeline di cutscene e animazioni | Dati e codice OOT3D | N64 indica solo l'intento dell'evento |
| Camera, illuminazione, fog, cielo, particelle e post-process | Dati OOT3D e comportamento decompilato da `code.bin` | N64 e solo un riferimento strategico quando il significato e ambiguo |
| Rendering | Semantica PICA OOT3D implementata nel backend OpenGL | Fast3D/N64 resta il renderer di fallback esplicito |
| HUD, menu e relative logiche interattive | Ship/N64, per la fase corrente | Non vengono instradati verso asset OOT3D |
| Validazione | Azahar e dump mirati | I dump non diventano mai input runtime o valori da copiare nel gioco |

In caso di conflitto, l'evidenza OOT3D vince sempre nel dominio del contenuto. La struttura N64 non puo imporre frame count, trasformazioni, camera, animazioni o resa a un contenuto OOT3D.

Quando asset e strutture gia decodificate non bastano a distinguere una variante o un comportamento, la logica N64 puo sostenere provvisoriamente la semantica comune, con provenance esplicita. Il gap viene chiuso decompilando `code.bin` o ulteriori formati OOT3D; un valore N64 non puo essere promosso a comportamento OOT3D soltanto perche produce un risultato plausibile.

HUD e menu interattivi sono un'eccezione di scope esplicita: restano interamente N64, incluse le logiche collegate, finche questo piano non viene deliberatamente esteso. Il logo e gli overlay temporali appartenenti a una cutscene o alla title intro non sono HUD/menu e seguono invece l'autorita OOT3D della relativa timeline.

## Confini Architetturali

```text
Sorgenti e tabelle OOT N64 (offline)
    -> OfflineSemanticSource: intenti, trigger, condizioni e varianti
    -> compilatore verificato + binding e indici nativi OOT3D
    -> SemanticRouteCatalog impacchettato

Gameplay Ship/N64 (runtime)
    -> GameplayBridge: richiesta semantica e contesto
    -> SemanticRouteCatalog: associazione verificata
    -> Oot3dContentIndex: identita native e dipendenze
    -> provider OOT3D tipizzati
    -> runtime OOT3D: timeline, stato e completamento
    -> renderer OpenGL con semantica PICA
    -> CompletionBridge: evento semantico verso il gameplay host
```

### OfflineSemanticSource

La ricostruzione della semantica N64 avviene offline e viene versionata in
`tools/oot3d/oot3d_asset_tool/profiles/semantic_gameplay_source.json`.
Contiene identita concettuali, trigger e condizioni espresse come fatti stabili; non contiene target asset o valori concreti OOT3D. Il compilatore la unisce ai binding OOT3D, alla tabella entrance estratta da `code.bin` e all'asset catalog. Solo il route catalog generato entra nel core O2R.

Un indice setup N64 puo essere evidenza usata durante la ricostruzione offline, ma non e una chiave nativa e non viene passato al provider OOT3D. Il runtime espone fatti come eta, fase giorno/notte e tipo di layer; eventuali nuovi fatti devono descrivere semantica di gameplay e non indirizzi, params o numeri di setup.

La specifica operativa e in `docs/OOT3D_OFFLINE_SEMANTIC_SOURCE.md`.

### GameplayBridge

Produce richieste prive di percorsi asset:

- scena concettuale, origine dell'ingresso, variante semantica e stato di progressione;
- ruoli attore richiesti, condizioni di presenza e funzionalita gameplay attesa;
- cutscene concettuale e causa di avvio;
- attore concettuale, azione gameplay e parametri;
- evento conclusivo atteso.

Non carica ZSI, CMB, CSAB o risorse N64 e non decide indici setup, actor params, trasformazioni, frame, clip o materiali OOT3D.

### SemanticRouteCatalog

E un artefatto generato e versionato, distinto dall'asset catalog. Collega una richiesta del control plane a una identita nativa OOT3D e dichiara esplicitamente chi possiede timing, rendering e completamento.

Le associazioni devono provenire da almeno una evidenza riproducibile:

- tabella o riferimento in `code.bin`;
- comando o indice ZSI/ZAR OOT3D;
- corrispondenza semantica verificata con i sorgenti N64;
- comportamento osservato in Azahar usato solo come conferma.

### Oot3dContentIndex

Resta l'inventario di asset nativi. Contiene identita di container e member, risorse canoniche derivate, dipendenze, ownership e stato di supporto. Non contiene decisioni di gameplay.

### Oot3dRuntime

Dopo la risoluzione della route possiede:

- timeline e clock nativi;
- selezione di setup, room e actor list;
- CSAB, CMAB, ANB/FACEB e visibility state;
- camera e ambiente temporale;
- emissione dell'evento di completamento.

### CompletionBridge

Converte la conclusione nativa in un evento semantico stabile, per esempio `cutscene_finished`, `scene_exit_requested` o `actor_action_finished`. Solo a quel punto il gameplay host riprende la progressione.

## Contratto Delle Route

Il formato iniziale e `oot3d_semantic_route_catalog_v1`. Ogni record contiene:

```json
{
  "route_id": "scene:SCENE_KOKIRI_FOREST",
  "kind": "scene",
  "scaffold": {
    "system": "oot_n64_gameplay",
    "request_kind": "scene",
    "semantic_key": "SCENE_KOKIRI_FOREST",
    "scene_id": 85,
    "entrance_index": 529,
    "variant_key": "KOKIRI_FOREST_INITIAL_CHILD_DAY"
  },
  "native": {
    "system": "oot3d",
    "asset_id": "scene:spot04_info.zsi",
    "scene_id": 85,
    "scene_path": "spot04_info.zsi",
    "setup_indices": [0],
    "room_bindings": [
      { "scaffold_room_index": 0, "native_room_index": 0 },
      { "scaffold_room_index": 1, "native_room_index": 1 },
      { "scaffold_room_index": 2, "native_room_index": 2 }
    ],
    "population_source": "oot3d_zsi_actor_and_object_lists",
    "actor_configuration_source": "oot3d_zsi_actor_params"
  },
  "authority": {
    "control_flow": "oot_n64_gameplay_scaffold",
    "content": "oot3d_native",
    "timing": "oot3d_native",
    "rendering": "oot3d_native"
  },
  "completion_event": "scene_exit_requested",
  "status": "resolved"
}
```

Vincoli obbligatori:

- `native.asset_id` deve esistere nell'asset catalog montato;
- una richiesta semantica non puo risolversi ambiguamente;
- gli ID scaffold e nativi restano campi distinti anche quando coincidono;
- ogni room Ship raggiungibile da una route attiva deve avere un binding esplicito verso una room presente nell'asset catalog OOT3D;
- la chiave di variante scaffold deriva da condizioni e ruoli del gameplay N64, mentre il setup target deve essere verificato nei dati OOT3D;
- popolazione e configurazione di una route attiva devono provenire dalle actor/object list e dai params OOT3D;
- una route non puo contenere un percorso OTR N64 come target;
- ogni fallback deve essere esterno alla route, esplicito e telemetrato;
- `timing` e `rendering` devono essere OOT3D per una route attiva;
- route incomplete possono essere indicizzate ma non attivate.

## Flussi Runtime

### Ingresso In Scena

1. Il compilatore offline traduce sorgenti e tabelle N64 in intenti, trigger e condizioni di variante stabili.
2. Ship emette al runtime il trigger corrente e i fatti di gameplay; il catalogo generato seleziona una sola variante semantica senza usare il setup N64 come identita OOT3D.
3. Il catalogo risolve ingresso e variante verso una voce globale e un setup OOT3D verificati, senza assumere uguaglianze numeriche.
4. La voce globale OOT3D determina scena e ingresso locale; la variante risolta determina quale setup OOT3D leggere.
5. Lo ZSI OOT3D fornisce room, spawn, trasformazione e parametri iniziali del player, actor/object list, ambiente e collisioni. Presenza, posizione e configurazione concreta degli attori non vengono ricostruite dalle liste N64.
6. Il parametro nativo del player seleziona la camera iniziale OOT3D o il relativo percorso floor/default; posa, entry action e animazioni vengono risolte dal runtime attore OOT3D.
7. I provider caricano solo identita OOT3D o cache direttamente derivate e il renderer usa materiali e stato PICA OOT3D.
8. Le uscite native emettono una nuova richiesta semantica verso Ship, che continua a possedere la progressione.

### Varianti Di Mondo E Popolazione

1. Le condizioni N64 vengono tradotte in una chiave stabile, per esempio fase iniziale child/day, fase successiva o stato post-evento.
2. La route associa quella chiave a uno o piu setup OOT3D soltanto quando ZSI, tabelle native o decompilazione ne dimostrano il ruolo.
3. Le liste OOT3D decidono quali attori e oggetti esistono, dove si trovano e con quali params; i provider attore risolvono modelli, materiali, skeleton e animazioni native.
4. La logica N64 fornisce il ruolo e il flusso funzionale quando il comportamento e condiviso. Ogni delta OOT3D dimostrato sostituisce il tratto corrispondente.
5. Se manca una corrispondenza affidabile, la variante o funzionalita resta marcata `needs_oot3d_decompilation` e non riceve valori inventati o copiati dalla versione N64.

### Cutscene

1. Il gameplay host richiede una cutscene per significato, non per puntatore o timeline N64.
2. La route seleziona sorgente, setup e timeline OOT3D.
3. Il player OOT3D possiede durata, camere, attori, animazioni, luci, cielo, logo, effetti e audio.
4. Il gameplay N64 rimane sospeso per gli aspetti posseduti dalla cutscene.
5. Al termine nativo viene emesso `cutscene_finished` con la route conclusa.
6. Ship applica la conseguenza di gameplay prevista dalla propria impalcatura.

### Azioni Degli Attori

1. La logica Ship richiede un'azione semantica, per esempio `locomotion.run`.
2. Il profilo personaggio OOT3D risolve modello, variante, equipaggiamento e CSAB.
3. Il CSAB determina durata, loop, posa e transizione; i dati OOT3D determinano visibilita e materiali.
4. Un'azione one-shot emette `actor_action_finished`; un loop continua finche il gameplay cambia richiesta.

## Strategia Di Migrazione

### Fase 1: Contratto E Scene

- generare route per tutte le scene native risolte;
- caricare il catalogo dal core O2R;
- far passare il provider scena attraverso una route semantica;
- telemetrare route attiva, target nativo e motivo del fallback.

### Fase 2: Cutscene Generiche

- collegare gli indici cutscene ZSI alle richieste semantiche;
- introdurre un player timeline comune;
- usare la title intro come primo caso completo, senza API dedicate alla title intro;
- terminare la timeline tramite `CompletionBridge`.

### Fase 3: Profili Attore E Azioni

- generare profili per actor/object/params;
- risolvere modelli, equipaggiamento, skeleton e animazioni native;
- sostituire mapping route-local e nomi Kokiri con lookup indicizzati.

### Fase 4: Ambiente E Audio

- collegare Kankyo, luci, fog, sky, effetti e audio ai profili scena/cutscene;
- mantenere OpenGL come unico backend in scope;
- mantenere HUD, menu e logiche collegate sul percorso Ship/N64;
- trattare title logo e overlay di cutscene come contenuti OOT3D della timeline, non come UI interattiva.

### Fase 5: Riduzione Dell'Impalcatura

- decompilare i delta di gameplay OOT3D da `code.bin`;
- sostituire per sottosistema solo i comportamenti dimostrati diversi;
- mantenere la logica N64 residua come control plane, non come fonte visuale.

## Verifica E Completezza

Ogni incremento deve avere:

- test strutturale del parser e degli indici;
- rigenerazione deterministica del catalogo;
- controllo che tutti i target nativi esistano nell'asset catalog;
- smoke test runtime di una route reale;
- telemetria su fallback e contenuti mancanti;
- confronto Azahar solo ai checkpoint in cui puo distinguere tra implementazioni plausibili.

La completezza si misura per route, non per screenshot. Una route e completa quando tutti i sottosistemi richiesti sono nativi OOT3D e il gameplay puo entrare, eseguirla e uscirne senza dipendere da asset N64 nascosti.

## Primo Incremento Implementativo

1. Generare il catalogo semantico delle scene usando `scene_source_table.json` e `oot3d_asset_catalog.json`.
2. Aggiungere al fork engine un parser indicizzato con validazione delle ownership.
3. Includere il catalogo in `oot3d-core.o2r`.
4. Caricarlo insieme all'asset catalog e risolvere le scene tramite il bridge.
5. Estendere lo stesso schema alle cutscene indicizzate, senza introdurre codice specifico per la title intro.

## Stato Implementativo

Completato:

- sorgente semantica offline separata dai binding nativi, con compilazione di condizioni in route runtime;
- matcher engine per fatti semantici, risoluzione non ambigua e provider scene-entry nativo;
- primo handoff Ship verso lo spawn ZSI OOT3D per l'ingresso Foresta Kokiri dalla casa di Link;
- binding espliciti delle room e contesto runtime persistente per scena, setup e room native, usato da renderer, luci, fog e attori;
- handoff dei parametri player OOT3D al bootstrap camera, con selezione nei record camera decodificati dalla collisione nativa;
- catalogo semantico di 102 scene native, con ownership verificata contro l'asset catalog;
- inventario e pacchetto di 126 stream QDB OOT3D estratti senza conversione del payload;
- parser engine tipizzato e validato per header, comandi, terminatore e allineamento QDB;
- route `CUTSCENE_OPENING_TITLE` dal trigger Ship/N64 alla sequenza nativa
  `spot00_demo_epona_00/01/02.qdb`, con setup e durate provenienti dai dati OOT3D;
- provider generico che prepara una sequenza QDB solo quando ogni risorsa nativa richiesta e disponibile;
- contratto di completamento semantico `cutscene_finished`;
- policy esplicita che mantiene HUD, menu e relative logiche sul percorso N64.

In corso:

- dispatcher comune dei comandi QDB verso camera, attori, ambiente ed eventi runtime;
- clock e stato di esecuzione posseduti dalla sequenza OOT3D;
- sospensione della timeline N64 soltanto dopo che il player nativo ha raggiunto lo stato `running`;
- emissione del completamento verso il gameplay host e prosecuzione della progressione Ship.

Il preflight presente nell'opening non costituisce ancora il playback nativo: risolve e carica la route, ma non sostituisce la timeline N64 finche il dispatcher comune non e operativo. Questa distinzione evita sia fallback visuali impliciti sia una schermata priva di contenuto.

## Comandi Operativi

Indicizzare e impacchettare prima gli stream QDB nativi:

```powershell
.\scripts\oot3d\Invoke-Oot3dQdbCatalog.ps1 -Verify
```

Rigenerare l'asset catalog, che incorpora le identita QDB:

```powershell
.\scripts\oot3d\Invoke-Oot3dAssetCatalog.ps1 -Verify
```

Rigenerare il catalogo semantico dopo il catalogo asset o dopo una modifica alle tabelle di binding:

```powershell
.\scripts\oot3d\Invoke-Oot3dSemanticRouteCatalog.ps1 -Verify
```

Rigenerare quindi il core pack che incorpora entrambi i cataloghi:

```powershell
.\scripts\oot3d\Invoke-Oot3dPlayablePack.ps1 -Verify
```

Gli output controllabili sono:

```text
I:\oot3dre_work\playable_catalog\oot3d_semantic_route_catalog.json
I:\oot3dre_work\playable_catalog\oot3d_semantic_route_catalog.md
I:\oot3dre_work\qdb_catalog\oot3d_qdb_catalog.json
I:\oot3dre_work\qdb_catalog\oot3d-cutscenes.o2r
I:\oot3dre_work\playable_pack\oot3d-core.o2r
```
