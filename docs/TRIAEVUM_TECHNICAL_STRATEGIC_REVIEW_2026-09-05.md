# TriAevum: revisione tecnica e strategica

Data: 5 settembre 2026. Stato: **analisi e piano di correzione, non attestazione di release pronta**.

### Aggiornamento del modello di distribuzione

Il maintainer ha adottato il modello delle recomp Xbox 360: compilazione del
titolo a cura del publisher, Forge limitato all'importazione della ROM utente.
Il commit `1d4e95cc9` implementa questo cambio senza modificare il runtime.
La prova frozen ROM-only della candidate 0.6.0-r1 ha richiesto **20,905 secondi**
complessivi (17,016 interni), zero compilazione/SDK/cache, seguita da avvio reale
dell'intro verificato dal framebuffer. Le prescrizioni del rapporto relative
alla compilazione sul PC utente sono quindi superate, non semplicemente
ottimizzate. Contratto, prove e limiti in
[TRIAEVUM_PRECOMPILED_RELEASE.md](TRIAEVUM_PRECOMPILED_RELEASE.md).

## 1. Risultato della revisione

Il progetto contiene un runtime giocabile e un renderer NRI sostanzialmente piu'
avanzati del percorso inizialmente distribuito da Forge. Il problema principale
non e' l'assenza generalizzata delle funzionalita': e' la mancanza di un unico
percorso riproducibile che consegni quelle funzionalita' all'utente.

La regressione segnalata dopo Forge, gioco lento, niente TopScreen e niente F1,
e' coerente con questa divergenza. Il commit `7e5a14a89` introduce il collegamento
del runtime maturo al plugin whole-AOT v2, ma **la selezione dell'eseguibile nel
build/package, la configurazione effettiva e le prove di rilascio non sono ancora
allineate a quella decisione**.

Non consiglio una nuova riscrittura, un nuovo renderer o un nuovo tentativo di
boot dalla decompilazione. Consiglio di chiudere verticalmente il prodotto gia'
esistente: stessa implementazione in sviluppo e in release, separazione netta
fra runtime pubblico e codice del titolo generato localmente, installazione
verificabile e prestazioni misurate sul percorso realmente distribuito.

Le prime correzioni sotto non richiedono nuova decompilazione.

## 2. Perimetro ed evidenze

### Identita' dei percorsi

| Percorso | Identita' osservata | Significato per questa revisione |
| --- | --- | --- |
| `I:\oot3dre` | `oot3d-port`, `36c74ec2eae38973448c5cf5de29a3013f47d949` | Worktree storico fornito come directory iniziale; non contiene il prodotto TriAevum piu' recente. Submodule locale modificato, non toccato. |
| `I:\oot3dre_work\triaevum-release` | `release/triaevum-forge`, `7e5a14a8964e05b8df6e6a40a5ed3926d2d4b1ec` | Sorgenti principali revisionati. Worktree pulito prima del rapporto. |
| `I:\oot3dre_work\whole-aot-product` | `mod/topscreen-2.1.1`, `51139e5eb074f62be3124ae1c28e362e85b0504a` | Linea di sviluppo da non confondere con il pacchetto Forge. |
| `I:\oot3dre_work\oot3d-native-renderer-integration` | `renderer/oot3d-native-integration`, `530a6450a2871dde362c3fb3c4ae4e9bb47e6520` | Altro worktree storico, non automaticamente la build corrente. |
| `I:\oot3dre_work\triaevum-direct-module-build` | CMake Release, clang-cl LLVM 22.1.6, plugin diretto attivo | Directory di build locale; contiene sia il vecchio `TriAevum.exe` sia il maturo `oot3d_native_game.exe`. |
| `I:\TriAevum-0.5.0-public-final` | Installazione modificata dopo il packaging | Contiene anche dati privati generati da Forge. Non e' piu' un pacchetto pubblico immacolato. |

Le righe di codice citate si riferiscono al commit `7e5a14a89`, non a eventuali
modifiche successive. I collegamenti relativi puntano a quel worktree.

### Verifiche effettuate

- Lettura di storia Git recente, target CMake, cache della build locale,
  generazione/link/installazione Forge, ABI, profili, ciclo di presentazione,
  architettura NRI, shader cache, test e workflow di release.
- Esecuzione della suite Python di release: **58 test superati in 8,616 secondi**.
- `readiness.py`: **35 complete, 0 blocked, 0 invalid**. Il significato limitato
  di questo risultato e' discusso in R05.
- `forge.py doctor` dal checkout: ritorna `status=ok` anche quando tutti i tool
  locali che elenca risultano assenti. Non significa che manchino nel pacchetto:
  dimostra che `ok` non certifica la capacita' di compilare/installare.
- Verifica diretta degli hash di runtime, Forge e archivio sorgenti installati.
- Lettura del benchmark esistente
  `I:\TriAevum-0.5.0-public-final\data\forge-abi-v2-final-benchmark.json`.

Non sono stati effettuati nuovi avvii del gioco, benchmark GPU, rebuild completi,
test su Windows pulito o riallineamenti della decompilazione. Le prove storiche
sono indicate come tali: non vengono spacciate per nuove verifiche end-to-end.

### Scala di priorita'

- **P1**: puo' consegnare il prodotto sbagliato, bloccare l'installazione o far
  dichiarare funzionante una combinazione non verificata.
- **P2**: rischio concreto di regressioni, build costose o manutenzione fragile.
- **P3**: consolidamento successivo, senza bloccare la prima release affidabile.

Non viene attribuito P0 in assenza di una criticita' di quel livello dimostrata.

## 3. Correzioni prioritarie

| ID | Priorita' | Problema | Evidenza |
| --- | --- | --- | --- |
| R01 | P1 | Il nome pubblico `TriAevum.exe` identifica ancora il vecchio host nel build | Confermato nel CMake e nel layout |
| R02 | P1 | Toolchain Forge v2 non dimostrata autosufficiente | Dipendenze implicite confermate; macchina pulita non provata |
| R03 | P1 | Profilo richiesto a 60, impostazione effettiva Original30 | Confermato in sorgenti e benchmark |
| R04 | P1 | Pacchetto riparato manualmente, manifesto/sorgenti rimasti indietro | Hash e commit verificati |
| R05 | P1 | Readiness e CI non qualificano il nuovo percorso giocabile | Confermato nel codice dei controlli |
| R06 | P1 | Installazione del plugin non transazionale; ABI poco identificabile | Ordine delle scritture e struttura ABI confermati |
| R07 | P2 | Chiavi cache incomplete rispetto agli input effettivi del toolchain | Confermato nel builder |
| R08 | P2 | Parallelismo senza budget RAM e lock non recuperabili dopo arresto | Confermato nel codice; impatto prestazionale da misurare |
| R09 | P2 | Profilo globale e percorsi assoluti indeboliscono portabilita'/selezione | Confermato; trasferimento installazione non provato |
| R10 | P2 | Confini NRI validi, ma integrazione e prove troppo concentrate | Rischio architetturale, non nuovo difetto visivo dimostrato |

### R01. Un solo eseguibile pubblico, derivato dal runtime maturo

**Evidenza.** [CMakeLists.txt](../CMakeLists.txt), righe 2173 e 2204, assegna
`OUTPUT_NAME TriAevum` a `triaevum_oot3d_module_host`. Il runtime maturo e'
`oot3d_native_game`, definito alla riga 3165 e collegato al plugin alle righe
3238-3275. Non ha lo stesso nome di output.
[runtime_release_layout.example.json](../tools/triaevum_release/runtime_release_layout.example.json),
riga 4, prende `build-release/TriAevum.exe`.

Quindi ricostruire seguendo il nome pubblico puo' riproporre l'host precedente,
anche se la cartella usata per la prova e' stata corretta copiandoci l'altro exe.
Non e' necessario ipotizzare che F1 e TopScreen siano stati rimossi dal renderer.
Anche [TRIAEVUM_PUBLIC_RUNTIME_VERTICAL.md](TRIAEVUM_PUBLIC_RUNTIME_VERTICAL.md),
righe 5-6, continua a indicare quel vecchio target come eseguibile pubblico.

**Metodo.** Introdurre un unico target/install manifest di prodotto che selezioni
esplicitamente il runtime maturo in modalita' plugin. Dare al vecchio host un
nome diagnostico inequivocabile ed escluderlo dal layout pubblico normale.
Generare l'elenco degli artefatti dal build, senza ricercare un file omonimo in
directory condivise e senza fallback silenzioso.

**Accettazione.** Da una directory di build vuota si produce un pacchetto in cui
l'hash dell'exe coincide con il target qualificato. Il binario espone una
capability query senza titolo: build ID, backend, F1, TopScreen, ABI supportate.
Il test del pacchetto rifiuta intenzionalmente il vecchio host. L'utente prova
poi quel pacchetto, non una copia corretta a mano.

### R02. Chiudere realmente l'ambiente C++/Windows di Forge

**Evidenza.** [whole_aot_plugin_backend.py](../tools/triaevum_release/whole_aot_plugin_backend.py),
righe 47-54 e 286-325, seleziona `x86_64-pc-windows-msvc`, C++20, `/MT` e linker
LLD. Il layout pubblico include LLVM, builtins e support library, ma non
descrive un sysroot completo/versionato di header e librerie C++/Windows.
Il builder eredita l'ambiente del processo; il messaggio d'errore alle righe
89-94 ammette la dipendenza da un ambiente compatibile Visual C++.

[TRIAEVUM_FORGE_WHOLE_AOT_V2.md](TRIAEVUM_FORGE_WHOLE_AOT_V2.md) lascia
esplicitamente aperta la prova su macchina pulita. Questo contraddice le
affermazioni di chiusura del vecchio workflow, non la validita' del pilot locale.

**Metodo.** Definire un bundle toolchain con identita' esplicita: compilatore,
linker, archiver, header, runtime, librerie e support library. Verificare la
provenienza/licenza di ogni componente prima della distribuzione. Nessuna
dipendenza implicita dall'installazione di sviluppo deve essere necessaria.
Prima di estrarre/generare tutto il titolo, `doctor` deve compilare, collegare
ed eseguire un piccolo probe dello stesso ABI: STL usata, TLS, floating point,
callback e allocazione. Eseguirlo in un processo separato, con diagnostica.

**Accettazione.** Forge funziona in Windows senza Visual Studio/SDK preinstallati
e senza accesso ai percorsi di sviluppo. Rimuovere solo `INCLUDE`/`LIB` non basta
a dimostrarlo: clang puo' individuare installazioni di sistema. Registrare una
prova in ambiente realmente pulito. Se manca un prerequisito, errore immediato
prima del lavoro costoso; mai ritorno automatico al backend ABI v1 lento.

### R03. Risolvere la configurazione effettiva, non solo gli argomenti

**Evidenza.** [forge.py](../tools/triaevum_release/forge.py), righe 896-940,
crea il JSON TopScreen e un profilo con `native30_interpolated` e
`--presentation-rate 60`, ma non inizializza `TriAevum.json`.
[graphics_settings.h](../runtime/three_ds_recomp/include/fast/oot3d/graphics_settings.h),
righe 148 e 163, parte da `Authentic` e `Original30`.
[graphics_settings.cpp](../runtime/three_ds_recomp/src/fast/oot3d/graphics_settings.cpp),
righe 620-627, riporta Authentic a Original30.
[oot3d_native_a32_window.cpp](../tools/oot3d/native_game_runtime/oot3d_native_a32_window.cpp),
righe 4331-4362, ricava il modo visivo dalle impostazioni effettive. Il permesso
di interpolare non equivale ad aver attivato l'interpolazione.

Il benchmark installato conferma `visual_interpolation_allowed=true` ma
`visual_interpolation_active=false`, moltiplicatore 1 e zero draw interpolati.
Questo non dimostra che x2/x3 siano rotti: dimostra che quella prova non li usa.

**Metodo.** Un resolver unico con precedenza documentata fra default, profilo
persistito, CLI e F1. Mostrare valore richiesto, valore effettivo ed eventuale
motivo di disattivazione. Creare al primo avvio un profilo esplicito per il
prodotto: NRI, TopScreen e scelta consapevole x1/x2. Se si vuole x2, non usare
un preset che lo vieta: per esempio Custom con effetti opzionali disattivi e
Interpolated2x. Conservare Authentic come riferimento nativo a 30.

Non sovrascrivere preferenze utente esistenti. Applicare migrazioni versionate
solo ai default assenti o esplicitamente appartenenti alla vecchia versione.

**Accettazione.** Avvio senza config, avvio con config utente, modifica F1 e
riavvio producono lo stesso contratto effettivo atteso. Per x2/x3 servono campioni
intermedi realmente diversi, non un contatore di Present piu' alto; vedere
sezione 5. Input, audio e velocita' di simulazione devono restare nativi.

### R04. Separare installazione privata e artefatto di rilascio

**Evidenza.** Il manifesto di `I:\TriAevum-0.5.0-public-final` dichiara il commit
`0ed044b8fbe12b413f4c22aac5c0ca1a438c05cf`, precedente alla correzione v2.
Gli hash SHA-256 verificati sono:

| File | Manifesto | File installato |
| --- | --- | --- |
| `TriAevum.exe` | `68f99315723a2ac7dc8d40479eeab02554e30922c5bdae3126d786671bf9e500` | `aaea869d00a85e76a27912e3a92b2f686ce44b0e3b84c9a28e0240bcf4cf821f` |
| `TriAevumForge.exe` | `75bb6e5df36b1440d4641e44433195a87a691280bf498c24c0d921b4d2a6f5b3` | `212c3c4334711720bfac4d41698a30c5502819bb4ad4fb6002583ee700c0bca1` |
| `source/TriAevum-source.zip` | `dc9507db6539171ac1fc51d1ea4ebd8a0b96b26ccbcb187515b1edec789a13ae` | Uguale al manifesto precedente |

E' normale che Forge produca dati privati nell'installazione. Non e' invece
corretto usare questa installazione modificata come prova che l'archivio
pubblico precedente contenga i nuovi binari e i loro sorgenti corrispondenti.

**Metodo.** Creare sempre il release staging da zero, tramite allowlist e receipt
dei target; rigenerare manifesto, documenti e source archive dallo stesso stato
qualificato. Testare una copia estratta dell'archivio finale. Tenere runtime
pubblico immutabile e titolo privato in sottodirectory distinta. Lo stub pubblico
e la DLL privata oggi hanno lo stesso nome: verificare la natura dello stub dal
target/provenienza, non dal solo nome del file.

**Accettazione.** Hash coerenti per tutti i file pubblici; il source archive
identifica gli stessi sorgenti effettivi dei binari. Il pacchetto vuoto passa
l'audit; la directory di un utente che ha gia' importato il gioco non viene
accidentalmente ricompattata come release. Le attribuzioni restano legate ai
componenti realmente inclusi. Nessuna nuova valutazione legale e' sostituita
da questo controllo tecnico.

### R05. Le prove devono qualificare il prodotto corrente

**Evidenza.** [readiness.py](../tools/triaevum_release/readiness.py), righe 37-77,
considera complete le voci con `status=complete`; non verifica i documenti
citati, il commit o gli artefatti. [release_readiness.json](../tools/triaevum_release/release_readiness.json)
chiude anche percorsi storici differenti dal v2 corrente.

[triaevum-release-policy.yml](../.github/workflows/triaevum-release-policy.yml)
non si attiva per modifiche ai principali sorgenti runtime/renderer o al CMake
radice. Esegue test Python e del modulo isolato su Linux, non il runtime maturo
Windows. Ignora l'exit code della readiness e si aspetta che il packaging fallisca:
quel fallimento puo' dipendere semplicemente dall'assenza dei file placeholder,
senza dimostrare la politica che il nome del test dichiara.
In [test_whole_aot_plugin_backend.py](../tools/triaevum_release/test_whole_aot_plugin_backend.py)
generazione, archivio e invocazione del compilatore sono sostituiti da mock:
il test e' utile per la logica del builder, non prova un vero compile/link v2.

**Metodo.** Separare tre risultati: policy del pacchetto, correttezza dei moduli,
accettazione del prodotto. Conservare i test veloci esistenti. Aggiungere un job
Windows per il target reale e l'installazione senza dati di gioco; eseguire le
prove con ROM privata soltanto nel laboratorio locale autorizzato. I receipt
devono contenere hash del binario, plugin, toolchain, profilo, input e scenario.
Una prova di packaging negativo deve asserire la causa attesa; serve anche una
prova positiva con fixture lecite. Estendere i trigger ai sorgenti coinvolti.

**Accettazione.** Un host sbagliato, toolchain mancante, profilo incoerente o
receipt di un vecchio commit non produce `ready`. Non moltiplicare checklist:
il controllo deve consumare automaticamente l'esito delle prove effettive.

### R06. Installazione transazionale e identita' dell'ABI

**Evidenza.** [forge.py](../tools/triaevum_release/forge.py), righe 883-950,
sostituisce prima la DLL attiva, poi legge/scrive configurazione, profilo e
stato. Se una fase successiva fallisce, la singola copia atomica non protegge
la coerenza dell'intera installazione. [forge_gui.py](../tools/triaevum_release/forge_gui.py),
righe 111-136, riconosce il titolo attivo tramite stato/hash del modulo, non
qualificando congiuntamente runtime, plugin e profilo effettivamente avviati.

[triaevum_title_whole_aot_abi.h](../tools/oot3d/native_game_runtime/triaevum_title_whole_aot_abi.h),
righe 13-43, esporta v2 con puntatori a funzioni che scambiano anche tipi C++
interni per riferimento. [oot3d_native_direct_aot.cpp](../tools/oot3d/native_game_runtime/oot3d_native_direct_aot.cpp),
righe 18-40, controlla versione, dimensione, callback ed entry ordinate, non
l'identita' dei layout transitivi o del titolo. Una struttura esterna di 48 byte
non basta a provare compatibilita' delle classi sottostanti.

**Metodo immediato.** Versionare l'identita' del contratto includendo layout,
toolchain compatibile, semantica delle callback e hash/revisione del titolo.
Preparare plugin, metadati e profilo in una generazione separata, validarli in
un processo figlio e attivare l'intera generazione con un solo record atomico.
Conservare la precedente generazione verificata per rollback. Rilevare il gioco
in esecuzione e non tentare di sostituirne i moduli caricati.

**Metodo successivo.** Ridurre il confine DLL a handle opachi e POD versionati
dove realmente utile, senza portare ora tutta l'architettura su una nuova ABI.
Preservare le callback `ExecutionActive` e `ObservableExit` del v2: risolvono una
duplicazione reale dello stato TLS fra host e plugin, non sono residui da togliere.

**Accettazione.** Interruzione/errore dopo ogni fase lascia avviabile la vecchia
generazione. Plugin di titolo o ABI incompatibile viene rifiutato prima di
eseguire codice del titolo. Salvataggi e preferenze non vengono sovrascritti;
la compatibilita' dei savestate e' dichiarata separatamente da quella dei save
nativi, non dedotta dal fatto che la DLL si carichi.

### R07. Cache realmente identificata dagli input che influenzano il risultato

**Evidenza.** [whole_aot_plugin_backend.py](../tools/triaevum_release/whole_aot_plugin_backend.py),
righe 220-241, include compiler, archiver, support library e dipendenze elencate,
ma non hash del linker, sysroot effettivo o argomenti di link/wrapper. Una
modifica a tali flag nel builder puo' mantenere la stessa chiave finale.
[whole_aot_object_cache.py](../tools/triaevum_release/whole_aot_object_cache.py),
righe 183-245, include gli header del progetto e nlohmann; non identifica gli
header/librerie di sistema individuati implicitamente dal compilatore.

**Metodo.** Descrivere una sola `BuildIdentity` normalizzata: formato/generatori,
input del titolo, profilo di ottimizzazione, tutti i tool, comandi effettivi,
dipendenze transitive e sysroot. Usare dipendenze del compilatore oppure un
sysroot immutabile identificato nel suo complesso. L'invalidazione deve essere
corretta prima di cercare ulteriori hit. Esporre perche' una cache e' stata
riusata o invalidata.

**Accettazione.** Cambiando uno alla volta linker, flag, header transitivo,
support library o ABI si invalida soltanto la superficie necessaria. Una modifica
a un'impostazione grafica non rigenera codice del titolo. Riuso e rebuild
producono risultati equivalenti e identita' tracciabili.

### R08. Build rapide, ma con limiti di risorse e ripartenza affidabile

**Evidenza.** [forge_gui.py](../tools/triaevum_release/forge_gui.py), riga 193,
sceglie fino a 12 job dal numero di CPU; il backend accetta fino a 16. Non e'
un budget di memoria. Il CMake whole-AOT possiede gia' un pool di compilazione
limitato, righe 1131-1134: non viene automaticamente applicato al builder Python.

I lock `.object-build.lock` e `.build.lock` usano `O_EXCL` e vengono rimossi nel
normale `finally`. Dopo un arresto forzato non e' prevista la verifica che il
processo proprietario sia morto. Inoltre il backend richiama generazione e
verifica dell'archivio prima di consultare la cache del plugin finale.
Il costo di quest'ultimo passaggio non e' stato quantificato in questa revisione.

**Metodo.** Un orchestratore comune con limite CPU e RAM, un solo link pesante,
stima del picco per shard e margine per il desktop. Non usare tutti i core come
surrogato di una politica delle risorse. Recuperare lock orfani tramite PID piu'
identita'/istante di creazione del processo, senza rubare lock di build vive.
Riusare il plugin finale dopo averne verificato una chiave completa, prima del
lavoro evitabile. Aggiungere cache del link ThinLTO solo dopo averne misurato
utilita' e correttezza; non e' una soluzione automatica a tutti i tempi di build.

**Accettazione.** Misurare separatamente importazione, generazione, compile,
link e installazione, sia a freddo sia a caldo. Un errore/cancel non lascia
worker o lock persistenti. L'aggiornamento del solo renderer non ricompila 256
shard. Il PC resta utilizzabile durante una build completa. Limiti numerici di
RAM e parallelismo si scelgono sui picchi misurati, non sulla potenza dichiarata
della macchina.

### R09. Percorsi e selezione dell'installazione

**Evidenza.** Il profilo scritto da Forge contiene percorsi assoluti a contenuti,
risorse, config e salvataggi. [forge_gui.py](../tools/triaevum_release/forge_gui.py),
righe 212-225, riceve `data_root` in `launch_runtime` ma non lo usa: avvia il
profilo globale dell'installazione. Il resolver del vecchio host e quello del
nuovo profilo non costituiscono una sola autorita' sui percorsi.

**Metodo.** Un `InstallationContext` risolve root pubblica, generazione privata,
config e salvataggi. Persistenza relativa alla root quando interna al pacchetto;
percorsi esterni esplicitamente marcati. Il pulsante Avvia usa il titolo appena
validato, non un profilo globale potenzialmente diverso. Migrazione dei vecchi
percorsi idempotente, senza cancellare dati utente.

**Accettazione.** Due installazioni non si scambiano profili. Cartelle con spazi
e caratteri non ASCII funzionano. Se la modalita' e' dichiarata portabile,
spostare la directory non richiede una nuova compilazione. La migrazione dei
save richiede una prova dedicata: non e' stata verificata in questo audit.

## 4. Architettura NRI e manutenibilita'

### R10. Consolidare i confini presenti, non ricominciare la separazione

Il riferimento resta
[OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md](OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md)
e le regole di [AGENTS.md](../AGENTS.md).

| Superficie | Riscontro | Azione raccomandata |
| --- | --- | --- |
| Shader canonici PICA | Cache canonica e varianti strumentate separate; audit degli output presente | Conservare questo punto di autorita', non aggiungere correzioni per scena/materiale |
| Hook shader | Metadati diretti presenti, analisi di compatibilita' ancora possibile | Misurare i fallback, migrare i producer che realmente li usano |
| Scene view | Riferimenti compatti/versionati, disponibilita' semantica esplicita | Pubblicare solo dati dimostrati; non chiamare disponibile uno scheletro/camera che manca |
| Effect graph | Contratti e test per ordine, risorse, binding, pubblicazione | Portare le verifiche nel percorso distribuito, non soltanto nei test isolati |
| World/UI/trasparenze | Separazione obbligatoria definita nell'architettura | Test di composizione sui framebuffer del pacchetto, incluse transizioni |
| Impostazioni | Regola di un proprietario per ogni opzione gia' definita | Verificare effetto reale, persistenza e conflitti per ogni voce F1 |
| Riutilizzo 3DS | Layer `renderer3ds` e nucleo generico gia' presenti | Non bloccare OOT3D per generalizzare ulteriormente un secondo gioco |

In [pica_shader_pipeline_cache.cpp](../runtime/three_ds_recomp/src/fast/oot3d/pica_shader_pipeline_cache.cpp),
righe 408-430, la cache preferisce gli hook pubblicati direttamente, ma puo'
ricorrere a `AnalyzePicaFragmentShaderHooks`. Esiste il contatore
`CanonicalCompatibilityAnalyses`. Non e' corretto descrivere tutta la pipeline
come semplice patch di stringhe, ne' dichiarare gia' eliminata ogni analisi del
sorgente shader. La correzione consiste nel chiudere i producer incompleti,
senza rimuovere alla cieca un percorso di compatibilita' ancora usato.

[NativeSceneView](../runtime/three_ds_recomp/include/fast/oot3d/native_scene_view.h)
usa riferimenti alla scena risolta invece di copie massive. Questa e' una
scelta da conservare, insieme a versioni delle risorse e discontinuita' temporali.
Gli effetti futuri devono leggere capability esplicite a un punto dichiarato
del graph, non ricostruire oggetti/camera dai pixel finali o dagli indirizzi del
titolo nel backend generico.

### Dimensioni che aumentano il rischio di regressione

Conteggio fisico sul commit revisionato:

| File | Righe |
| --- | ---: |
| `CMakeLists.txt` | 3.383 |
| `tools/oot3d/native_game_runtime/oot3d_native_a32_window.cpp` | 8.801 |
| `runtime/three_ds_recomp/src/fast/backends/gfx_vulkan.cpp` | 5.371 |
| `runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica.cpp` | 9.695 |
| Documento di architettura NRI | 1.666 |

La lunghezza non prova un bug o un collo di bottiglia. Mostra pero' dove si
accumulano responsabilita' e dove ogni integrazione rischia di riaprire input,
presentazione, configurazione e grafica insieme.

**Estrazioni mirate, nell'ordine utile:**

1. Receipt/diagnostica JSON fuori dal loop del window host, con raccolta dati
   compatta e serializzazione fuori dal percorso per-frame ordinario.
2. Configurazione effettiva e comando di applicazione in un modulo condiviso
   fra Forge, avvio e F1.
3. Scheduler temporale, input polling e coordinamento audio con interfacce
   esplicite; mantenere le implementazioni corrette gia' presenti.
4. Build di prodotto e builder privato separati dai target storici tramite
   moduli CMake/preset, senza spostare contemporaneamente tutti i file.
5. Ulteriori estrazioni NRI solo quando un cambiamento tocca una responsabilita'
   identificabile: risorse, composizione, presentazione, provider di un effetto.

Il documento di architettura deve tornare a essere una specifica breve: regole,
confini, responsabili e prove. Spostare la cronologia in un registro di evidenze
collegato. Non perderla e non sostituirla con percentuali di completamento prive
di denominatore.

### Prove grafiche ad alto rendimento

- Riutilizzare le catture framebuffer e i dump Azahar gia' disponibili, con
  profili equivalenti e aggiunte opzionali disattivate per la fedelta' nativa.
- Confrontare anche stato di depth/stencil/blend, target/transfer e ordine dei
  draw; la differenza cromatica dell'immagine da sola non identifica la causa.
- Testare world/UI con depth e trasparenze, soprattutto fate/Navi, acqua, logo,
  menu e transizioni. Un effetto della scena non deve campionare o coprire la HUD.
- Per ogni provider: disattivo = output canonico; attivo = sole risorse e domini
  dichiarati. Verificare resize, cambio profilo e ricreazione swapchain.
- Non diagnosticare un costo GPU da una chiamata isolata: per esempio
  `vkDeviceWaitIdle` su una transizione MSAA non prova un'attesa per ogni frame.

La suite del renderer esiste ma e' opzionale (`THREE_DS_RECOMP_BUILD_TESTS=OFF`
come default). Selezionare un sottoinsieme di contratti per la CI ordinaria e
un replay locale per il pacchetto; non pretendere una nuova campagna sull'intero
gioco per ogni modifica a un pulsante.

## 5. Prestazioni: cosa sappiamo e cosa non sappiamo

### Lettura corretta della prova disponibile

Il benchmark `forge-abi-v2-final-benchmark.json` registra:

| Dato | Valore |
| --- | ---: |
| Warm-up | 60 presentazioni |
| Finestra misurata | 120 presentazioni / 2,000707 secondi |
| Throughput dichiarato | 59,9788 presentazioni/s |
| VSync, pacing e SDL limiter | Disattivati nel report |
| Delta della prova | Fisso, 1/30 secondo |
| Modo prova | Throughput, non gioco regolato sul tempo reale |
| Interpolazione effettiva | Disattivata; moltiplicatore 1 |
| Draw interpolati | 0 |
| Intera run | 180 presentazioni, 359 refresh guest |

E' un risultato utile per quella configurazione e quel tratto, non una misura
di fluidita' x2 o una garanzia di 60 fps in tutte le cutscene. Il delta fisso e
i refresh guest mostrano inoltre perche' non va usato per certificare la normale
velocita' del gameplay. La prova di equivalenza dinamico/statico descritta nel
documento v2 resta utile, ma riguarda la finestra catturata, non ogni scenario.

I timer aggregati riportano circa 1,700 s in `frame_start_seconds`, 0,592 s in
`guest_seconds` e 0,354 s in `visual_presentation_seconds`. Sono intervalli host
ampi, non timestamp GPU; alcuni contatori descrivono fasi annidate. Non vanno
sommati indiscriminatamente o trasformati in una diagnosi certa. La prima
disaggregazione utile riguarda proprio `frame_start_seconds` e le attese.

Il vecchio valore di circa 15,7 fps e il nuovo valore vicino a 60 non costituiscono
un confronto controllato se warm-up, scena, durata, audio e clock differiscono.
Non viene quindi attribuita in questa revisione una percentuale di accelerazione.

### Metodo di misurazione da applicare

1. Identificare build, DLL, scenario, risoluzione, GPU/driver, profilo effettivo,
   cache calda/fredda e stato degli effetti. Salvare questi dati col risultato.
2. Separare boot/caricamento, gameplay stabile, movimento camera e cutscene
   complessa. Iniziare con 3-5 scenari gia' disponibili, non un nuovo sistema
   di automazione del gioco intero.
3. **Throughput:** VSync e limitatori realmente disattivati, clock sintetico
   dichiarato. Audio emulato conservato; eventuale output host escluso soltanto
   in una variante esplicita, per evitare backpressure artificiale.
4. **Esperienza reale:** clock reale, audio attivo, x1/x2/x3, con e senza VSync.
   Misurare velocita' guest/tempo host e regolarita' della presentazione.
5. Usare finestre stabili di almeno 30 secondi e tre ripetizioni come protocollo
   iniziale proposto. Pubblicare mediana, p95/p99 del tempo-frame, stutter,
   CPU/GPU time, attese, byte copiati, cache miss e pipeline compilate.
6. Per x2/x3 registrare identificativi dei frame sorgenti e alpha di ogni
   campione. A parita' di input e tempo guest, stato del gioco identico a x1;
   su movimento continuo, framebuffer intermedi diversi e continuita' su attori
   e camera. Nessuna interpolazione attraverso tagli di camera/load-state.

Budget-obiettivo, non risultati gia' ottenuti: 16,67 ms a 60 Hz e 11,11 ms a
90 Hz, con margine per input/audio e varianza. Distinguere lavoro effettivo da
attesa deliberata della presentazione. Un Present duplicato non e' un nuovo
frame visivo. Non eliminare fog, audio, attori o effetti selezionati per vantare
un aumento delle prestazioni del profilo completo.

### Strategia di ottimizzazione

Prima misurare il percorso consegnato all'utente. Poi scegliere il maggiore
costo evitabile e correggerne l'intera categoria: copie di payload invarianti,
invalidazioni cache, compilazione shader durante il frame, sincronizzazioni
eccessive o serializzazione diagnostica. Contatori su ownership, byte e hit/miss
sono piu' utili di una nuova riscrittura dell'AOT senza profilo CPU.

Ogni tranche deve produrre un confronto A/B sullo stesso pacchetto/scenario.
Le ottimizzazioni che riducono il lavoro mantenendo gli output vengono prima
di quelle che abbassano qualita' o copertura. Non e' dimostrato da questa audit
che il codice guest sia oggi il collo di bottiglia dominante.

## 6. Strategia operativa

### Tranche A: un prodotto identificabile e riproducibile

Chiudere R01, R03 e la parte di R04/R05 relativa all'identita' degli artefatti.
Un solo comando/preset produce runtime pubblico, stub, Forge e manifesto.
Testare il pacchetto estratto: boot, F1, TopScreen, valore effettivo x1/x2 e
riavvio con impostazioni persistite. Nessun lavoro nuovo di grafica in questa
tranche. La prova su questa macchina non viene chiamata prova su macchina pulita.

**Consegna:** archivio riproducibile, receipt e profilo verificato; non solo un
nuovo exe copiato dentro un'installazione vecchia.

### Tranche B: Forge autosufficiente e aggiornamenti affidabili

Chiudere R02, R06 e R09; collegare R07 all'identita' del toolchain. Probe iniziale,
generazioni transazionali, diagnostica comprensibile e validazione del plugin.
Provare prima una fixture minima, poi una importazione ROM completa in ambiente
pulito. Preservare salvataggi, profili e output privati.

**Consegna:** preparazione realmente da sola ROM decriptata `.3ds`/`.cci`, senza
installazioni implicite di sviluppo, piu' riavvio dopo errore/cancel e rollback.

### Tranche C: tempi e fluidita' verificabili

Chiudere R08 e la matrice della sezione 5 sullo stesso percorso v2 distribuito.
Misurare a freddo/caldo sia Forge sia gioco. Riparare prima i costi dominanti
misurati, preservando le callback del runtime maturo e i contratti NRI.

**Consegna:** tabella A/B con build esatta, tempi di preparazione, RAM massima,
framerate reale e campioni x2/x3 dimostrati. Nessuna promessa ricavata dai soli
2 secondi del benchmark precedente.

### Tranche D: consolidamento modulare e fedelta'

Applicare R10 nei punti realmente modificati. Collegare i test renderer/input/
settings al prodotto e qualificare le composizioni piu' a rischio con gli
oracoli esistenti. Archiviare documenti superati e target diagnostici dietro
nomi espliciti; rimuoverli soltanto dopo verifica delle dipendenze.

**Consegna:** confini piu' piccoli e testabili senza cambiare l'output, non una
nuova infrastruttura parallela ancora priva delle funzioni gia' disponibili.

### Criterio di rimodulazione

Non aprire una nuova strategia a ogni errore locale. Se una tranche non produce
la consegna dichiarata, identificare quale assunzione e' stata smentita e
modificare quel confine, mantenendo gli artefatti funzionanti. Non accumulare
test che dimostrano solo se stessi. Un bug del pacchetto non giustifica nuova
decompilazione, e un bug di shader non giustifica cambiare host o gameplay.

Commit piccoli per responsabilita', ma tranche complete per risultato utente:
codice, test, package receipt e documentazione devono descrivere lo stesso stato.

## 7. Cosa conservare e cosa evitare

**Conservare:** whole-AOT v2 giocabile come percorso del prodotto; separazione
fra pubblico e derivati privati; NRI/Vulkan; asset/PICA nativi; TopScreen;
configurazione F1; audio; shader canonici distinti dalle estensioni; risorse
versionate; contratti temporali e test gia' esistenti. Una copertura di 12.419
funzioni generate non equivale a 12.419 funzioni C++ semantiche indipendenti
dall'architettura guest, ma questo non ne invalida l'utilita' per il prodotto.

**Evitare:** nuovo boot sperimentale come prerequisito della release; ritorno
silenzioso al vecchio host; sostituzioni manuali nel pacchetto finale; percentuali
di fedelta' calcolate dal numero di test; eliminazione di funzionalita' per
accelerare le prove; ripetizione di confronti N64 non pertinenti al backend;
nuova estrazione/decompilazione prima di un gap concreto che la richieda.

La decompilazione esterna rimane una fonte per chiarire semantica e copertura,
non un requisito da riallineare continuamente per correggere packaging, ABI,
presentazione o toolchain.

## 8. Limiti e prossima decisione

Questo audit non certifica parita' visiva completa, assenza di bug di gameplay,
prestazioni in tutte le location, compatibilita' di ogni save-state o completa
portabilita' multipiattaforma. Non sono state eliminate build o dipendenze:
farlo prima di ricostruire le identita' aggraverebbe il rischio rilevato in R01.

**Prossima azione consigliata: Tranche A, senza modificare il gameplay.** E' il
miglior rapporto costo/risultato per impedire che l'utente riceva di nuovo una
versione diversa da quella che viene dichiarata funzionante. Segue la chiusura
del toolchain pulito, non un'altra campagna di microcorrezioni del renderer.
