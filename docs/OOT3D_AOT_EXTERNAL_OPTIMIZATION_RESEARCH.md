# OOT3D AOT External Optimization Research

## Scopo

Questa ricerca confronta il whole-AOT OOT3D con static binary translator,
dynamic binary translator e recompilation project pubblici. L'obiettivo non e
copiare un backend estraneo, ma identificare quali tecniche affrontano il costo
misurato nel nostro runtime:

- 53.534.709 ingressi di basic block in 480 refresh;
- circa 5,3 secondi guest su 7,7 secondi host nella migliore run Kokiri;
- funzioni gia coperte dal whole-AOT, senza unsupported exit;
- accessi frequenti a `GuestState`, helper memoria, flag e accounting;
- 256 shard C++ compilati da MSVC.

Data della ricerca: 20 luglio 2026.

## Conclusione

Il problema principale non e la quantita di codice ARM tradotto. E la qualita
della rappresentazione consegnata al compilatore host.

I traduttori veloci convergono su questi principi:

1. formano regioni piu ampie di un basic block;
2. rappresentano valori guest in SSA o temporanei host;
3. eliminano get/set ridondanti dello stato architetturale;
4. calcolano e materializzano i flag soltanto quando sono vivi;
5. collegano direttamente blocchi e call noti;
6. separano RAM ordinaria da fault, MMIO, trace e altri slow path;
7. specializzano lo stato CPU invariabile;
8. applicano PGO, layout e LTO dopo aver reso ottimizzabile l'IR.

Per OOT3D, la priorita corretta e quindi:

1. state promotion e loop/region lowering nell'IR;
2. flag liveness e lazy materialization;
3. fast/slow path espliciti per memoria, diagnostica e accounting;
4. backend LLVM AOT oppure C++ reso deliberatamente SROA-friendly;
5. PGO/SPGO e layout come rifinitura.

Cambiare semplicemente MSVC con Clang, abilitare LTCG sull'archive attuale o
aumentare ancora la copertura non corregge la causa strutturale.

## Evidenze Dai Progetti

### LLBT: Il Confronto Piu Vicino

LLBT traduce staticamente ARM in LLVM IR e poi usa i backend LLVM per x86,
x86-64, ARM e MIPS. E il riferimento piu vicino al nostro caso per ISA e
modalita AOT.

Il paper descrive ARM register e condition flag inizialmente come variabili
locali LLVM. Le ottimizzazioni promuovono tali variabili da memoria a registri
virtuali e la global register allocation le assegna ai registri fisici host.
Gli autori attribuiscono a questa trasformazione una forte riduzione dei
load/store dello stato architetturale.

Nella loro valutazione:

- `-O2` target-independent porta circa 2,8x rispetto a QEMU sul corpus usato;
- le ottimizzazioni target-dependent portano il risultato oltre 6x;
- la global register allocation vale circa 2x rispetto a quella locale;
- SROA e la pass piu importante fra quelle target-independent, perche rende i
  membri dello stato ARM candidati a register allocation e propagation.

Questi numeri appartengono a EEMBC, toolchain e hardware del paper e non sono
una previsione per OOT3D. Il risultato trasferibile e causale: se lo stato ARM
resta un aggregate osservabile attraverso pointer alias e helper, il backend
non puo promuoverlo efficacemente.

LLBT riduce anche il costo dei branch indiretti limitando la Address Mapping
Table a return address, function entry e jump-table target plausibili, quindi
partizionandola tramite hash invece di usare un unico grande switch sparso.

Fonti:

- [A Retargetable Static Binary Translator for the ARM Architecture](https://ir.lib.nycu.edu.tw/bitstream/11536/25095/1/000341066600004.pdf)
- [Scheda ufficiale del paper LLBT](https://scholar.nycu.edu.tw/en/publications/a-retargetable-static-binary-translator-for-the-arm-architecture/)

### QEMU TCG: Chaining E Stato Specializzato

QEMU evita il ritorno al main dispatch fra translation block quando il target
e noto. Il direct block chaining collega direttamente il blocco corrente al
successivo; solo cambi di stato rilevanti, interrupt, fault o target non noti
tornano al dispatcher.

TCG registra inoltre nel translation block gli aspetti dello stato CPU che non
possono cambiare durante il blocco. Il codice emesso e specializzato per quello
stato e omette operazioni che sarebbero altrimenti ripetute.

Il TCG IR applica liveness a livello basic block, dead-code elimination,
constant simplification e rimozione di move morti. Sono ottimizzazioni meno
globali di LLBT, ma confermano che un percorso letterale privo di data-flow
analysis lascia overhead evitabile.

Applicazione OOT3D:

- le call dirette whole-AOT sono gia un buon equivalente del block chaining;
- manca ancora la continuita dei valori guest attraverso regioni e loop;
- CPSR/FPSCR mode invarianti possono diventare specialization key o assert di
  ingresso invece di essere riletti durante ogni operazione;
- fault, SVC e scheduling restano uscite obbligatorie, non motivi per
  materializzare tutto lo stato a ogni basic block.

Fonti:

- [QEMU Translator Internals](https://www.qemu.org/docs/master/devel/tcg.html)
- [QEMU TCG Intermediate Representation](https://www.qemu.org/docs/master/devel/tcg-ops.html)

### FEX: SSA, Register Allocation E Flag Morti

FEX usa un IR SSA con register allocation esplicita. Il pass pubblico:

- conserva affinita fra load/store del register file e registri host;
- calcola next-use;
- usa spill furthest-first;
- rimaterializza costanti invece di spillare;
- coalesca move e operazioni adiacenti;
- distingue classi GPR, FPR e registri fixed.

FEX possiede inoltre un pass dedicato alla eliminazione dei calcoli di flag
ridondanti, con data-flow sul CFG e insiemi read/write per singolo flag.

Applicazione OOT3D:

- N/Z/C/V non devono essere sempre ricomposti nel CPSR;
- ogni istruzione ARM puo produrre una rappresentazione lazy dei flag;
- il valore concreto va generato solo per consumer, exit osservabili o helper
  che leggono CPSR;
- VFP e GPR richiedono classi/liveness separate per evitare spill artificiali.

Fonti:

- [FEX RegisterAllocationPass](https://github.com/FEX-Emu/FEX/blob/main/FEXCore/Source/Interface/IR/Passes/RegisterAllocationPass.cpp)
- [FEX RedundantFlagCalculationElimination](https://github.com/FEX-Emu/FEX/blob/main/FEXCore/Source/Interface/IR/Passes/RedundantFlagCalculationElimination.cpp)
- [Repository ufficiale FEX](https://github.com/FEX-Emu/FEX)

### Box64: Big Blocks, Deferred Flags E Call/Ret

Box64 espone pubblicamente opzioni che riflettono la propria architettura:

- `DYNAREC_BIGBLOCK` costruisce blocchi piu grandi;
- `DYNAREC_DF` abilita deferred flags;
- `DYNAREC_NATIVEFLAGS` usa i flag host quando possibile;
- `DYNAREC_CALLRET` e secondary entry point evitano lookup non necessari;
- DynaCache persiste codice tradotto per ridurre il costo di startup.

Applicazione OOT3D:

- big block e deferred flag sono direttamente rilevanti;
- la cache di codice JIT non serve, perche gli object AOT sono gia persistenti;
- native flags possono essere un'ottimizzazione successiva e backend-specific,
  non il primo requisito per una corretta lazy flag representation;
- le opzioni di memory ordering x86-on-ARM non si applicano al nostro host x64.

Fonte:

- [Box64 usage and performance options](https://github.com/ptitSeb/box64/blob/main/docs/USAGE.md)

### N64Recomp: Letteralita Utile, Ma Con Limiti Diversi

N64Recomp emette C letterale, usa call C++ dirette per `jal`, riconosce tail
call e trasforma jump table note in switch. Il runtime gestisce lookup per
target indiretti e overlay. Questo conferma la validita generale di call
dirette e metadata statici.

N64Recomp dimostra anche due pratiche di build utili:

- output separato per funzione o gruppi di funzioni;
- archive base precompilato e object di patch collegati prima dell'archive,
  cosi una modifica locale non richiede ricompilare tutto.

La somiglianza termina qui. OOT3D A32 ha conditional execution, CPSR, VFP,
servizi CTR e un contratto di scheduling a basic block. Il fatto che C letterale
sia sufficiente per un recomp MIPS non dimostra che MSVC possa eliminare lo
stato ARM materializzato nel nostro emitter.

Fonti:

- [N64Recomp](https://github.com/N64Recomp/N64Recomp)
- [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)

### rev.ng E HQEMU: IR Compiler E Region Formation

rev.ng solleva codice attraverso un IR indipendente dall'ISA e LLVM, usando
SSA, use-def e pass compiler standard. HQEMU mantiene un percorso rapido per i
blocchi e forma trace piu ampie da ottimizzare con LLVM. Le due architetture
mostrano una distinzione utile:

- il frontend deve conservare semantica e CFG;
- un IR di ottimizzazione deve eliminare il costo dello stato guest;
- il backend produce object host;
- runtime e slow path non devono contaminare ogni operazione tradotta.

Per OOT3D non serve introdurre un tier JIT: profilo e CFG sono gia disponibili
offline. Le trace hot possono essere trasformate in regioni AOT deterministiche.

Fonti:

- [rev.ng CFG and function recovery paper](https://rev.ng/downloads/cc-2017-paper.pdf)
- [HQEMU paper](https://www.iis.sinica.edu.tw/papers/dyhong/18243-F.pdf)

## Ottimizzazioni Del Compilatore Host

### PGO E SPGO MSVC

MSVC PGO usa `/GL`, profili di esecuzione e `/LTCG /USEPROFILE`. Le
ottimizzazioni documentate includono inlining, register allocation, layout di
basic block e funzioni, branch ordering e separazione del codice freddo.
SPGO puo raccogliere sample hardware con `xperf` senza una build instrumented.

Questo e utile per:

- ordinare i 12.196 corpi emessi;
- isolare fault e diagnostica freddi;
- migliorare branch probability e instruction-cache locality;
- verificare quali shard e funzioni meritano `favor speed`.

Non puo ricostruire affidabilmente SSA guest attraverso helper aliasing e
materializzazioni obbligatorie generate dal sorgente. Va quindi provato dopo o
in parallelo a un pilot strutturale, mai usato come sostituto.

Fonti:

- [MSVC Profile-Guided Optimizations](https://learn.microsoft.com/en-us/cpp/build/profile-guided-optimizations?view=msvc-170)
- [MSVC LTCG](https://learn.microsoft.com/en-us/cpp/build/reference/ltcg-link-time-code-generation?view=msvc-170)
- [MSVC Sample PGO](https://learn.microsoft.com/en-us/cpp/build/reference/spgo-enable-sample-profile-guided-optimization?view=msvc-170)

### LLVM ThinLTO

ThinLTO combina un indice whole-program con backend paralleli e cache
incrementale. E piu adatto del monolithic LTO a 256 shard grandi e permette
function importing senza fondere tutto in un singolo modulo ingestibile.

Un eventuale backend LLVM dovrebbe produrre bitcode per shard/SCC, usare
ThinLTO e limitare i job di backend. Compilare semplicemente il C++ corrente con
ThinLTO resta un esperimento secondario: la forma dell'IR in ingresso conta piu
del linker.

Fonte:

- [Clang ThinLTO](https://clang.llvm.org/docs/ThinLTO.html)

## Gap Precisi Del Runtime Corrente

| Tecnica | Progetti | Stato OOT3D | Gap |
| --- | --- | --- | --- |
| Direct call/chaining | QEMU, N64Recomp, Box64 | presente per call whole-AOT | estendere continuita stato nelle regioni |
| Region/big-block formation | HQEMU, Box64 | funzione e CFG, ma accounting per blocco | SCC/loop region con valori live |
| State promotion | LLBT, FEX | `GuestState` canonico referenziato | locali/SSA e flush su exit |
| Global register allocation | LLBT, LLVM | delegata a MSVC per singolo shard | IR che renda separabili GPR/flag/VFP |
| Lazy/dead flags | FEX, Box64 | flag concretizzati frequentemente | liveness N/Z/C/V e materializzazione lazy |
| CPU-state specialization | QEMU | minima | specialization key per invarianti sicure |
| Fast/slow memory split | QEMU, LLBT | page table fast piu fallback | inline RAM access e outline fault/MMIO |
| Indirect target specialization | LLBT, N64Recomp | external/indirect dispatch | target set statico, PIC e hot-target guard |
| PGO/layout | MSVC, LLVM | non applicato al whole archive | pilot S/PGO dopo state promotion |
| Incremental LTO/cache | ThinLTO, N64Recomp | archive e shard incrementali | pilot bitcode/SCC separato |

## Strategia Raccomandata

### Fase 0: Assembly Evidence

Prima di cambiare backend, selezionare tre regioni:

- loop PICA `0x00466E7C`;
- loop audio `0x004A02C8`;
- una funzione control-flow non numerica.

Per ciascuna misurare nell'assembly corrente:

- load/store a `GuestState.r[]`, CPSR e VFP;
- call helper per basic block e per memory access;
- spill/reload host;
- branch verso accounting, trace e fault;
- dimensione e instruction-cache footprint.

Questa e la baseline meccanica. FPS da solo non dice perche il codice e lento.

### Fase 1: Architectural-State Promotion Nel C++ Emitter

Implementare una IR pass indipendente dal backend:

1. use/def di GPR, VFP e singoli flag per basic block;
2. liveness inter-block e phi ai merge;
3. dirty set dello stato architetturale;
4. temporanei scalari per valori live nella regione;
5. flush soltanto su SVC, fault, indirect unresolved, callback osservabile e
   ritorno al dispatcher;
6. reload soltanto dopo helper che possono modificare lo stato;
7. modalita scalare invariata per block trace completo.

Il C++ deve esporre scalari separati, non una view opaca dell'array `r[]`.
L'obiettivo del pilot e far sparire i load/store del `GuestState` dal corpo del
loop nell'assembly, non soltanto cambiare la sintassi emessa.

### Fase 2: Lazy Flags

Rappresentare N/Z/C/V con producer e valid mask:

- eliminare producer sovrascritti prima dell'uso;
- generare solo i flag letti dalla condizione successiva;
- materializzare CPSR completo solo alle uscite osservabili;
- mantenere helper esatti per casi rari e complessi;
- non usare direttamente host EFLAGS finche l'IR lazy non e validato.

Questo ordine separa l'ottimizzazione semantica portabile da un futuro lowering
x64 specifico.

### Fase 3: Region E Loop Lowering

Formare regioni da SCC naturali e direct-call closure, con limiti di size:

- loop interni strutturati come `while`/branch host;
- budget in locale host;
- accounting matematicamente equivalente per edge/iterazione;
- safe point agli stessi confini richiesti dallo scheduler;
- side exit con PC guest preciso e flush del dirty set;
- nessun riconoscimento per indirizzo o scena.

La prima fixture puo essere `0x00466E7C`, ma la regola deve dipendere da CFG,
use/def e side effect.

Stato del pilot del 20 luglio 2026: il frontend whole-AOT mantiene ora GPR e
N/Z/C/V come scalari C++ per tutta la CFG della funzione, quindi anche lungo i
backedge delle SCC. `commitState` scrive soltanto i GPR/flag dirty e viene
eseguito su ritorno/side exit, direct o indirect call, external call e callback
di block-entry effettivamente osservabile; `reloadState` segue soltanto confini
che possono modificare lo stato. I trasferimenti VFP multipli dichiarano ora
anche use/writeback del base register nell'IR di analisi.

Sul medesimo `MeshCommandPacket_Submit_00466E2C` usato dal pilot LLVM, Clang
22.1.6 O2 passa da 32 load e 33 store del contenitore promosso a 17 load e 12
store; i load/store diretti di `GuestState` restano entrambi a zero. Gli accessi
residui sono nei cammini di ingresso/uscita e osservazione, non nel modello di
esecuzione delle singole istruzioni guest. Artefatti riproducibili:
`I:\oot3dre_work\whole_aot_optimization\llvm_pilot\scalar_mesh_submit`.

### Fase 4: Backend LLVM AOT

Se il C++ non produce assembly adeguato, riusare la stessa IR ottimizzata:

- un modulo per SCC/shard, non un'unica funzione da 900.000 istruzioni;
- scalar alloca promuovibili oppure SSA esplicita;
- helper con attributi `readonly`, `readnone`, `noalias`, `nounwind` soltanto
  quando dimostrati dal contratto;
- object COFF e registry entry compatibile con il runtime attuale;
- ThinLTO opzionale e cache incrementale;
- archive separato dal MSVC AOT validato.

LLVM non deve cambiare servizi CTR, PICA, audio, scheduling o semantica A32.

### Fase 5: PGO, Layout E Specializzazione

Dopo la riduzione dei load/store guest:

1. provare MSVC PGO/SPGO sull'archive strutturalmente migliorato;
2. separare hot/cold function e side exit;
3. ordinare funzioni secondo call graph/profile;
4. specializzare indirect target con guard hot piu fallback generale;
5. confrontare ThinLTO/clang-cl e MSVC su archive distinti.

Il target `oot3d_native_whole_aot` espone ora opzioni isolate, tutte disattive
per default:

- `OOT3D_WHOLE_AOT_THINLTO=ON` genera bitcode ThinLTO per shard, riconosciuto
  direttamente da `lld-link` nel consumer finale;
- `OOT3D_WHOLE_AOT_PROFILE_MODE=generate` abilita LLVM instrumentation PGO;
- `use` consuma il `.profdata` indicato da `OOT3D_WHOLE_AOT_PROFILE_FILE`;
- `sample-use` consuma un sample profile prodotto, per esempio, da
  `llvm-profgen` senza alterare la run misurata;
- `OOT3D_WHOLE_AOT_ORDER_FILE` propaga un `/ORDER:@file` opzionale al link
  finale, dopo che il profilo ha fornito un ordine difendibile.

Il merge delle run instrumented e deterministico:

```powershell
$env:LLVM_PROFILE_FILE='I:\oot3dre_work\profiles\oot3d-%p-%m.profraw'
# Eseguire qui le workload rappresentative senza usare la run come benchmark.
python tools\oot3d\native_a32_runtime\merge_llvm_profiles.py `
  --llvm-root I:\oot3dre_tools\llvm-22.1.6 `
  --input-directory I:\oot3dre_work\profiles `
  --output I:\oot3dre_work\profiles\oot3d.profdata `
  --report I:\oot3dre_work\profiles\merge_report.json
```

Un pilot clang-cl 22.1.6 sullo shard scalare 168 ha gia verificato insieme
`-flto=thin` e `-fprofile-instr-generate`: l'output da 1.763.144 byte e bitcode
LLVM valido (`LLVM22.1.6`), non un normale COFF materializzato da MSVC. Il
link MSVC-ABI usa esplicitamente `clang_rt.builtins-x86_64.lib` e, in modalita
`generate`, `clang_rt.profile-x86_64.lib`; i flag driver non vengono passati
erroneamente come opzioni raw a `lld-link`. Il
passaggio `use` accetta intenzionalmente soltanto una workload completa e
riproducibile: un profilo sintetico produrrebbe layout fuorviante.

### Risultato Whole-AOT LLVM Del 20 Luglio 2026

La pipeline e stata validata end-to-end sul checkpoint
`navi_kokiri_main_forest.oot3dsav`, con Vulkan, audio attivo e 480 frame:

- build instrumented ThinLTO completa e funzionante;
- una run rappresentativa ha prodotto 41.626.496 byte di `.profraw`;
- `llvm-profdata merge` ha prodotto un `.profdata` da 4.303.648 byte;
- la build `profile=use` ha compilato e linkato l'intera applicazione;
- sei run PGO e sei run ThinLTO senza profilo hanno mantenuto 37.817 draw,
  1.577.894 chiamate whole-AOT, zero unsupported exit e 523.774 campioni audio
  non nulli.

Le mediane robuste, preferite alle medie per la presenza di outlier host, sono:

| Variante | FPS mediano | Guest time mediano |
| --- | ---: | ---: |
| LLVM ThinLTO | 63,19 | 4,12 s |
| LLVM ThinLTO + instrumentation PGO | 65,44 | 3,82 s |

Il PGO riduce quindi il guest time mediano di circa il 7,2% e aumenta il frame
rate mediano di circa il 3,6%. Tutte le run PGO hanno lo stesso fingerprint
finale; cinque delle sei baseline condividono quel fingerprint. La singola
baseline divergente mantiene gli stessi contatori grafici, AOT e audio e va
considerata separatamente come nondeterminismo runtime da localizzare, non come
una differenza attribuita al profilo.

Artefatti e report sono in
`I:\oot3dre_work\profiles\aot_scalar_thinlto`. Le configurazioni `generate`,
`use` e `none` devono usare build directory distinte: cambiare modalita nella
stessa directory invalida tutti i 256 shard e costa circa otto minuti anche su
questa macchina. Il prossimo raffinamento di layout deve ricavare un ordine di
simboli dal profilo e dalla symbol table LLVM; non va costruito da indirizzi PC
guest o da una lista manuale di funzioni.

`Build-Oot3dLlvmWholeAot.ps1` applica questo contratto per costruzione: crea le
sottodirectory persistenti `none`, `generate`, `use` e `sample-use`, riusa il
vcpkg statico validato e porta in ogni build il medesimo `aot_program.json`.

```powershell
.\scripts\oot3d\Build-Oot3dLlvmWholeAot.ps1 `
  -ProfileMode use `
  -ProfileFile I:\oot3dre_work\profiles\aot_scalar_thinlto\oot3d.profdata
```

`-ConfigureOnly` valida una nuova configurazione senza avviarne la build; una
nuova directory richiede comunque il configure iniziale delle dipendenze, ma
le ricompilazioni successive restano confinate alla modalita scelta.

Il profilo stesso ha poi esposto un collo di bottiglia piu importante del
layout: `Oot3dAotShouldNotifyBlock` eseguiva una scansione lineare di circa 65
hook UI su ogni basic block, raggiungendo 3,33 miliardi di iterazioni interne
nella workload. Il producer ora ordina e deduplica una volta la lista e il
runtime usa una ricerca binaria. Le modalita trace/profile, che passano una
lista vuota per richiedere ogni block entry, restano semanticamente invariate.

Sei nuove run per variante dopo la correzione danno:

| Variante | FPS mediano | Guest time mediano |
| --- | ---: | ---: |
| LLVM ThinLTO + hook lineari | 63,19 | 4,12 s |
| LLVM ThinLTO + hook binari | 71,97 | 3,13 s |
| LLVM ThinLTO + hook binari + PGO | 78,33 | 2,75 s |

La correzione strutturale migliora il guest time mediano di circa il 24% e il
frame rate di circa il 14%; il PGO aggiunge poi circa il 12% sul guest time e
circa il 9% sul frame rate rispetto alla nuova baseline. Tutte le dodici run
post-correzione hanno fingerprint finale `7208472857129201407`, 37.817 draw,
1.577.894 chiamate whole-AOT, zero unsupported exit e lo stesso output audio.

## Cosa Non Fare

- Non aggiungere altre nove funzioni manuali una alla volta: sono utili come
  oracle e upper bound, non come architettura generale.
- Non aumentare ancora la copertura: gli unsupported exit sono gia zero.
- Non rimuovere safe point o block budget senza provare equivalenza scheduler.
- Non usare il profiler instrumented come benchmark wall-time.
- Non introdurre cache JIT, invalidazione self-modifying o tiering non richiesti
  da un `code.bin` statico noto.
- Non applicare monolithic LTCG ai 304 MB di archive come primo esperimento:
  costo e rischio sono elevati e la rappresentazione resta invariata.
- Non assumere che `__restrict` risolva aliasing senza un contratto verificato.

## Protocollo Di Valutazione

Ogni pilot deve riportare quattro livelli di prova:

1. **Assembly**: load/store guest, spill, helper call e branch nel loop.
2. **Semantica**: trace interprete/whole-AOT equivalente per almeno 3 frame.
3. **Contatori**: PCM, draw, unsupported exit e stato finale invariati.
4. **Prestazioni**: almeno tre coppie A/B alternate dopo warm-up.

Soglia consigliata per integrare una trasformazione complessa:

- almeno 5% sul guest time oppure circa 3 FPS di prodotto;
- nessuna disattivazione di audio, interpolazione o rendering;
- regola generale A32/CFG/ABI, senza PC hardcoded.

## Decisione Tecnica

La pass `ArchitecturalStatePromotion`, il lowering scalare di regione e il
pilot LLVM sono ora completati. Le evidenze mostrano che il backend LLVM
ThinLTO con PGO e una direzione valida, ma che il guadagno complessivo resta
limitato dalla parte non guest del frame. I prossimi investimenti devono quindi:

- mantenere il PGO come configurazione LLVM di produzione;
- isolare le build directory per evitare ricompilazioni massive tra modalita;
- derivare e misurare un layout hot/cold dal profilo reale;
- localizzare gli outlier host separando guest, submit PICA e present;
- mantenere build e trace correnti come oracle.

Il backend LLVM non e piu una riscrittura speculativa: e stato compilato,
profilato e misurato sull'intera workload Kokiri. S/PGO resta un'alternativa da
confrontare solo se offre un profilo piu rappresentativo o meno intrusivo; non
deve sostituire una instrumentation PGO che ha gia superato la soglia tecnica.
