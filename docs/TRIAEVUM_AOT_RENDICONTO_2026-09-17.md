# Rendiconto del collegamento diretto AOT

Data: 17 settembre 2026. Implementazione: commit `ecea4ca`.

## Obiettivo

Ridurre il tempo di elaborazione prima del renderer NRI, eseguendo gruppi di
funzioni AOT con meno controlli ripetuti e meno passaggi attraverso il runtime.
Il criterio era ottenere un beneficio misurabile nel gioco, non soltanto nei
test isolati. Il traguardo generale del -20% non e' stato raggiunto.

## Cosa ho fatto e come

Il precedente adattatore permetteva di eseguire le funzioni specializzate sullo
stato reale del gioco, ma introduceva costosi passaggi di uscita e rientro tramite
eccezioni. Ho realizzato un prototipo che chiama invece il gruppo direttamente
dall'AOT, condividendo memoria, registri e budget di esecuzione del chiamante.

Ho scelto un gruppo pilota di **22 funzioni**, relativo ai controlli geometrici
lungo un segmento, con ingresso `003723C0`. I controlli necessari al runtime
restano rispettati: quando nel gruppo sono presenti callback attivi, viene
usato il percorso originale. Non sono state introdotte correzioni per la scena.

Per compilare ho ricostruito un solo blocco sorgente, riutilizzando **256 oggetti
compilati** dopo verifica di hash, dipendenze e compilatore. Ho preparato due
varianti confrontabili: controllo e candidato, generate con lo stesso strumento.
Ho inoltre corretto l'associazione al manifest della cache: quello inizialmente
indicato non corrispondeva agli oggetti disponibili.

## Come ho verificato

- **48 test automatici superati**, inclusi cinque nuovi test del generatore.
- **Due serie ABBA**, cioe' controllo/candidato/candidato/controllo: otto run
  complessive dello stesso salvataggio, con input invariati.
- Ogni run comprende 360 frame; la misura considera solo gli ultimi 180, dopo
  il riscaldamento. Interpolazione, Vsync e limiti di presentazione disattivati.
- Le impronte finali di memoria e processo coincidono in tutte le otto run,
  anche con il riferimento precedente.
- Un campionamento separato conferma l'esecuzione reale delle funzioni
  specializzate: 19 osservazioni dentro i loro corpi. Questa run diagnostica
  non e' stata usata per misurare il guadagno prestazionale.

## Risultati

Tempo medio per frame prima di NRI; valori inferiori sono migliori:

| Confronto | Controllo | Candidato | Variazione del tempo |
| --- | ---: | ---: | ---: |
| Prima serie | 8,224 ms | 8,928 ms | +8,55%, peggiore |
| Seconda serie | 9,010 ms | 8,957 ms | -0,59%, sostanziale parita' |

**Non emerge un miglioramento ripetibile.** Anche il controllo varia tra le
serie, quindi non attribuisco una percentuale precisa di peggioramento al
prototipo. Il beneficio osservato in precedenza nei test isolati non si traduce
in un vantaggio dimostrato sul tempo complessivo del gioco.

La preparazione iniziale delle due varianti ha richiesto **465,67 secondi**:
ThinLTO ha rigenerato codice macchina anche per oggetti sorgente riutilizzati.
Il successivo collegamento senza modifiche ha richiesto circa due secondi,
ma questo non rappresenta il costo di una nuova modifica al codice.

## Stato finale e limiti

Il collegamento diretto funziona nello scenario provato, ma **resta un esperimento
disattivato**. Eseguibile distribuito, renderer e configurazione normale non sono
stati modificati. Non ho esteso questa variante alle 303 funzioni precedentemente
selezionate: sarebbe scorretto presentare tale superficie come gia' integrata o
interamente verificata. Non e' stata effettuata una validazione Linux.

Prima di un'estensione servono misure del costo complessivo realmente eliminabile
nei gruppi, senza contare due volte le funzioni condivise. Restano da distinguere
i costi dei controlli d'ingresso dagli effetti dell'inlining e della disposizione
del codice; questi test non ne identificano ancora la causa dominante.

## Riferimenti

- Generatore: `tools/renderer/tev_program/build_aot_direct_cohort.py`.
- Test: `tools/renderer/tev_program/test_aot_direct_cohort.py`.
- [Rapporto tecnico e dettagli delle misure](TRIAEVUM_AOT_DIRECT_COHORT.md).
- Evidenze locali: `C:/Users/xander/triaevum-aot-causal-analysis/`, sottocartelle
  `direct-line-v2`, `direct-line-v2-abba`, `direct-line-v2-abba-repeat` e
  `direct-line-v2-sampling`.
