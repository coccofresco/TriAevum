# Esecuzione della revisione del 5 settembre

Riferimento: [rapporto tecnico](TRIAEVUM_TECHNICAL_STRATEGIC_REVIEW_2026-09-05.md).
Commit implementativo: `10bcc10b84c0d16b76a176d0f6b1e7bf9639dd9a`.
Non tutte le tranche del rapporto sono concluse.

## Candidato aggiornato da usare

`I:\TriAevum-0.5.1-candidate-r2`, prodotto dal commit
`0ecdee0248d1ceead9df44cc611d1ddb094b0788`, sostituisce il primo candidato citato
nelle prove storiche sotto. Include anche l'identita' del builder come dato
esplicito nel bundle PyInstaller, senza dipendere da `__file__` di un modulo
contenuto soltanto nell'archivio Python congelato.

- Audit finale superato: 37 file, 265.406.794 byte, manifesto/source ZIP coerenti.
- SHA-256 runtime: `7e56c32ba3a5dcddbf309613520c770ffcafacd73c9466d02b0f3b9c685cc647`.
- Suite finale: 68/68 test superati, incluso controllo dei pixel della cattura.
- Forge congelato r2: probe compile/link/execute superato in circa 12,88 secondi;
  identita' del builder presente. Ancora una prova su questa macchina.
- Run r2 completata autonomamente: 30 secondi di loop, 1.711 presentazioni dopo
  warm-up in 28,8261 secondi, 59,36 presentazioni/s con VSync/pacing attivi.
- TopScreen selezionato, x2 attivo, 134.367 draw interpolati; framebuffer
  ispezionato, con 437 colori campionati. Nessun processo di prova lasciato aperto.
- Evidenze r2: `I:\oot3dre_work\triaevum-release-artifacts-r2\real-clock-x2\`.

L'installazione transazionale, il sysroot autosufficiente e le restanti voci
aperte della tabella non vengono dichiarati risolti da queste prove.

## Correzioni applicate

| Rilievo | Stato concreto |
| --- | --- |
| R01 | Target pubblico collegato al runtime maturo; vecchio host rinominato diagnostico. CMake pubblica i percorsi esatti. |
| R02 | Probe reale compile/link/execute prima dell'importazione ROM. La verifica su Windows senza ambiente di sviluppo rimane aperta. |
| R03 | Default x2 serializzato dal renderer, consumato da Forge; configurazioni esistenti preservate. x2 attivo nella run del candidato. |
| R04 | Nuovo candidato prodotto automaticamente, manifesto e source ZIP dello stesso commit, controllo dello stub e degli hash. |
| R05 | Verifica del contratto dell'exe durante packaging; CI estesa ai percorsi runtime e ai test Windows. Rimosso il falso test che considerava utile un qualsiasi fallimento del packager. Non e' ancora CI del gioco completo. |
| R06 | Aperto: attivazione per generazioni/rollback e rafforzamento dell'identita' ABI. |
| R07 | Chiave plugin estesa a linker e builder. Rimane aperta l'identita' completa del sysroot implicito. |
| R08 | Lock gestiti dal kernel, testati anche dopo morte del processo; parallelismo GUI limitato da CPU/RAM. Profilazione completa della build e cache del link ancora aperte. |
| R09 | Aperto: contesto d'installazione unico e portabilita' dei percorsi. |
| R10 | Nessuna nuova modifica al renderer o refactor indiscriminato. Consolidamento successivo come da rapporto. |

## Artefatto generato

`I:\TriAevum-0.5.1-candidate`

- 37 file consentiti, 265.397.309 byte.
- Manifesto con `qualification=candidate`, sorgenti del commit `10bcc10b8`.
- Runtime SHA-256: `e44f805bde35991ce10e9da1a3aff9258cdfbeb14b0dddc165a7d551385c6acb`.
- Generato tramite `prepare_release.py`, non copiando il vecchio pacchetto usato.
- Niente plugin privato o ROM nel candidato. Non e' una certificazione di
  installazione autosufficiente su tutte le macchine.

Il comando riproducibile e' in [TRIAEVUM_PRODUCT_WORKFLOW.md](TRIAEVUM_PRODUCT_WORKFLOW.md).
Il package builder forza una riconfigurazione per evitare un build ID rimasto
al commit precedente anche quando il codice non richiede ricompilazione.

## Prove effettuate

- Suite Python prima del packaging: 67/67 test superati.
- Build incrementale del vero target `triaevum_public_runtime`, senza
  rigenerazione/ricompilazione degli shard del titolo.
- `--product-info`: NRI, F1 e TopScreen presenti; ABI v2; stub pubblico;
  configurazione `Custom / Interpolated2x` e aggiunte grafiche disattivate.
- Forge congelato nel pacchetto: probe C++ reale superato in circa 12,9 secondi.
  Il risultato dichiara `scope=current_machine` e non dichiara ambiente ermetico.
- Test di lock: esclusione reciproca, rilascio dopo terminazione brusca,
  nessuna acquisizione automatica di vecchi lock PID potenzialmente attivi.
- Test delle preferenze: un file esistente, anche legacy, non viene sostituito;
  un JSON corrotto genera un errore, non un reset silenzioso.

## Prova del gioco reale

Harness: `tools/triaevum_release/validate_product_run.py`.
Crea una copia privata del candidato, non modifica pacchetto originale,
contenuti originali o salvataggi esistenti. Usa un plugin v2 privato gia'
disponibile: **non e' una prova della generazione completa da ROM**.

Evidenze locali:
`I:\oot3dre_work\triaevum-release-artifacts\real-clock-x2-capture\`

| Campo | Riscontro |
| --- | --- |
| Durata del loop richiesta | 30 secondi, clock reale |
| Durata complessiva del processo | Circa 36,3 secondi |
| Finestra dopo warm-up | 1.715 presentazioni in 28,8815 secondi |
| Frequenza | 59,38 presentazioni/s |
| VSync / pacing / limiter | Attivi: prova di esperienza reale, non throughput libero |
| Delta fisso | Assente |
| Profilo UI | `topscreen` |
| Interpolazione | Attiva, moltiplicatore 2 |
| Draw interpolati | 134.663 |
| Framebuffer | 437 colori nel campionamento; ispezione visiva dell'intro con Link/Epona e ambiente |

Una prima run ha misurato 60,10 presentazioni/s ma catturato il frame iniziale
nero: il default della cattura e' il frame zero. Il validatore ora cattura dopo
il caricamento e controlla i pixel, non soltanto l'esistenza del BMP. Non e'
stata applicata alcuna correzione grafica per modificare questo risultato.

Non dedurre da queste due run piena regolarita', margine GPU sopra 60, copertura
del gameplay, F1 interattivo verificato dall'utente o parita' Azahar. Rimangono
da completare confronto dei campioni intermedi a livello di pixel, x3 e misure
p95/p99 su piu' scenari. La prima run mostra anche jitter nei timer di pacing:
la media vicina a 60 non lo annulla.

## Prossimo lavoro

Chiudere R06/R09 con un contesto d'installazione e attivazione transazionale,
poi l'ambiente ermetico R02/R07. Qualificare una importazione completa e i
controlli F1/TopScreen nel gameplay prima di promuovere il candidato a release.
Proseguire con la matrice prestazionale del rapporto senza ridurre le funzioni.

## Integrita' dell'avvio Forge: prima parte R06/R09

- `forge.py`: verifica entrambi i JSON utente prima di sostituire il plugin;
  registra inoltre SHA-256 del profilo pubblicato. Una configurazione corrotta
  non sostituisce piu' la DLL o il profilo precedenti.
- `installed_runtime.py`: controllo isolato di eseguibile, DLL realmente
  importata dal loader, profilo e percorsi del titolo/configurazioni/salvataggi.
  I ricevuti precedenti senza hash del profilo sono accettati, ma rimangono
  soggetti ai controlli dei percorsi e delle identita' host/plugin.
- `forge_gui.py`: l'avvio usa davvero la cartella dati richiesta e il ricevuto
  del suo titolo attivo. Un'installazione mista viene segnalata prima di Popen.
- Verifica: **73 test Python superati**, inclusi cinque nuovi test su identita',
  cartella alternativa, compatibilita' dei ricevuti e configurazione corrotta.

Questa tranche non chiude R06/R09: nessuna garanzia di rollback dopo perdita
di alimentazione, nessun lock tra pubblicazione e avvio e nessuna protezione
aggiunta al doppio click diretto su TriAevum.exe. La DLL rimane importata dal
loader Windows prima di main; servira' un caricatore esplicito per generazioni
coerenti. Restano anche i percorsi assoluti e la verifica completa ABI/layout.
Non sono stati modificati renderer, gameplay, salvataggi o default grafici.
Il candidato r2 gia' pubblicato non contiene queste nuove modifiche Forge;
andra' rigenerato prima del test della distribuzione aggiornata.

## Pubblicazione recuperabile Forge: seconda parte R06

`activation_transaction.py` isola backup, journal, lock di processo e rollback
dal codice di generazione del titolo. `build_private_title` comprende nella
transazione packaging/metadati del modulo, DLL installata, profilo, titolo
attivo e configurazioni. I file generati immutabili nella cache non vengono
cancellati dal rollback; i salvataggi non vengono modificati.

- Errore Python: ripristino dei file precedenti e rimozione dei nuovi file
  della transazione. Se il ripristino fallisce, journal e backup rimangono.
- Arresto brusco: al successivo tentativo per gli stessi target viene prima
  ripristinata la generazione precedente. Target diversi vengono rifiutati,
  senza tentare di indovinare quali file riscrivere.
- Backup e journal vengono sincronizzati su file. Non e' una certificazione
  di resistenza a guasti del disco/perdita di alimentazione.
- Forge rifiuta l'avvio con journal pendente; lock kernel condiviso fra
  pubblicazione e validazione/Popen evita operazioni Forge simultanee.
- Il lock termina dopo Popen: non e' un lease per tutta la vita del gioco e
  non elimina la finestra prima che il loader Windows apra la DLL.

Verifica: **79 test Python superati**, con errore iniettato, processo figlio
terminato tramite `os._exit`, recupero, target discordanti, esclusione reciproca
e rifiuto dell'avvio pendente. Forge congelato con PyInstaller compilato in
circa 24 secondi e smoke test `doctor --inventory` superato:
`I:\oot3dre_work\triaevum-release-artifacts-r3\forge\TriAevumForge.exe`.
Questo e' un artefatto di verifica, non un nuovo pacchetto utente completo.

R06 rimane parziale fino al caricatore nativo esplicito con generazioni
immutabili e selettore unico: il doppio click diretto e l'ABI/layout completo
non sono ancora coperti. Nessuna ricompilazione degli shard titolo o modifica
di renderer/gameplay e' stata necessaria in questa tranche.

## Caricamento esplicito di generazioni private

Il runtime Windows non importa piu' `triaevum_title_aot.dll` nella tabella PE:
carica la DLL selezionata da `--title-plugin` prima della prima query ABI e la
mantiene per tutta la vita del processo. Il profilo di avvio supporta lo stesso
argomento, quindi anche il doppio click usa la generazione selezionata. Senza
selezione resta compatibile con la DLL legacy accanto all'eseguibile.

Forge pubblica `private-plugins/<sha256>/triaevum_title_aot.dll`, esegue una
query nativa separata con `--verify-title-plugin` e sostituisce il profilo solo
dopo il successo. La sostituzione atomica del profilo e' il punto di visibilita'
della nuova generazione per il runtime; non vengono sovrascritti plugin in uso.
Il rollback Forge conserva i file immutabili non piu' referenziati, riutilizzabili
al prossimo tentativo. Non eliminarli mentre un gioco puo' ancora usarli.

Il validatore real-clock ora esercita il lancio senza argomenti da profilo,
mantenendo lo stub pubblico accanto all'exe e selezionando una DLL privata in
una generazione separata. Verifiche gia' effettuate: build incrementale del
runtime riuscita, import PE privo della DLL titolo, query della DLL privata v2
esistente riuscita. La qualificazione del nuovo pacchetto segue sotto.

Limiti distinti: ABI v2 validata strutturalmente ma senza fingerprint completo
dei layout C++; portabilita' dei manifest con percorsi assoluti ancora aperta;
configurazioni utente condivise, non snapshot per generazione. L'avvio diretto
puo' vedere il profilo vecchio o nuovo durante una pubblicazione: entrambi
selezionano un plugin completo gia' verificato, non una DLL in sostituzione.

### Riscontri end-to-end

Candidato r4 da `6b0a364c1`: 37 file, 265.433.319 byte, audit superato.
Due run real-clock concluse senza processi lasciati aperti:

| Percorso | Presentazioni misurate | Frequenza | Draw interpolati |
| --- | --- | --- | --- |
| Profilo predefinito, avvio senza argomenti | 1.701 in 28,684 s | 59,30/s | 133.261 |
| Pubblicazione Forge reale, profilo generato | 1.716 in 28,928 s | 59,32/s | 134.778 |

Entrambe con warm-up di 60 frame escluso, VSync/pacing attivi, nessun delta
fisso, x2 e TopScreen attivi. Framebuffer non vuoto (447/451 colori campionati);
ispezione del primo: intro con ambiente, cielo, Link ed Epona. Non e' una
misura di throughput senza limite ne' una prova interattiva di F1 o parita'
grafica con Azahar. Evidenze:
`I:\oot3dre_work\triaevum-release-artifacts-r4\real-clock\` e
`I:\oot3dre_work\triaevum-release-artifacts-r4\published-real-clock\`.

Il secondo test ha permesso anche di eliminare una dipendenza residua di
`publish_private_runtime` dalla cartella dell'istanza Forge: host e resources
sono ora risolti dall'installazione di destinazione esplicita. Suite: 81 test.
Restano fuori da questa chiusura funzionale della pubblicazione i punti del
rapporto su sysroot ermetico, layout ABI, relocazione completa e qualificazione
su Windows pulito. Il pacchetto rimane pertanto candidato, non release finale.

### Candidato aggiornato r5

- `I:\TriAevum-0.5.1-candidate-r5`, sorgenti/binari da `4fefb88a1`:
  37 file pubblici, 265.434.990 byte, audit superato.
- Forge congelato: `doctor` esegue davvero compile/link/run; successo in
  13,365 secondi su questa macchina, ambiente ermetico non dichiarato.
- Pubblicazione privata e run NRI: 1.710 presentazioni misurate in 28,922 s
  dopo warm-up, **59,13/s**, 134.268 draw interpolati, x2 e TopScreen attivi.
  Loop richiesto 30 s, processo complessivo 35,64 s, concluso normalmente.
- Evidenze e installazione privata pronta ad avvio normale:
  `I:\oot3dre_work\triaevum-release-artifacts-r5\published-real-clock\`.
  Il suo `private-installation/TriAevum.exe` usa il profilo Forge senza limiti
  temporali; i limiti della verifica erano override soltanto della run di test.

L'audit va eseguito a Forge chiuso: PyInstaller estrae temporaneamente file
in `.triaevum-forge-runtime` mentre il processo vive. Un primo tentativo di
audit contemporaneo a `doctor` li ha correttamente rifiutati; dopo la chiusura
e la pulizia automatica, audit e run sono riusciti senza deroghe all'allowlist.

## R02/R07: dipendenze Windows esplicite

Implementato un contratto sysroot verificato usato da probe, cache oggetti e
linker plugin; include hash di header/librerie e versione di compatibilita'
MSVC esplicita. Le variabili della Developer Shell non prevalgono sul contratto.
Importatore locale e test dedicati in `tools/triaevum_release`; dettagli,
percorsi, riscontri e limiti in [TRIAEVUM_WINDOWS_SYSROOT.md](TRIAEVUM_WINDOWS_SYSROOT.md).

Il probe reale e' riuscito usando la copia privata; nessuna modifica al renderer
o al codice del titolo. Non sono ancora chiusi acquisizione automatica dello
SDK, qualificazione su Windows pulito e verifica del wrapper completo con il
sysroot. Il pacchetto pubblico r5 rimane immutato. Questa e' implementazione
parziale dei requisiti del rapporto, non motivo per dichiararlo completato.

## R02/R05/R06: prova nativa del confine DLL senza ROM

Il nuovo `validate_whole_aot_toolchain.py` usa generatore, cache oggetti, wrapper
e linker reali con un programma sintetico di cinque istruzioni. Un host C++
carica la DLL e verifica aritmetica, memoria, callback, TLS, uscita osservabile
e rifiuto della versione ABI errata. Tutte le verifiche sono riuscite con il
sysroot esplicito e la support library realmente distribuita nel candidato.
Dettagli ripetibili in [TRIAEVUM_WINDOWS_SYSROOT.md](TRIAEVUM_WINDOWS_SYSROOT.md).

La prova ha evidenziato verifiche hash duplicate del sysroot nella stessa
operazione: il contratto verificato viene ora passato al builder dell'archivio,
senza cache persistente o verifica omessa nelle run successive. Suite Python:
87 test. Nessuna compilazione del titolo completo o modifica del renderer.

## R02: acquisizione privata delle dipendenze

Aggiunti planner del catalogo e download verificato dei payload Microsoft,
con cache riutilizzabile e test negativi. La qualificazione reale del catalogo
e' stata fermata da una differenza fra SHA/dimensione dichiarati dal canale e
contenuto restituito dal relativo URL. Non e' stato aggirato il controllo.
Valori, percorsi e passi residui in
[TRIAEVUM_TOOLCHAIN_ACQUISITION.md](TRIAEVUM_TOOLCHAIN_ACQUISITION.md).
Suite: 93 test; acquisizione GUI ed estrazione SDK non ancora implementate.

## R09: riferimenti portabili dell'installazione

`installation_context.py` centralizza i riferimenti persistenti: dati interni
relativi al manifest, argomenti del profilo relativi a `${profile_dir}`,
input esterni assoluti e marcati come tali. Forge, GUI e verifica della ricevuta
usano lo stesso contratto. Gli output precedenti restano leggibili; non viene
forzata una migrazione dei manifest esistenti o dei salvataggi.

Test automatici: trasferimento dei dati preparati in una cartella con spazi e
caratteri accentati, rimozione degli input originali e verifica hash dei nuovi
percorsi; espansione dei percorsi di DLL/config/salvataggi sotto una nuova root;
conservazione dei riferimenti esterni. Questa prova Python non certifica ancora
la gestione Unicode end-to-end del runtime C++.

Run nativa con pubblicazione Forge e profilo relativo:
`I:/oot3dre_work/triaevum-release-artifacts-r5/portable-profile-check`.
Host pubblico r5 invariato, DLL privata preesistente, 30 secondi di loop:
1705 presentazioni in 28.7411 secondi dopo 60 frame di riscaldamento,
59.3228 fps, VSync/pacing/limiter attivi, TopScreen e x2 attivi,
133565 draw interpolati, framebuffer non uniforme (451 colori campionati).
Non e' una misura di throughput massimo ne' una build ROM-to-plugin a freddo.

Residui R09: qualificazione nativa dopo trasferimento completo, percorsi Unicode
nel processo C++, migrazione esplicita delle vecchie installazioni. Restano
aperti anche gli altri requisiti riportati sopra: il rapporto non e' completato.

### Trasferimento nativo e diagnosi Unicode

`validate_product_run.py --relocate` ora copia e verifica anche gli input privati,
pubblica il profilo e rinomina la sola installazione di test prima della run.
La vecchia posizione non esiste piu'; nessuna ricompilazione del titolo.
Prova riuscita in `triaevum-release-artifacts-r5/relocation-check`: 59.1800 fps,
TopScreen/x2 attivi, framebuffer non uniforme. Suite Python: 98 test.

`--relocate --unicode-path` aggiunge caratteri latini e non latini. Sul candidato
r5 il preflight ha fallito: i caratteri giapponesi erano diventati `??` negli
argomenti nativi. Aggiunto manifest UTF-8 al target Windows, non un aggiramento
del controllo DLL. Il preflight della nuova build con lo stesso percorso ora
carica correttamente il plugin. Resta da ripetere la run dal pacchetto coerente.

### Candidato r6: run Unicode riuscita

Pacchetto `I:/TriAevum-0.5.1-candidate-r6`, sorgenti e host dal commit
`c9db0ccf1`, 37 file pubblici, 265468890 byte, audit superato. Include Forge
aggiornato e il manifest Windows UTF-8. Non include il sysroot privato.

Run con `--relocate --unicode-path`, evidenza in
`I:/oot3dre_work/triaevum-release-artifacts-r6/unicode-relocation-check`:
1713 presentazioni in 28.8879 secondi dopo 60 frame di riscaldamento,
59.2982 fps, TopScreen e x2 attivi, 134515 draw interpolati. Framebuffer
ispezionato: paesaggio, Link ed Epona visibili. Gli input privati sono copie
verificate dentro la directory trasferita; nessuna ricompilazione del titolo,
nessuna modifica all'installazione originale.

Questo chiude la prova nativa di trasferimento e percorsi non ASCII per il caso
verificato. Non chiude migrazione dei vecchi manifest/salvataggi, pulizia del
toolchain, prova su Windows senza SDK o gli altri requisiti del rapporto.

### Migrazione esplicita delle installazioni precedenti

`forge.py migrate-paths` usa il journal di attivazione per migrare manifest,
indice, ricevuta, profilo e selezione attiva. Non apre ne' riscrive salvataggi,
non converte formati e non rigenera codice. Verifica prima gli hash del titolo,
runtime e plugin e i riferimenti incrociati degli input. Percorsi esterni
rimangono assoluti e vengono elencati nel risultato; non viene promessa la loro
portabilita'. Istruzioni in `TRIAEVUM_PRODUCT_WORKFLOW.md`.

Suite: 100 test, inclusi fixture legacy con percorsi assoluti, secondo passaggio
senza modifiche, conservazione byte per byte del salvataggio e delle preferenze,
trasferimento successivo e rollback completo su errore tardivo.
Eseguito anche il comando reale sulla copia privata in
`triaevum-release-artifacts-r5/relocation-check/relocated installation`: prima
run `migrated`, seconda `unchanged`, poi avvio di 30 secondi riuscito.
`post-migration-state.json`: 59.2736 fps dopo warmup, TopScreen/x2 attivi.
Questa run usa dati gia' relativi con metadati precedenti: la trasformazione
completa legacy assoluta e' coperta dai test, non da una ROM ricompilata.

Il codice di migrazione e' successivo al candidato r6 e non ancora incluso nel
suo Forge congelato. Restano aperti R02 e gli altri requisiti del rapporto;
nessuna qualificazione di macchina pulita o parita' grafica e' implicata.

## R02: dipendenze CAB lette senza installazione

Il planner puo' ora risolvere i CAB strettamente necessari dalle tabelle Media
degli MSI gia' scaricati e verificati. Il lettore usa API Windows in sola
lettura, non avvia installer. Prova reale su MSI SDK locale riuscita; test
coprono identita' MSI, nomi CAB, cataloghi e URL invalidi. Suite: 104 test.
Dettagli in `TRIAEVUM_TOOLCHAIN_ACQUISITION.md`. Il mismatch del catalogo remoto
si riproduce anche sul canale LTSC: non e' stato aggirato. Estrazione, consenso
licenze, attivazione GUI e qualificazione Windows pulito restano da completare.

## R08: ricompilazione grafica spuriosa alla riconfigurazione

Causa identificata in `oot3d_fidelityfx_vk_core_aliases.cmake`: l'ultima
`file(WRITE)` riscriveva sempre `ffx_vk.cpp`, anche con tutte le patch gia'
applicate. La modifica del timestamp invalidava oggetto e archivio ad ogni
preparazione del pacchetto. Ora il file viene scritto una sola volta e solo se
il contenuto finale differisce da quello iniziale. Le tre patch grafiche e il
loro risultato rimangono invariati.

Test CMake reale su fixture: applicazione iniziale delle tre correzioni e
seconda applicazione con byte e timestamp immutati. Suite: 105 test con
`CMAKE_COMMAND` impostato al CMake di Visual Studio. Riconfigurazione e build
reali del target pubblico riuscite: nessuna ricompilazione di `ffx_vk.cpp` o
del suo archivio; solo identita' prodotto e link, attesi dopo nuovi commit.
Il target ShaderMake effettua ancora il proprio controllo incrementale.
Non e' una misura di accelerazione della compilazione completa del titolo.

## R07/R08: controllo anticipato della DLL finale

Con sysroot verificato, `whole_aot_plugin_backend.py` calcola un'identita'
degli input, tool, generatori e dipendenze prima della generazione. Una DLL
finale con ricevuta corrispondente e hash valido viene riusata senza generare
C++ o attraversare gli oggetti. Senza sysroot esplicito rimane il percorso
precedente: dipendenze Windows scoperte implicitamente non giustificano un hit
anticipato. La copertura degli header e' conservativa, non ancora minima.

Test: modifica linker/header invalida la chiave; DLL alterata rifiutata; hit
anticipato non richiama il builder dell'archivio. Suite completa: 105 test.
Probe nativo reale in `I:/oot3dre_work/triaevum-native-early-cache-probe`:
prima run 56.4336 s (`built`), seconda 29.7287 s (`reused`), tutte e sei le
verifiche ABI riuscite e DLL con lo stesso SHA della precedente qualificazione.
I tempi comprendono hash sysroot e compilazione dell'host di prova; non sono
tempi di preparazione del titolo completo e non quantificano il suo speedup.

## R05: prova Windows del supporto nativo dalla stessa definizione del prodotto

La libreria di supporto ha ora una definizione condivisa in
`tools/triaevum_release/cmake/TitleSupport.cmake`, usata dal prodotto e dal
progetto isolato `tools/triaevum_release/native_probe`. Nessuna duplicazione
della lista dei cinque sorgenti, nessuna dipendenza dal renderer o dalla ROM.
La compilazione isolata richiede C++20 ed eccezioni MSVC esplicite.

Build isolata locale riuscita in circa 3.5 secondi; probe con l'archivio appena
prodotto riuscito in 31.3431 secondi, tutte e sei le verifiche ABI superate.
Evidenza in `I:/oot3dre_work/triaevum-isolated-support-probe`. La DLL sintetica
ha lo stesso SHA-256 delle prove precedenti. Suite Python: 105 test.

La CI Windows ora costruisce questo supporto e la DLL sintetica, esegue il
probe e conserva solo log/ricevute. Il runner crea un sysroot privato dagli SDK
preinstallati: non e' la prova Forge su Windows pulito richiesta da R02.
La CI remota non e' stata eseguita in questa sessione; YAML analizzato localmente.
Il test della patch renderer e' esplicitamente saltato quando il submodule non
e' presente, invece di fallire per un file assente o simulare una verifica.

## R02: layout dei file SDK dai metadati MSI

Estesa la lettura in sola lettura alle tabelle Directory/Component/File.
Il mapping dei file CAB ai percorsi finali deriva dalle relazioni originali,
con controlli su nomi Windows, attraversamenti, cicli e collisioni. Sullo stesso
MSI locale verificato: 365 librerie x64 risolte correttamente. Suite: 107 test.
Nessun installer eseguito; la decompressione CAB e l'attivazione del sysroot
acquisito non sono ancora implementate. Dettagli nel documento di acquisizione.

## R02: estrazione nativa CAB/MSI verificata

Implementata decompressione tramite SetupAPI con selezione dei file dal layout
MSI, hash/size verificati e pubblicazione solo a copertura completa. Nessuna
azione installer. Dal pacchetto SDK locale sono stati estratti 365 file,
69,790,662 byte; tutti coincidono per SHA-256 con l'SDK installato. Test nativi
su CAB sintetico coprono errori e mancata pubblicazione dei risultati parziali.
Suite: 108 test. Restano VSIX, assemblaggio toolchain, consenso, GUI e prova
Windows pulito; il problema dei cataloghi remoti non e' stato aggirato.

## R02: estrazione VSIX

Implementata estrazione ZIP/VSIX verificata e pubblicazione da staging completo,
senza registrazione di estensioni o esecuzione di file. Prova su pacchetto VSIX
locale reale: 32 file, 6,975,026 byte, metadati conservati. Non e' il pacchetto
CRT scelto per Forge e non viene distribuito: e' una prova del formato.
Dettagli e hash nel documento di acquisizione. Restano assemblaggio dei pacchetti
selezionati, fonte remota coerente, consenso/GUI e prova su Windows pulito.

## R02: assemblaggio verificato e dipendenza SDK corretta

Unificati assemblaggio e snapshot del sysroot: staging completo, inventario
verificato, rifiuto di collisioni e nessuna pubblicazione incompleta. La prova
reale ha identificato una dipendenza mancante nel planner (Store Apps Libs,
necessaria anche per kernel32.lib), ora inclusa. Le librerie UM estratte dai due
MSI ricostruiscono esattamente l'inventario precedente insieme agli header/CRT
privati gia' disponibili. Il probe nativo passa tutti i sei controlli in 36.30s
e produce la stessa DLL sintetica. Suite completa: 113 test superati.
Evidenze e limiti in TRIAEVUM_TOOLCHAIN_ACQUISITION.md. Acquisizione remota,
consenso/GUI e qualificazione su macchina pulita restano aperti; R02 non chiuso.

## R02: coordinamento acquisizione ed estrazione

Collegati download verificati, risoluzione CAB dai veri MSI ed estrattori in
`acquire_windows_components.py`. Consenso esplicito prima dei download, callback
di progresso, ricevuta con piano e hash, staging transazionale. I download
completati rimangono riutilizzabili dopo un errore; componenti incompleti non
vengono pubblicati. Non attiva una toolchain e non esegue installer.
Prova reale cache-only sul VSIX locale: 32 file. Suite: 115 test superati.
Restano fonte remota qualificata, mapping dei pacchetti selezionati al sysroot,
presentazione licenze/GUI e qualificazione pulita. Nessuna estensione delle
conclusioni di questo test al download remoto o alla compilazione del titolo.

## R02: acquisizione reale dei due pacchetti CRT

Individuato un errore nei metadati dimensionali del pacchetto header: dimensione
dichiarata superiore a quella ricevuta, ma SHA-256 esattamente coincidente.
La dimensione ora e' un limite superiore, l'hash rimane obbligatorio e invariato.
Acquisiti ed estratti entrambi i veri pacchetti CRT selezionati (324 e 45 file)
tramite metadati dell'installazione VS locale. Suite: 116 test superati.
Dettagli nel documento di acquisizione. Il mismatch degli hash dei cataloghi
remoti e la qualificazione della pipeline completa restano distinti e aperti.

## R02: consumer dei layout e CRT acquisito utilizzabile

Implementato `windows_component_layout.py`: verifica inventari e mapping dei
layout originali MSI/VSIX al sysroot Forge, senza ricerca sul sistema host.
La CLI collega il consumer all'assemblaggio transazionale. Test di versioni,
file alterati/aggiuntivi e percorsi MSI; suite completa 118 test superati.
Il nuovo sysroot usa realmente i CRT scaricati, insieme a SDK/Clang verificati
locali: 5.891 file, 804.071.496 byte. Probe ABI nativo superato (6 controlli,
38.44 secondi), stessa DLL sintetica delle prove precedenti. Restano acquisizione
SDK completa, integrazione GUI/licenze e qualificazione pulita/full-title.

## R02: acquisizione completa CRT/SDK da metadati locali

Acquisiti online e verificati tutti gli 11 componenti CRT/SDK necessari dal piano
ricavato dai metadati VS installati, con provenienza esplicita. Corretto il
planner che includeva ARM64 nel target x64; conservati gli header condivisi
denominati x86. Estrattore MSI tollera brevemente errori Windows di condivisione
durante pubblicazione, senza ignorare errori persistenti. Suite: 121 test passati.
Ricevuta in `I:/oot3dre_work/triaevum-full-components/components.json`.
La risoluzione del catalogo remoto e la qualificazione GUI/PC pulito restano aperte.

## R02: prova nativa del sysroot interamente acquisito

Il sysroot ottenuto dai pacchetti CRT/SDK scaricati supera i sei controlli ABI
nativi in 36,81 secondi, generando la stessa DLL sintetica precedente. Nessun
header/libreria SDK ricopiato dall'installazione host in questa prova.
Implementato coordinatore `prepare_windows_toolchain.py`: acquisizione,
assemblaggio e probe prima della pubblicazione della generazione, conservando
metadati/licenze dei componenti. Test negativi verificano mancata pubblicazione
su probe fallito o identita' divergente. Il coordinatore completo e la GUI
richiedono ancora qualificazione; la prova sintetica non certifica il titolo.

## R02: coordinatore verificato end to end e collegamento Forge

Il comando completo prepare_windows_toolchain termina con native_probe_passed
e pubblica la generazione solo dopo compilazione/caricamento reali. Ricevuta:
`I:/oot3dre_work/triaevum-toolchain-generation-proof/toolchain.json`.
Collegato sysroot esplicito attraverso build-title fino al backend; il worker
GUI lo passa identico a preflight e build. Suite: 123 test superati. Restano
interfaccia acquisizione/consenso, selezione automatica, catalogo pulito e prova
full-title. Il successo del coordinatore non chiude da solo R02 o il rapporto.

## R02: selezione automatica della generazione privata

Il worker Forge seleziona `<data-root>/toolchain` senza richiedere un nuovo campo
all'utente. `prepared_toolchain.py` verifica i sei risultati nativi, hash di
compilatore/supporto e inventario SDK; una generazione presente ma invalida non
viene ignorata. Il sysroot esplicito conserva precedenza. Verificato sulla vera
generazione `triaevum-toolchain-generation-proof`; coperto il routing automatico
nel test del worker. Restano UI acquisizione/licenze e qualificazione pacchetto.

## R02/R05: probe disponibile nel Forge distribuito

Corretto il probe nativo che usava ancora il percorso sorgente anziche' il root
del bundle; incluso whole_aot_abi_probe.cpp nei dati PyInstaller. Il comando
`TriAevumForge.exe verify-toolchain` espone la stessa verifica dal prodotto.
Costruito EXE in `I:/oot3dre_work/triaevum-forge-toolchain-proof` e avviato il
probe da quella directory, senza usare lo script Python del repo. Sei controlli
passati in 44,31s, stessa DLL sintetica. Ricevuta e log:
`I:/oot3dre_work/triaevum-frozen-toolchain-probe`.
LLVM/support/sysroot sono ancora input privati espliciti; non e' una prova
su macchina pulita, ne' del titolo completo. UI acquisizione resta aperta.

## R02: acquisizione collegata alla GUI con consenso esplicito

Implementato il contratto `forge/toolchain-setup.json` e il relativo loader
isolato: percorsi confinati, hash di piano/licenze, versioni esplicite. La GUI
mostra il testo completo con Accept/Cancel; il worker accetta solo il consenso
per lo stesso descriptor, prepara e seleziona il sysroot prima della ROM.
Test coprono il routing e il rifiuto del consenso assente/divergente. Suite 127
test passati, piu' riesecuzione dei test worker dopo ampliamento della copertura.
Il pacchetto pubblico non contiene ancora il descriptor e le licenze qualificati:
non viene dichiarata acquisizione automatica funzionante nella candidate attuale.
Resta verifica visiva/end-to-end del nuovo flusso nel Forge congelato.

## R02/R04: header Clang nella release

Individuata dipendenza non distribuita: la release portava il builtins LLVM,
ma non gli header resource Clang richiesti dall'assemblaggio sysroot. Il packager
ora enumera esplicitamente `lib/clang/22/include` da LLVM e li colloca sotto
`forge/clang/include`, con ruolo allowlist dedicato. Verificato il layout reale:
300 file (header, wrapper e metadati Clang); nessun SDK Microsoft incluso.
Suite: 128 test passati. La candidate precedente non viene modificata.
Il profilo setup pubblico e il testo licenze pertinente restano da qualificare.

## R02/R04: profilo setup collegato al packager

`prepare_release.py --toolchain-setup` ora valida il profilo prima della build e
include esattamente descriptor, piano e testo licenze tramite ruolo allowlist
dedicato. Rifiuta riferimenti installed_metadata privati e layout Clang diverso
da quello distribuito. Nessun archivio o file SDK viene incluso implicitamente.
Suite: 129 test superati. L'opzione e' implementata, ma nessun profilo/licenza
fittizi sono stati inseriti nella release: qualificazione dei contenuti e prova
end-to-end GUI del pacchetto rimangono aperte.

## R08: interrompere la coda dopo un errore di compilazione

Il precedente executor.map accodava tutte le unita' e il context manager
attendeva il completamento anche dopo un errore: centinaia di compilazioni
potevano continuare inutilmente. `bounded_build.py` mantiene al massimo `jobs`
future, controlla gli errori del gruppo completato prima di avviare altro lavoro
e cancella le future non ancora partite. Le compilazioni gia' in corso vengono
attese prima di rilasciare lock/staging; gli oggetti riusciti restano riutilizzabili.
Test con errore nella prima unita' di 1.000: partono al massimo i tre worker,
non tutta la coda. Questo non risolve ancora la terminazione dei processi figli
quando l'utente chiude Forge, che resta da implementare e verificare.

## R08: isolamento del worker GUI e processi discendenti

Preparazione spostata dal thread GUI a un processo install-worker con protocollo
JSON di avanzamento. Il worker Windows si associa a un Job Object kill-on-close
prima di avviare strumenti: chiusura/terminazione del worker chiude anche i
processi discendenti. GUI e gioco avviato sono fuori dal job. La GUI aspetta
l'uscita prima di pubblicare l'evento conclusivo, ed elimina il worker alla
chiusura confermata, anche nella corsa tra chiusura e creazione del processo.
Test Windows reale: worker con figlio in attesa, uccisione del worker e verifica
EOF della pipe ereditata dal figlio. Passato. Verificato anche protocollo errore
del worker tramite CLI sorgente. Suite completa: 133 test superati.
Resta verifica nel Forge congelato e interruzione/ripresa durante una vera build
del titolo; gli staging interrotti possono restare su disco, non vengono attivati.

## R08: proprietario GUI e worker congelato

Il worker monitora ora direttamente il PID della GUI tramite un handle Windows
SYNCHRONIZE. Quando il proprietario termina, il worker esce e il Job Object
chiude i discendenti: questo copre anche il processo intermedio PyInstaller
one-file, senza assumere che uccidere il bootloader termini automaticamente
l'interprete interno. Test reale proprietario/worker superato, suite 134 test.
Creato `I:/oot3dre_work/triaevum-forge-worker-proof/TriAevumForge.exe`; comando
install-worker con owner PID reale restituisce stage/error JSON e termina con
codice 1 quando il runtime manca, senza traceback. Non e' una prova di build
titolo: quella e l'interruzione/ripresa nel pacchetto completo restano aperte.

## Candidate r7: pacchetto aggiornato e avvio reale

Prodotta `I:/TriAevum-0.5.1-candidate-r7` dal commit `c8cddb083`, attraverso il
comando prepare_release completo. 337 file pubblici, 281.165.035 byte; audit
allowlist superato. Include nuovo Forge/worker, probe congelato e header LLVM.
Non include SDK Microsoft, plugin privato o profilo di acquisizione/licenze.
La build host incrementale ha aggiornato l'identita' e relinkato, senza nuova
compilazione delle unita' del titolo. Candidate r6 lasciata invariata.

Eseguito validate_product_run con il plugin privato precedente su copia privata
del pacchetto: 30 secondi di run richiesta, 37,67 secondi wall complessivi,
1.707 frame misurati in 28,7363s dopo 60 frame di warmup, 59,40fps.
VSync/pacing/limiter attivi: verifica a 60Hz, non throughput massimo.
TopScreen e x2 attivi, 133.591 draw interpolati. Framebuffer catturato dal runtime
e ispezionato: title intro con Link/Epona, terreno e cielo visibili.
Evidenze `I:/oot3dre_work/triaevum-release-artifacts-r7/product-run`.
Non certifica F1 interattivo, x3, ROM-to-plugin a freddo, worker interrupt/resume
o PC pulito. Queste prove restano necessarie per completare il rapporto.

## Candidate r7: x3 e throughput nativo separati

Esteso validate_product_run con `--multiplier 1|2|3` e `--throughput` (solo x1).
Il test modifica esclusivamente la configurazione della propria copia privata;
verifica moltiplicatore/interpolazione effettivi e, per throughput, tutti i limiti
realmente disattivati nel report. I default del prodotto restano invariati.

- x3 real-clock: 2.606 frame / 29,2519s dopo warmup = 89,09fps; VSync off,
  pacing/limiter a 90Hz attivi; 202.344 draw interpolati. Framebuffer ispezionato.
- throughput x1: 2.823 refresh / 29,2873s = 96,39 refresh nativi/s; VSync,
  pacing e limiter tutti off, interpolazione off. Ogni frame avanza un passo
  nativo fisso di 1/30s: misura capacita' di elaborazione, NON velocita' di gioco.

Entrambe run da 30s sulla stessa candidate/plugin, con 60 frame esclusi per warmup.
Evidenze in `triaevum-release-artifacts-r7/product-run-x3` e
`product-throughput-x1`. Il throughput attraversa piu' tempo simulato, quindi
non e' un confronto pixel A/B allineato. Conteggi dei draw e singoli framebuffer
non dimostrano ancora la diversita' pixel di ogni campione interpolato: quella
verifica e la matrice su gameplay/cutscene restano aperte.

## Candidate r7: sequenza pixel x3

Catturati dal framebuffer 60 frame consecutivi (host 600..659) nella title intro
x3, usando la cattura sequenziale esistente. Analisi dei soli pixel RGB, esclusi
header/padding/alpha: 56 immagini distinte e 4 coppie adiacenti identiche su 59
(indici 8,9,10,11 della sequenza). Il risultato esclude una sequenza interamente
duplicata ma NON attribuisce ogni immagine a un campione nativo/interpolato.
La cattura sincrona puo' alterare il pacing; non usarla come benchmark.
Report `product-run-x3/x3-sequence-state.json`, immagini `x3-sequence_*.bmp`,
analisi `x3-pixel-analysis.json` sotto gli artefatti r7. I contatori cumulativi
mostrano numerosi repeated/clamped samples: serve associazione per-frame prima
di concludere sul corretto comportamento di ogni campione.
`analyze_frame_sequence.py` rende ripetibile il controllo senza dipendenze imaging;
test coprono padding/alpha e contenuti malformati. Suite: 136 test superati.

## R07/R08: separazione cache sorgenti e richiesta di link

Il builder annidava la cartella dei sorgenti generati sotto la request identity,
che comprende anche linker, SDK, support library e script Forge. Queste variazioni
creavano una nuova cartella anche a codice/translator invariati. Ora la cartella
dipende soltanto dall'identita' di generazione gia' calcolata. La request identity
continua a invalidare correttamente il plugin; le dipendenze native continuano a
invalidare oggetti e archivio. Non viene indebolita la verifica degli hash.

Un lock del kernel protegge generazione e lettura per la compilazione, impedendo
producer concorrenti sulla stessa cartella; si libera anche dopo errore. Test:
cambio linker/header/support mantiene la cartella, cambia il plugin quando dovuto,
errore di compilazione seguito da ripresa, corruzione DLL rifiutata. Suite completa:
136 test superati in 11,339 secondi.

Probe nativo reale in `I:/oot3dre_work/triaevum-generation-cache-proof`:
prima run 49,741 s, seconda 29,173 s con build_status=reused; entrambe superano
aritmetica, memoria, callback, TLS, uscita osservabile e rifiuto ABI incompatibile.
DLL identica: `85d69baa913c642b5bc01d8e73c9d24a23f2b2513741580896b448f70f27d271`.
Questi tempi includono validazione sysroot e ricompilazione dell'host del probe:
non sono tempi di preparazione dell'intero titolo e non misurano il risparmio
specifico dopo cambio linker. Quel caso e' attualmente coperto dal test isolato.
Il candidate r7 non incorpora ancora questa modifica Python a Forge.

## R02: aggiornamento della prova ABI senza riacquisire l'SDK

Il pacchetto r7 contiene una support library con hash `8a4933ee...`, diverso dal
probe precedente (`16667977...`). La selezione della toolchain correttamente
rifiutava la prova precedente, ma non offriva recupero dopo un aggiornamento.
`select_sysroot` ora accetta un callback di nuova prova, usato dalla GUI: prima
verifica l'integrita' del sysroot, poi ricompila/esegue il probe contro i binari
aggiornati. Pubblica atomicamente soltanto una prova completa con hash corretti,
preservando acquisizione/licenze; un errore mantiene il receipt precedente.
Lock dedicato, nessun download e nessuna nuova prova su aggiornamenti invariati.

Verifica reale nella copia privata `triaevum-full-title-proof/data/toolchain`:
35,583 s, sei controlli ABI superati; receipt in
`data/diagnostics/toolchain-upgrade-proof/receipt.json`. Questa prova usa gli
strumenti/support library r7 e il codice di validazione del checkout, non il
nuovo frontend frozen (ancora da ricreare). Suite completa: 136 test superati.

## Prova completa dalla ROM fornita

Vedere [prova completa del titolo](TRIAEVUM_FULL_TITLE_BUILD_PROOF_2026-09-05.md):
estrazione reale della `.cci`, indice senza seed, 257 oggetti nuovi, DLL generata
dal Forge r7 e avvio reale con TopScreen/x2. Builder a freddo 682,214 s, a caldo
28,731 s; intro misurata a 59,402 presentazioni/s con clock reale e limitatori.
Corretto nel checkout anche il report dei costi di cache hit: non deve ristampare
i contatori di compilazione della prima build come lavoro della run corrente.
La prova non sostituisce la qualificazione GUI/macchina pulita ancora aperta.

## Candidate r8: ultime correzioni incluse e avvio verificato

Pacchetto immutabile `I:/TriAevum-0.5.1-candidate-r8`, sorgenti `db241bc5b`:
337 file pubblici, 281175376 byte. Include separazione della cache di generazione,
recupero della prova toolchain dopo aggiornamento e metriche cache-hit corrette.
Build incrementale del runtime (identita' prodotto e relink), nessuna nuova
compilazione delle 257 unita' del titolo. Forge frozen e source archive ricreati.
L'audit del pacchetto passa; rimane un candidate, non una release qualificata.

`validate_product_run.py` ha avviato una copia privata con la nuova DLL della
prova completa da ROM (`106aa7a6...`). Uscita 0, TopScreen/x2 effettivi, 134671
draw interpolati e framebuffer non uniforme ispezionato (Link/Epona e scena).
Evidenze in `I:/oot3dre_work/triaevum-release-artifacts-r8/product-run`.
Non usare i tempi di questa run come benchmark controllato: la suite Python si
e' parzialmente sovrapposta all'esecuzione. I 136 test sono superati; il benchmark
separato della prova completa r7 rimane il riferimento prestazionale di questa fase.

WindowsSandbox.exe non risulta disponibile tramite Get-Command su questa macchina;
nessuna feature di sistema installata o riavvio forzato. La prova Windows pulito
resta da eseguire. Richiesta conferma utente di F1/persistenza/audio nell'installazione
privata completa. Acquisizione SDK pubblica con termini applicabili, qualificazione
GUI end-to-end e matrice grafica/gameplay non sono dichiarate completate.

## R03: attribuzione reale dei pixel ai campioni x2/x3

Il modulo screenshot pubblica ora un sidecar `<capture>.bmp.json` con il contratto
temporale condiviso 3DS gia' passato al renderer: sorgenti precedente/corrente,
epoch, alpha, ordinale, moltiplicatore e reset. Il window host osserva soltanto
l'ultimo campione eseguito nella presentazione; dove non e' disponibile scrive
null. Nessuna modifica a shader, scheduling, clock, gameplay o formati del titolo.
La serializzazione avviene soltanto per le catture richieste.

Build nativa incrementale riuscita. Evidenze private in
`I:/oot3dre_work/triaevum-temporal-capture-proof`. Prime prove a clock reale:
il readback sincrono rende quasi ogni cattura un nuovo frame sorgente, anche
a 400x240; quindi non qualificano le coppie/terne intermedie. La prima prova
restava a 1280x720 per la configurazione persistita, poi corretta nella sola
configurazione diagnostica, senza cambiare i default utente.

Usato il delta diagnostico gia' esistente, non una nuova temporizzazione:

| Modalita' | Delta host diagnostico | Catture | Intervalli completi | Tutti gli ordinali con pixel distinti |
| --- | --- | ---: | ---: | ---: |
| x3 | 1/90 s | 90 | 30 | 30 |
| x2 | 1/60 s | 90 | 44 | 44 |

Frame host 600..689, 400x240. `fixed-analysis.json` (x3) e `x2-analysis.json`
associano hash pixel e metadata reali. x3: 90 immagini distinte; x2: 88 immagini
distinte, 89 catture attribuite, con reset/non-attribuzione esclusi dagli intervalli.
Il lettore verifica dimensioni/schema e non mescola epoch o frame sorgenti.
Test coprono un intervallo completo e metadata mancanti oltre alle prove BMP.

Questo dimostra campioni intermedi distinti nel tratto catturato, non la fedelta'
di ogni attore, la continuita' di ogni cutscene o il pacing reale a 90 Hz. Non usare
i tempi di queste run come benchmark. Il candidate r8 e' immutabile e non contiene
ancora questi sidecar; la prova usa il runtime appena compilato del checkout e la
DLL completa da ROM gia' validata.
