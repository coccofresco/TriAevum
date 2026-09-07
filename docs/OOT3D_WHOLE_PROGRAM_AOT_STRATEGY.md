# Strategia Whole-Program AOT OOT3D

## Decisione

Il percorso di produzione non deve ampliare l'attuale emettitore
`generate_true_aot.py`. Quel generatore resta una fixture differenziale per
regioni foglia. Il nuovo percorso compila staticamente funzioni e call graph
OOT3D tramite un IR di programma, senza `PackedOp`, decoder runtime, callback a
ogni basic block o accessi virtuali a `MemoryBus` nel percorso RAM ordinario.

L'ordine autoritativo e:

1. sorgente OOT3D C/C++ completo e verificato, compilato direttamente;
2. traduzione AOT del residuo ARM da `code.bin`;
3. interprete corrente soltanto nelle build diagnostiche e nell'oracolo
   differenziale.

L'unita runtime e una funzione OOT3D completa. L'unita di build e uno shard di
funzioni correlato dal call graph. Le due granularita non devono coincidere.

## Evidenza Che Impone Il Cambio

La title intro a 600 refresh, 4K Vulkan e interpolazione disattivata mantiene
come baseline circa `6,64 s` guest, 18.305 draw, PCM FNV-1a
`14397512309130401104` e framebuffer SHA-256
`DF8375ED116DEBE8DAD86BC400EFD4AABD8F37E8228316D7A2B3D1D2789C5B7F`.

Le prove del 18 luglio 2026 hanno mostrato:

- `MemoryArena_AllocateAlignedBlock`, 63 istruzioni, non produce un vantaggio
  misurabile (`6,661 s` contro `6,638 s` medi);
- `PicaCompare_ConvertFunction`, 33.750 ingressi aggiuntivi, non migliora la
  baseline;
- `CmbRenderer_UpdateMeshTransforms`, 131 istruzioni e sei callsite, resta
  bit-identica ma porta il guest a `7,787 s`;
- il tentativo di richiamare direttamente i consumer host riduce i rientri nel
  dispatcher ma porta la stessa run a `8,655 s`.

Il problema non e la dimensione del blocco. Il lowering conserva un array di
registri guest, una condizione runtime per istruzione, accessi virtuali alla
memoria e callback fra caller e callee. Ingrandire quel modello aumenta la
copertura nominale senza trasformarlo in codice host ottimizzabile.

## Front-End Da Conservare

Il nuovo compilatore riusa gli artifact gia versionati:

- `arm_ir.py` per decodifica e semantica core/VFP;
- `a32_cpp_aot.py` per literal recovery, raggiungibilita, block start e
  partizionamento del testo;
- `codebin_function_inventory.csv` per confini, simboli e call graph;
- `codebin_callable_boundary_residue_audit_166.csv` per escludere entry non
  affidabili;
- `runtime_discovered_entries.csv` per entry osservate ma non ancora presenti
  nell'inventario;
- primitive binary32/binary64 esatte del runtime A32 corrente;
- test e hash della title intro come oracolo.

Il front-end produce un nuovo artifact backend-neutral, non array di opcode:

```text
AotProgram
  functions[]
    guest entry/end, hash e simbolo
    basic blocks e archi CFG
    direct calls, tail calls e target address-taken
    literal/data references
    istruzioni normalizzate con operandi espliciti
    use/def registri, CPSR, FPSCR e memoria
    exit SVC, indirect, fault e return
```

## Lowering Host

### Registri E Control Flow

Ogni funzione generata carica una volta i registri ARM usati in variabili
locali. Branch, join e loop diventano control flow C++ diretto. CPSR usa flag
lazy: N/Z/C/V vengono materializzati solo se una successiva istruzione li
consuma o prima di un confine osservabile.

Un `BL` con target noto diventa una normale call host al simbolo della funzione
OOT3D. Prima della call vengono sincronizzati soltanto registri caller-saved,
SP, flag e VFP realmente live; i callee-saved restano locali e vengono
verificati contro l'ABI ARM. `BX LR` diventa un return host. Tail call note
diventano tail call host.

Target indiretti e callback conservano nella memoria guest il loro indirizzo
ARM. Una tabella statica indirizzo -> funzione host li risolve senza decoder.
Entry interne address-taken ricevono un wrapper dedicato; non obbligano a
frammentare il corpo principale.

### Memoria Guest

Il processo x64 riserva uno spazio virtuale contiguo di 4 GiB. Un indirizzo
guest RAM ordinario diventa `guest_base + uint32_address`; le regioni del
manifest vengono committate e protette senza copiare i dati a ogni accesso.

Il lowering distingue staticamente:

- RAM/VRAM/code leggibile: load/store host diretto con helper unaligned
  definiti;
- pagine con permessi o fault osservabili: fast check di pagina;
- MMIO e operazioni esclusive: helper tipizzato;
- SVC/IPC: uscita esplicita verso i servizi CTR esistenti.

Il backend non puo chiamare l'interfaccia virtuale `MemoryBus` per ogni word.
`NativeA32Memory` resta l'oracolo diagnostico e fornisce la mappa con cui creare
la vista veloce.

Il primo livello implementato non modifica il dispatcher esistente: espone a
codice AOT `ReadFast<T>` e `WriteFast<T>` non virtuali tramite una page table
sparsa a due livelli. Le leaf vengono allocate soltanto per i gruppi guest
effettivamente mappati; una pagina interamente coperta usa direttamente il
puntatore host, mentre boundary, protezioni e mapping parziali ricadono nel
percorso controllato esatto. La riserva contigua di 4 GiB resta un secondo
incremento opzionale: deve sostituire questa vista soltanto se le misure sulla
prima closure dimostrano che il doppio lookup e ancora rilevante.

### VFP

Trasporti e operazioni con comportamento IEEE host equivalente vengono emessi
direttamente. Le primitive esatte esistenti restano per rounding non standard,
default-NaN, flush-to-zero, eccezioni e conversioni non rappresentabili in
modo affidabile dal compilatore host. La decisione e per operazione e deriva
dai bit FPSCR, non da profili di scena.

### Sorgente Decompilato E Override

Ogni indirizzo funzione ha un unico simbolo host stabile. Se esiste sorgente
OOT3D verificato, lo shard AOT non emette il corpo e collega un adapter C/C++
con la stessa ABI guest. Una successiva modifica a una funzione sostituisce
solo quel simbolo e il suo object file.

Il sorgente N64 puo fornire offline nomi, tipi, intenti e struttura logica da
confrontare con binario e dati OOT3D. Non puo fornire automaticamente il corpo
runtime: durata, costanti, ordine, chiamate, backend e differenze OOT3D vincono
sempre. L'unica eccezione di prodotto resta la UI N64 delimitata dal contratto
dedicato.

## Backend E Build

Il primo backend emette C++ function-level perche usa il toolchain gia
operativo e permette confronti rapidi. Non emette una funzione per istruzione:
usa locali, `goto` per CFG irriducibili, call host e helper semantici tipizzati.

L'IR resta indipendente dal backend. Se il backend C++ non raggiunge il budget
su una closure rappresentativa, si aggiunge un emitter LLVM IR che produce
object file AOT; non si modifica nuovamente il front-end. LLVM e preferibile per
SSA, lazy flags, register promotion e code generation massiva, ma introdurlo
prima di validare l'IR aumenterebbe dipendenze e tempo di bootstrap.

Gli shard sono costruiti per SCC/call-graph cluster e dimensione massima, non
per intervalli arbitrari. Ogni shard ha una chiave composta da hash dei byte
funzione, versione IR, versione emitter e override sorgente. Cambiare una
funzione non rigenera ne ricompila l'intero `code.bin`. ThinLTO e facoltativo e
si abilita solo se il vantaggio supera il costo di linking.

## Alternative Valutate

| Strategia | Valutazione |
| --- | --- |
| Ampliare `generate_true_aot.py` | Scartata per produzione: misure negative e modello ancora dispatcher-like. |
| Riscrivere tutto manualmente in C++ | Ottima manutenibilita finale, ma troppo lenta e soggetta a divergenze per arrivare rapidamente al gioco completo. |
| Tradurre il C N64 e adattarlo | Scartata: ricostruirebbe un altro gioco e perderebbe differenze OOT3D. Utile solo come evidenza semantica. |
| Compilare il C/C++ OOT3D disponibile | Priorita massima per ogni funzione completa e verificata. |
| Whole-program AOT C++ da ARM | Primo backend raccomandato, se usa IR, locali, call dirette e fast memory. |
| Whole-program LLVM AOT | Backend successivo raccomandato se C++ non offre margine sufficiente. |
| Dynarmic/JIT o cache JIT serializzata | Utile come oracolo prestazionale, non conforme al prodotto nativo senza CPU JIT. |
| Interpreter ARM | Solo diagnostica; non e una soluzione prestazionale o di produzione. |

### Traduttori Esterni

Un traduttore esterno puo accelerare il lowering delle istruzioni, ma non puo
decidere al posto nostro ABI CTR, memoria guest, SVC/IPC, PICA o confini tra
sorgente verificato e residuo binario.

- [rev.ng](https://github.com/revng/revng) e il solo candidato da sottoporre a
  un pilot limitato: supporta ARM, usa un modello di programma modificabile e
  puo produrre LLVM con una funzione per funzione input. Il pilot deve operare
  su un ELF-envelope generato da `code.bin`, non su una ricostruzione manuale
  del gioco.
- [Remill](https://github.com/lifting-bits/remill) non e oggi una fondazione
  di produzione per questo progetto: il supporto AArch32 risulta ancora in
  sviluppo e Windows e indicato come sperimentale.
- [McToll](https://github.com/microsoft/llvm-mctoll) e orientato ad ARM32
  Linux/ELF e non offre direttamente il contratto necessario al raw image 3DS
  e ai suoi servizi.
- [RetDec](https://github.com/avast/retdec) e Ghidra restano strumenti di
  recupero sorgente, tipi e prove; il C decompilato non diventa automaticamente
  semantica eseguibile autoritativa.
- QEMU/Azahar restano riferimenti per semantica ARM/PICA e confronto, non il
  runtime finale AOT.

Il pilot rev.ng e accettato soltanto se tre funzioni rappresentative (CFG
integer, accessi memoria/switch e VFP) diventano object file collegabili alla
nostra ABI, passano i test differenziali di stato e memoria e non conservano un
dispatcher o un helper QEMU per istruzione nell'IR ottimizzato. Il pilot non
blocca l'emitter interno: se richiede modifiche invasive al lifter o non rende
sostituibile il modello memoria, viene chiuso e si continua sullo stesso
`AotProgram` con backend C++/LLVM proprietario.

### Uso Del Sorgente N64

Una traduzione totale del C N64 non e una scorciatoia valida. Anche con funzioni
semanticamente simili introdurrebbe costanti, timing, layout, asset contract e
backend del gioco sbagliato. Il suo impiego massivo ammesso e offline:

1. associare simboli OOT3D a candidati N64 tramite call graph, stringhe e dati;
2. proporre tipi, strutture, invarianti e nomi di campi;
3. validare ogni proposta contro accessi, layout, chiamate e comportamento del
   binario OOT3D;
4. annotare `AotProgram` o sostituire una funzione solo quando esiste sorgente
   OOT3D verificato equivalente.

Questo permette di recuperare leggibilita e semantica senza fare del port N64
la logica di gioco. La UI single-screen N64 resta l'eccezione esplicita e
delimitata dal relativo contratto.

## Piano Implementativo

1. Estrarre `AotProgram` dal front-end pinned e produrre un manifest con closure,
   callsite, target indiretti e categorie di istruzioni.
2. Aggiungere test CFG su funzioni con loop, literal pool, switch, tail call e
   entry interne.
3. Implementare la vista guest non virtuale e confrontarla byte-per-byte con
   `NativeA32Memory` su boundary, protezioni, atomiche e regioni CTR; valutare
   la riserva contigua solo dopo una misura della closure.
4. Emettere funzioni integer complete con locali, lazy flags, direct call e
   indirect resolver.
5. Integrare VFP transport/scalar e le primitive esatte gia disponibili.
6. Collegare come override le nove funzioni host gia validate.
7. Compilare in massa la closure boot + title intro, lasciando il packed
   dispatcher solo nella build differenziale.
8. Confrontare stato guest a VBlank noti, sequenza PICA, PCM e framebuffer 4K.
9. Estendere la generazione all'intera immagine raggiungibile e rendere errore
   di build ogni funzione priva di sorgente o AOT nel target release.
10. Misurare uncapped; se il tick non ha margine abbondante sopra 60 FPS,
    attivare il backend LLVM sullo stesso IR e sugli stessi test.

## Primo Incremento Accettabile

Il primo incremento non e un'altra leaf. Deve compilare almeno una SCC o una
catena di funzioni della title intro che copra il 10% dei campioni residui e:

- eseguire call dirette senza callback host;
- non leggere `PackedOp` o opcode raw durante la run;
- non usare `MemoryBus` virtuale per RAM ordinaria;
- conservare identici stato guest osservabile, 18.305 draw, PCM e framebuffer;
- ridurre il tempo guest in tre coppie alternate;
- rigenerare e ricompilare solo gli shard modificati.

Solo dopo questa prova si aumenta la copertura fino alla closure completa.

## Front-End Strutturale Implementato

Il 18 luglio 2026 il primo pass del nuovo percorso e stato implementato in
`tools/oot3d/native_a32_runtime/whole_aot_program.py`. Il wrapper operativo e
`tools/oot3d/native_a32_runtime/generate_whole_aot.py`; ricava `code.bin`,
ExHeader, process entry, text size, inventario e boundary audit dal catalogo
locale, senza leggere il checkout di decompilazione esterno.

Il comando riproducibile e:

```powershell
python tools/oot3d/native_a32_runtime/generate_whole_aot.py
```

Sul `code.bin` corrente produce
`build-codex/oot3d_whole_aot/aot_program.json` con questa partizione:

- 918.014 istruzioni raggiungibili in 161.340 basic block;
- 9.975 entry callable dell'inventario;
- 1.998 entry dimostrate da puntatori a codice;
- 133 target diretti non ancora promossi dall'inventario;
- 497 closure residue esplicite, senza attribuzione per prossimita;
- zero basic block persi;
- 12.593 closure staticamente chiuse su 12.603;
- dieci soli fallthrough condizionali verso word non classificate come codice;
- 61.706 call dirette, 3.625 tail call e 2.470 siti indiretti elencati.

Blocchi condivisi restano globali e le funzioni li referenziano: switch case,
callback address-taken e code chunk discontinui non vengono duplicati o
assegnati artificialmente all'intervallo Ghidra piu vicino. Ogni istruzione ha
una categoria semantica e operandi decodificati; il raw word resta provenance
di compilazione, non un opcode da decodificare durante il gioco.

Tre test sintetici verificano call, ritorno, loop, tail call, literal pool ed
entry address-taken. L'output e deterministico e hashato. La prima analisi
completa richiede circa 90 secondi nel tool Python corrente; il cache hit
verificato del wrapper richiede meno di un secondo e impedisce che il pass
ricada nelle normali build incrementali.

La vista memoria non virtuale per il futuro codice AOT e ora presente e coperta
da test su accessi unaligned, cross-page e read-only. Non viene instradata nel
dispatcher corrente: una prova A/B bit-identica non ha mostrato un vantaggio
affidabile, confermando che il beneficio deve provenire dal lowering con accessi
inlined e non da un altro helper aggiunto al vecchio percorso.

## Primo Lowering Function-Level

`tools/oot3d/native_a32_runtime/whole_aot_cpp.py` implementa ora il primo
backend function-level. La selezione versionata e in `whole_aot_functions.json`
e il wrapper operativo e:

```powershell
python tools/oot3d/native_a32_runtime/generate_whole_aot_cpp.py
```

Il primo shard compila tutti i 35 basic block e le 177 istruzioni di
`NativeCurve_SampleFloat` in un solo corpo C++. Lo stato attraversa il corpo in
un `Oot3dWholeAotFrame` con campi core/VFP nominati; branch e loop sono `goto`
host e la memoria ordinaria usa `ReadFast<T>/WriteFast<T>`. Le operazioni VFP
chiamano primitive binary32 nominate, non un decoder raw. La snapshot pinned
resta byte-identica: l'estensione locale compila la sua implementazione come
translation unit autoritativo e aggiunge soltanto gli entry point AOT.

Il test differenziale completo usa la stessa curva nativa, stato guest e stack
nei percorsi ARM e AOT e confronta registri, CPSR, FPSCR, lane VFP e memoria.
Nella title Vulkan 4K da 600 refresh il corpo viene chiamato 38.877 volte senza
fault o fallback; draw, PCM e framebuffer restano identici. Tre coppie
alternate misurano `8,5311 s` senza e `8,4201 s` con il nuovo corpo, pari al
`1,30%` medio. Il cache hit del backend richiede circa `0,26 s` e non riscrive
gli artifact invariati.

Questo chiude la prova architetturale su una funzione calda completa, ma non il
primo incremento accettabile: la copertura profilata e ancora `3,699%`. Mancano
il lowering delle call dirette, gli shard SCC/call-graph e una closure title
oltre il 10% verificata con lo stesso metodo.

## Closure Profilata Massiva Del 19 Luglio 2026

Il primo incremento accettabile e ora chiuso. Il profilo strutturale della title
mostra blocchi da `6,802` istruzioni medie: il `48,40%` degli ingressi esegue al
massimo quattro istruzioni e l'`82,20%` al massimo otto. Questo conferma che gli
array `PackedOp` predecodificati restano un interprete realtime e che i lookup
dei callback non sono il costo dominante.

`select_profile_whole_aot.py` verifica integralmente ogni funzione calda contro
il lowering corrente e rigenera una selezione deterministica. Il backend
supporta ora anche call host dirette, rientri AOT dopo un callee ARM, carry ARM
esatto per gli shifter logici, `EOR/TEQ/CMN/ORR`, `ADC/SBC/RSC`, `MUL/MLA` e
hint `NOP`. La selezione risultante contiene 168 funzioni, 8.380 istruzioni e
313 entry di continuazione; i 107 callee non ancora abbassabili restano confini
ARM espliciti, non adattamenti del comportamento.

La copertura nominale del profilo title sale al `52,241%`. In 600 refresh 4K
Vulkan il runtime esegue 1.459.684 ingressi whole-AOT, 1.759.066 call dirette e
764.294 call esterne con zero fault e zero uscite unsupported. Tre coppie
alternate nello stesso eseguibile misurano `6,8426 s` guest senza whole-AOT e
`5,8616 s` con whole-AOT, una riduzione del `14,34%`; il tempo processo medio
scende da `11,3534 s` a `9,9654 s`, pari al `12,22%`. Restano invariati 18.305
draw, PCM FNV-1a `14397512309130401104` e framebuffer SHA-256
`DF8375ED116DEBE8DAD86BC400EFD4AABD8F37E8228316D7A2B3D1D2789C5B7F`.

Il target supera appena 60 refresh/s includendo bootstrap e shutdown, ma non ha
ancora margine abbondante. Il passo successivo e abbassare VFP convert/load
multiple, trasferimenti doubleword, SVC con continuazione e target indiretti;
poi il profilo va rigenerato sul residuo realmente interpretato.

## Residuo Post-Closure E Promozione PGO

Il profilo residuo deterministico successivo e versionato in
`profiles/title_intro_post_whole_170_1201.json`: 1.201 refresh, 29.895.112
ingressi ARM e 467.111 campioni a 1:64. Il file conserva il 95,796% dei
campioni riportati e dimostra che il costo residuo non coincide piu con il
profilo usato per costruire la prima closure.

Il lowering risolve ora jump table `LDR pc` verso basic block interni, `BLX`
verso entry AOT selezionate e rientri dopo target indiretti. La coorte completa
con siti indiretti e risultata regressiva ed e quindi esclusa dalla selezione
predefinita, pur restando supportata e diagnosticata. Due dispatcher con jump
table sono invece rimasti host e portano la closure a 170 funzioni.

La conversione VFP `VCVT.S32.F32` usa la stessa implementazione binary32 pinned
dell'interprete. Chiude altre otto funzioni e porta la configurazione accettata
a 178 funzioni, 9.949 istruzioni e copertura nominale `54,246%`. Due run 4K
senza readback misurano `5,7138 s` e `5,9026 s` guest, media `5,8082 s`; il
tempo processo e `9,8094 s` e `10,1386 s`. Restano identici 18.305 draw, PCM
FNV-1a `14397512309130401104` e framebuffer SHA-256
`DF8375ED116DEBE8DAD86BC400EFD4AABD8F37E8228316D7A2B3D1D2789C5B7F`.

Sono disponibili anche confini `SVC` con resume, `VSQRT` esatto e trasferimenti
`LDRD/STRD` con fault address e writeback ARM. La promozione congiunta delle 14
funzioni sbloccate ha pero peggiorato il guest a `6,3189 s`; anche il
sottoinsieme senza rientri ARM ha misurato `6,0103 s`. Il manifest conserva
quindi queste entry in `performance_exclusions`: e una decisione PGO, non una
modifica semantica. Il lowering resta disponibile finche la chiusura dei loro
callee o un diverso layout rende la coorte misurabilmente utile.
