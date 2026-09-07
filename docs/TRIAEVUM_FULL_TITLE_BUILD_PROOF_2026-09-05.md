# Prova completa del titolo da ROM

## Perimetro

Prova locale del 5 settembre 2026, non certificazione Windows pulito o release
pubblica completa. ROM originale letta soltanto; nessuna modifica a Zelda3drecomp.
Installazione privata: `I:/oot3dre_work/triaevum-full-title-proof`.
Runtime/Forge copiati dal candidate r7 immutabile. Nessuna vecchia DLL del titolo
o cache di compilazione importata nella nuova installazione.

- ROM: `I:/Zelda3drecomp/oot3d.cci`, 483672064 byte.
- SHA256 ROM: `d4670c962a6dd5c9cb953de63e6316b1b03214d78ce3fd52b75cca6bcfe6aef2`.
- Forge: `712a2bf0b704c3c368ca1bac50be28bfa6a0025ce3a0e81a3af69e8274d8fcd8`.
- Toolchain privata gia' acquisita: `I:/oot3dre_work/triaevum-fully-acquired-sysroot`.
- Sysroot: `3ab0946f30d5d528336687428cd0e9cc24ed7d733e88ec56ee28804ca16c160c`.

L'estrazione e' stata eseguita con `ctr_rom.extract_decrypted_rom` e
`publish_extracted_inputs` del checkout; verifica/preparazione con Forge CLI del
checkout; compilazione/link/pubblicazione con **TriAevumForge.exe r7**.
Non e' ancora la prova di un unico click GUI comprensivo di acquisizione SDK.

## Risultato della preparazione

Revisione `oot3d-eur-project-baseline-16a6b0aa`, hash dei tre file estratti uguali
alla revisione supportata. Contenuto sotto `data/sources/67c1892539bc8f65eabf38a06bcf03f224bcddc6f723a6539868f00014bc0d40`.

Indice strutturale: **seeded=false**, 12422 funzioni censite, 12419 selezionate,
161341 blocchi, 918019 istruzioni uniche. Receipt `source-backend.json` sotto
`data/translator-cache/structural-ir/99b0ba02377bfe69ffea185e9285b7b9398165d247db53c08bd4a2ffa76e9e5d`.

Prima compilazione: 256 shard, 257 oggetti compilati, zero riutilizzati. Builder
C++/link: **682,214 s** (11 min 22 s), esclusa preparazione dell'indice strutturale.
Due compilazioni concorrenti; ThinLTO usa il proprio parallelismo. Picco residente
osservato del linker circa 1,71 GB: campionamento, non peak RAM dell'intero albero.

DLL prodotta: 85852160 byte, SHA256
`106aa7a6b1be71b2f2cee9c0f4baf2a3f0d43a3c4a1443e84ff365837c536549`.
Receipt persistente `whole-aot-plugin.json` sotto
`data/translator-cache/whole-aot-v2/plugins/4f1a86b84fab4105939e52e0a795740bf1e6baa6c02044842c2b8d44006fdc64`.
Pubblicazione transazionale e attivazione completate, profilo relativo creato.

Seconda esecuzione identica: **early_cache_hit=true**, DLL e modulo riutilizzati,
builder **28,731 s**. Il r7 ristampa i contatori storici 257/0 anche su cache hit:
non sono compilazioni della seconda run. Il checkout corregge questo report,
conservando separatamente la provenienza e azzerando i costi oggetto dell'early hit.

## Avvio della nuova DLL

Comando dalla directory privata:

```powershell
./TriAevum.exe --launch-profile ./TriAevum.launch.json --frames 0 --max-seconds 30 --output ./fresh-build-runtime.json --screenshot ./fresh-build-framebuffer.bmp --screenshot-start-frame 600 --benchmark-warmup-frames 60
```

Uscita 0. Framebuffer 1280x720 ispezionato: intro Hyrule Field con Link/Epona,
terreno e luna visibili. Report: TopScreen attivo; simulazione 30 Hz; x2 attivo;
133682 draw interpolati. Finestra dopo 60 frame warm-up: **1708 presentazioni /
28,753 s = 59,402 presentazioni/s**, clock reale, VSync/pacing/limitatore attivi.
Nessuna configurazione grafica sostituita manualmente per ottenere questo risultato.

Questa prova chiude la lacuna della DLL completa mai ricompilata da questi input
nel percorso r7. Non dimostra ancora F1 interattivo, qualita' audio, tutte le scene,
campioni x2/x3 corretti per ogni intervallo, import GUI su Windows senza strumenti
installati, ne' il workflow pubblico automatico di acquisizione della toolchain.

Gli artefatti privati citati non devono entrare nel pacchetto distribuibile.
