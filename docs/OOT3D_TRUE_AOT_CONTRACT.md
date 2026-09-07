# OOT3D True AOT Contract

## Obiettivo

Il runtime finale non deve interpretare `PackedOp` e non deve tradurre codice
durante il gioco. Ogni funzione OOT3D viene risolta, in ordine, come:

1. C/C++ decompilato e mantenuto, compilato direttamente dal toolchain host;
2. codice host generato ahead of time dal residuo A32 di `code.bin`;
3. interprete A32 soltanto nelle build diagnostiche e nei confronti
   differenziali.

Il renderer PICA, i servizi CTR e la memoria guest restano componenti host
condivisi. Non esiste un secondo stato gameplay e il traduttore non sintetizza
logica specifica per scene, attori o cutscene.

## Unita Di Compilazione

L'unita primaria e una funzione completa dell'inventario `code.bin`. Se una
funzione contiene ancora istruzioni o confini non traducibili, l'unita
secondaria e la massima regione CFG chiusa che possa conservare in host loop e
branch interni. Un basic block isolato e ammesso soltanto come prima prova o se
una misura dimostra che una regione piu grande e sfavorevole. I confini che
giustificano una divisione sono SVC/MMIO, call indirette, semantica A32 non
ancora coperta e punti espliciti destinati a un override C/C++.

Una regione puo esportare piu entry point guest verso lo stesso corpo host. Il
dispatch avviene una volta all'ingresso; salti, join e back-edge interni sono
control flow C++ diretto e non rientrano nel decoder o nel dispatcher. Quando
una funzione viene successivamente compilata da sorgente, il suo simbolo host
sostituisce integralmente le regioni AOT corrispondenti. Un artifact AOT
contiene:

- intervallo guest e hash dei byte A32 originali;
- simbolo semantico, se noto;
- dipendenze verso altre funzioni guest;
- tabella degli entry point interni ammessi;
- codice host diretto, senza array `PackedOp`;
- mappa guest-PC/source-line per fault e diagnostica;
- classificazione delle uscite: return, tail branch, call, SVC, fault.

Gli shard sono partizionati per mantenere incrementali compilazione e linking.
Una modifica a un consumer host non rigenera gli shard; una modifica al
generatore invalida soltanto gli shard il cui output cambia.

## ABI Host

Ogni funzione generata riceve:

```cpp
struct AotContext {
    GuestState& State;
    MemoryBus& Memory;
    HostCallResolver& Calls;
};

AotExit Execute(AotContext& context);
```

`GuestState` conserva registri A32, CPSR, FPSCR, VFP, TLS ed exclusive state.
`AotExit` contiene kind, guest PC e dettaglio per SVC o fault. Return e branch
non rientrano nel dispatcher `PackedOp`: saltano direttamente a un simbolo AOT
risolto staticamente oppure ritornano al piccolo dispatcher di funzioni quando
la destinazione e dinamica.

Le chiamate verso C/C++ decompilato e A32 generato condividono la stessa ABI.
Il resolver serve solo per chiamate indirette, servizi host e simboli non ancora
convertiti; le chiamate dirette tra simboli AOT diventano normali call host.
Una funzione compilata viene risolta anche quando il suo entry point e il PC
corrente del dispatcher, non soltanto come target di `BL/BLX`. Questo copre
tail branch, callback e riprese dirette senza rieseguire il corpo packed. Se il
corpo host rifiuta l'input, il dispatcher esegue normalmente lo shard A32 a
quell'indirizzo.

## Semantica A32

Il codice generato usa aritmetica unsigned esplicita per wrap a 32 bit. Flag N,
Z, C e V vengono prodotti soltanto dalle istruzioni che li aggiornano e restano
valori host separati nel CPSR guest. Shift, multiply-low, multiply-long,
saturazione ed exclusive access hanno primitive definite senza signed overflow
C++.

Load e store passano dall'API memoria guest. Gli intervalli RAM contigui possono
ottenere view host validate per eliminare il costo virtuale per word; MMIO,
allineamenti eccezionali e accessi atomici restano transazioni esplicite. Ogni
fault riporta il guest PC dell'istruzione originale.

Le operazioni VFP comuni vengono emesse direttamente. Le primitive binary32
esatte esistenti restano per rounding mode, NaN, flush-to-zero ed eccezioni che
non possono usare in sicurezza il `float` host. La scelta e per istruzione e
non puo cambiare il risultato FPSCR.

## Servizi E Callback

`SVC` termina l'unita AOT con lo stesso immediate del binario. Il process host
esegue il servizio e riprende dall'entry point AOT successivo. Callback native,
thread entry, TLS e scheduler usano indirizzi guest come identita; i puntatori a
funzione non vengono sostituiti dentro la memoria del gioco.

Le funzioni non ancora convertite sono ammesse soltanto in una configurazione
diagnostica esplicita. Una build release segnala a build time ogni destinazione
raggiungibile priva di sorgente compilato o shard true-AOT.

## Strategia Incrementale

1. Compilare direttamente le funzioni C/C++ complete ordinate dal profilo.
2. Generare true-AOT per funzioni o massime regioni CFG delle funzioni residue.
3. Aggiungere memory, branch e call dirette, poi VFP e operazioni rare.
4. Ridurre a zero gli ingressi nell'interprete nella title intro.
5. Estendere la closure a boot, Kokiri Forest e playthrough completo.
6. Eliminare l'interprete dal prodotto, mantenendolo nei test differenziali.

Il profilo decide l'ordine, non il comportamento. Ogni conversione deve passare
un confronto ARM/AOT con stato guest osservabile, PCM, comandi PICA e cattura
framebuffer interna. Una conversione piu lenta o non bit-stabile viene scartata.

## Criteri Del Primo Shard

Il primo shard generato deve:

- contenere soltanto operazioni integer e branch con semantica gia coperta;
- non chiamare `ExecuteBlock`, `ExecuteCore*` o decoder basati sul raw opcode;
- produrre direttamente espressioni e accessi memoria host;
- avere un test differenziale su input sintetici e una run title completa;
- ridurre il tempo guest misurato, non soltanto il numero di `PackedOp`;
- conservare identici hash PCM, draw count e framebuffer interno.

Solo dopo questa prova il formato viene esteso al resto dell'immagine.

## Primo Shard Validato

Il manifest `tools/oot3d/native_a32_runtime/true_aot_blocks.json` seleziona il
blocco `0x003247B0`. Il runtime marca soltanto il relativo record `Block`,
quindi il dispatcher normale non esegue ricerche aggiuntive. Il corpo host
compila il back-edge del loop, aggiorna CPSR e conserva fault PC e accessi guest
originali. La run differenziale da 600 refresh produce 1.082 call host per
537.754 iterazioni e riduce il tempo guest in entrambe le coppie alternate;
hash PCM, hash framebuffer e draw count restano identici. Questo chiude la
prova dell'ABI.

L'emissione non e piu un corpo statico dedicato. Il generatore locale
`tools/oot3d/native_a32_runtime/generate_true_aot.py` legge da `code.bin`
l'intervallo CFG dichiarato dal manifest e produce
`oot3d_a32_true_aot_generated.{h,cpp}`. Il primo subset traduce direttamente
ALU integer, shift immediati, load/store immediati, condizioni, branch interni
e piu entry point per regione; non contiene
`PackedOp`, decoder o dispatch per iterazione. Un'istruzione non appartenente
al subset invalida la generazione invece di introdurre un fallback silenzioso.
La seconda regione validata amplia il loop copia di `FUN_00303B24` da sei
istruzioni isolate a `0x00303C70..0x00303CB8`: include inizializzazione, salto
al join, loop interno ed esterno e usa `0x00303CAC` come ingresso guest
alternativo nello stesso corpo. In 600 refresh le transizioni true-AOT scendono
da 23.682 a 5.230; tre coppie alternate misurano una riduzione media di circa
31 ms guest, con output invariato.

I candidati true-AOT non sono piu incorporati negli shard packed. Il runtime
configura i soli bit `native_candidate` dagli entry point generati: una modifica
al piccolo manifest rigenera il corpo locale in circa mezzo secondo e compila
soltanto quel translation unit, senza ripetere l'analisi delle 794.924 slot A32.

La prima regione VFP validata e `NativeCurve_SampleFloat.scan_type2_keys`
(`0x00308910..0x0030894C`). VMOV e VMRS vengono emessi come trasporti diretti;
VCVT signed e VCMPE chiamano primitive binary32 esatte gia condivise dal decoder,
senza passare il raw opcode a un interprete. FPSCR, eccezioni e NZCV restano
parte dello stato guest. La regione viene conservata soltanto dopo confronto
prestazionale e identita completa di PCM, draw e framebuffer.
Il wrapper manuale resta compilabile soltanto quando gli artifact generati non
sono disponibili; il normale target AOT usa il corpo generato e il relativo
test di processo. Il prossimo incremento deve ampliare subset e regioni in base
al profilo residuo, preservando lo stesso criterio fail-closed.
