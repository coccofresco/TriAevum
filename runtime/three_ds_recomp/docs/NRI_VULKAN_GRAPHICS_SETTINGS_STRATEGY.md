# Strategia Vulkan/NRI ed effetti avanzati per OoT3D

## Decisione esecutiva

La strategia precedente subordinava gli effetti avanzati alla migrazione quasi
completa del backend Vulkan verso NRI. Questo ordine rallenterebbe inutilmente
il primo risultato visibile.

La nuova strategia mantiene il rasterizzatore PICA originale come produttore
della scena e usa subito NRI in modalita interop sul device Vulkan esistente.
Colore, profondita e command buffer Vulkan vengono avvolti come oggetti NRI;
gli effetti vengono eseguiti nel punto di display transfer, senza aspettare la
riscrittura di allocatore, pipeline PICA e swapchain.

Le scelte iniziali sono:

| Area | Prima implementazione | Evoluzione prevista |
|---|---|---|
| Erba interattiva | distribuzione CPU deterministica sulle mesh CMB selezionate per texture, draw istanziato NRI, vento GPU e collision field di Link | culling/LOD GPU-driven, piu interattori e profili foliage PICA-aware |
| SSAO | FidelityFX CACAO con normali ricostruite dalla profondita e peso ambientale PICA pre-TEV | normal input PICA per le qualita alte e guida RGB per la risposta ambientale per-canale |
| SSR | ray marching gerarchico Hi-Z nativo, deterministico e material-gated | provider FidelityFX SSSR con working color lineare, environment PICA, split-sum BRDF, resolve e profili espliciti per texture operativi; calibrazione artistica e UI materiali rinviate fino alla riapertura esplicita del filone |
| Cel shading | shader ibrido `PicaToon`, sviluppato sul percorso PICA | profili per materiale e outline temporally stable |
| NRI | wrapper del device, command buffer e texture Vulkan esistenti | ownership NRI di risorse, pipeline e infine swapchain |
| Renderer originale | percorso `Authentic` bit-compatible come riferimento | nessuna rimozione finche la parita non e dimostrata |

Stato operativo del renderer dedicato: CACAO, erba interattiva, toon PICA,
outline, Hi-Z SSR filtrato, FidelityFX SSSR, FXAA, SMAA 1x High ufficiale,
MSAA 2x/4x/8x, TAA interno e gli upscaler NRI NIS/FSR/DLSS sono ora percorsi
eseguibili. Il filone materiali/riflessi e considerato chiuso al checkpoint
corrente per decisione dell'utente: le implementazioni restano disponibili,
ma calibrazione e UI non bloccano il lavoro successivo e verranno riaperte
solo su richiesta. SMAA non e piu l'approssimazione monolitica nello scanout: usa
edge detection, calcolo dei blending weight e neighborhood blending come tre
dispatch compute NRI distinti, con AreaTex/SearchTex ufficiali e output
RGBA16F separato. L'MSAA e
capability-gated e risolve separatamente
colore, depth, normal/material guide, rigid motion e ambient guide. Finche la
combinazione pipeline/resolve multisample NRI non e rappresentabile, resta
disponibile il fallback Vulkan. Sul percorso qualificato anche MSAA usa scope,
pipeline, descriptor e draw NRI, inclusi resolve depth/guide e
alpha-to-coverage. I consumer
CACAO/SSR continuano
quindi a ricevere surface single-sample coerenti. Il TAA usa jitter sub-pixel
Halton a otto campioni soltanto sui draw prospettici del mondo: HUD, ImGui,
Shadow2D e draw ortografici non vengono spostati.

La fondazione temporale dispone ora di un `TemporalHistoryManager` indipendente
da Vulkan: history per target e rendered-frame, current/previous
view-projection, inversa validata e reset classificati per primo frame,
savestate/scena, risoluzione, proiezione/FOV e teletrasporto. La diagnostica
runtime misura prepare validi, camera cut e uso reale della history. Il primo
consumer e operativo come `MotionVectorPass`: compute camera-based da depth
PICA risolta, compatibile
con depth prospettica e W-buffer, output RGBA16F pubblicato come
`SceneSurfaceKind::Motion`. Texture motion/reactive, uniform, descriptor,
sampler, pipeline, dispatch e barrier sono posseduti o registrati via NRI.
RG contiene l'offset UV verso la
history, B la disocclusione e A una copia della reactive mask. Il contratto
degli upscaler espone inoltre la mask come immagine R16F dedicata, derivata
dall'alpha libero della material guide: zero per gli opachi e uno per i draw
blended con depth test; HUD e overlay senza depth test sono esclusi. Ogni draw
world opaco idoneo dispone inoltre di
una `RigidMotionGuide` RGBA16F separata. La variante temporale rivaluta lo
stesso vertex program PICA con gli uniform correnti e precedenti, percio scrive
motion UV esatti anche per palette skinned e percorsi instanziati/deformabili;
un'identita stabile ricavata da shader, geometria, occorrenza e namespace lega
gli uniform tra frame consecutivi. Il decoder dell'origine rigida comune resta
un fallback economico, mentre il compute camera-based copre i pixel senza
history valida. La guide ha attachment e resolve MSAA propri. Anche le blade
generate conservano posizione e proiezione del batch precedente: vento,
collisione con Link e jitter producono motion UV per vertice nella stessa
guide, con invalidazione quando cambia la topologia. La doppia valutazione e le
relative varianti shader vengono escluse dal percorso `Authentic` quando non
sono richiesti TAA o motion vector.

`TemporalAaPass` mantiene due history RGBA16F ping-pong committed e possedute
da NRI, con validita del contenuto separata dallo stato di layout Vulkan.
Sampler, descriptor pool/set, root constants, pipeline e dispatch sono NRI;
anche le transizioni batch delle due history usano barrier NRI. Quando la history non
e valida il descriptor history punta esplicitamente al colore corrente, quindi
il primo frame non campiona memoria indefinita. Il resolve applica reproiezione motion,
neighborhood clamp 3x3, rejection disocclusion/reactive e sharpening
configurabile. `SceneCompositePass` produce inoltre un working target RGBA16F
modulare, committed e posseduto da NRI. Sampler, descriptor, root constants,
pipeline, dispatch e texture barrier del composite sono NRI:
CACAO e SSR vengono composti prima dell'accumulo TAA e non applicati una
seconda volta nello scanout. Nei percorsi TAA/upscaler e SMAA, anche l'outline
viene risolto nel composite avanzato prima del consumer successivo; il
fallback scanout resta disponibile per le modalita che non richiedono quel
working target. La fondazione alimenta ora anche il provider FSR
capability-gated tramite NRI, riusando motion esatto, reactive mask,
history/reset e depth lineare Hi-Z.

Il contratto di colore avanzato e ora esplicito. `LinearSceneColorPass`
converte lo snapshot PICA `R8G8B8A8_UNORM` in un working target
`R16G16B16A16_SFLOAT` committed NRI, senza modificare il render target
autentico. La policy pure-CPU deriva dal modo sRGB effettivo del raster PICA:
decodifica soltanto quando il fragment shader ha codificato esplicitamente
sRGB nel target UNORM. Pipeline compute, descriptor, sampler, target e barrier
sono NRI. SSSR, composite, TAA e upscaler ricevono l'encoding dichiarato; lo
scanout converte in sRGB soltanto quando la swapchain non effettua gia la
conversione. Questo target e HDR-capable, ma il contenuto PICA sorgente resta
LDR: illuminazione realmente HDR, exposure e tone mapping non vengono
considerati implicitamente completati.

Il provider SSSR dispone ora anche di una catena IBL modulare. I draw world
PICA depth-writing alimentano un accumulatore pure-CPU con fog e global
ambient originali; il profilo risultante genera via compute NRI un environment
cube 32x32 con sei mip GGX e una LUT split-sum BRDF 64x64. Le risorse persistono
tra i frame e la cube viene rigenerata soltanto quando cambia la firma
quantizzata del profilo. `ReflectionMaterialResolvePass`, separato sia
dall'adapter FidelityFX sia dal composite, converte poi la radiance grezza nel
contratto comune RGB+weight: ignora l'alpha interno del denoiser, ricava il
peso da coverage, reflectivity e roughness della material guide PICA e dalla
LUT BRDF. L'assenza di dati PICA validi usa un profilo low-frequency
deterministico, senza bloccare il fallback Hi-Z.

La calibrazione materiale non forza una euristica riflettente sui draw
vertex-lit. `ReflectionMaterialProfile` mantiene regole esplicite per hash,
dimensioni e mapper slot, con preset acqua, metallo, polished e custom.
`ResolveReflectionMaterialProfile` seleziona deterministicamente lo slot piu
basso e la prima regola corrispondente; una texture non assegnata resta non
riflettente. `PicaReflectionMaterialShaderVariant` riscrive soltanto
`pica_material_guide`, include reflectivity/roughness nella chiave pipeline e
non modifica mai `pica_color` o il campionamento dell'albedo. Il percorso
automatico basato sullo specular TEV originale rimane disponibile sulle scene
che usano realmente il fragment lighting PICA.

La calibrazione e ora osservabile senza log testuali ad hoc:
`reflection_texture_usages` aggrega per frame, con un limite fisso di 96 voci,
gli hash esatti, dimensioni e mapper slot dei draw world material-eligible.
Per le texture assegnate include rule id, profilo, reflectivity, roughness e
numero di draw realmente profilati. Il gate rifiuta overflow e richiede che
tutti i draw profilati siano attribuiti all'hash richiesto.
Per l'ispezione offline, `OOT3D_TEXTURE_PREVIEW_DIRECTORY` abilita inoltre
l'esportazione opt-in dei texel RGBA gia decodificati come BMP nominati con
hash e dimensioni. L'export e isolato in `TexturePreviewArtifact`, non
sovrascrive file esistenti e ogni errore resta estraneo all'upload texture.
La debug view materiale usa R per reflectivity e G per class/coverage:
l'alpha della material guide e la reactive mask temporale e non viene piu
presentato erroneamente come eligibility.

La stabilita sul checkpoint disponibile e misurata su quattro frame
stabilizzati consecutivi per provider. Il parser BMP e le metriche di sequenza
sono moduli condivisi, mentre il gate A/B resta soltanto l'orchestratore:
SSSR deve rimanere entro l'envelope temporale osservato con Hi-Z sulla stessa
simulazione, senza confondere animazioni deterministiche con flicker.

Anche il target R32F di CACAO e ora una texture committed e posseduta da NRI.
Il context FidelityFX rimane deliberatamente Vulkan, perche questa versione
della libreria espone quell'adapter: CACAO riceve image e storage view Vulkan
native estratti dalla risorsa NRI, mentre composite e scanout usano la sampled
view della stessa texture. La distruzione rispetta l'ordine
`DestroyScreenSizeDependentResources` e poi rilascio della texture NRI.

CACAO Medium usa ora RGB del normal guide PICA come input esterno, gia
codificato in `[0, 1]` e prodotto in view space dal fragment lighting originale.
`CacaoNormalInputDecision` mantiene la scelta separata dall'adapter
FidelityFX: Medium con view valida usa la guida e passa la trasformazione
identita normal-to-view; Low, view assente e ogni percorso legacy continuano a
ricostruire le normali dalla depth. L'identita della view entra nel contratto
di ricreazione delle risorse screen-size-dependent, evitando descriptor
obsoleti durante i cambi scena.

Il moltiplicatore post-process uniforme non e piu l'unico percorso CACAO.
Il frontend fragment-lit separa ambient locale/globale dal diffuse diretto
prima del TEV e pubblica nell'alpha del normal guide un peso ambientale
quantizzato in `[0.5, 1]`; XYZ restano invariati per outline e SSR. La guida
ambientale dedicata conserva inoltre la risposta RGB per-canale e un validity
bit. Un modulo pure-CPU e una libreria GLSL condivisa applicano la stessa
equazione nello scanout diretto e nel `SceneCompositePass` temporale. Gli
shader vertex-lit o originali scrivono validity zero e ricadono sull'alpha
legacy, evitando di duplicare logica nei consumer.

Anche lo scanout fullscreen e ora un modulo dedicato: shader, pipeline layout,
pipeline base/overlay, sampler, descriptor pool/set, root constants, binding e
draw sono NRI. Le immagini della swapchain sono possedute da `NriSwapchain`,
esposte al runtime come handle Vulkan nativi e passano attraverso il
`ResourceStateTracker` con transizioni
`Present -> ColorAttachment -> Present`. Anche lo scope dynamic-rendering del
target swapchain e ora aperto e chiuso da `NriPicaScanoutPass`; la diagnostica
`nri_scanout_scope_owned` ne certifica l'uso reale. Se dynamic rendering non e
disponibile, o se
`OOT3D_GRAPHICS_NRI_SCANOUT=0`, resta operativo lo scanout Vulkan precedente.
Acquire e present passano da `NRISwapChain`; i semafori binari del modulo sono
wrapped con `WrapperVKInterface` per conservare la submission Vulkan originale.
Il present worker proprietario viene escluso nello stesso gate. Se la creazione
NRI fallisce, immagini, view, semafori, acquire/present e worker tornano
in modo atomico al percorso Vulkan precedente.

Il raster PICA non dipende piu obbligatoriamente da un `VkRenderPass` legacy.
`PicaDynamicRenderingScope` apre uno scope con cinque color attachment,
depth/stencil, resolve colore e resolve depth MSAA, replicando esplicitamente
le dipendenze di memoria in ingresso e uscita. Begin/end rendering, view
attachment tipizzate e barrier globali pre/post PICA sono posseduti da NRI.
Le barrier non falsificano una transizione di layout nel
`ResourceStateTracker`: sono dipendenze esplicite `nri::GlobalBarrierDesc`
registrate dal ponte interop. Il contatore
`nri_pica_global_barrier_count` deve essere esattamente il doppio degli scope
dynamic-rendering; il fallback delle sole barrier si forza con
`OOT3D_GRAPHICS_NRI_PICA_BARRIERS=0`. La cache pipeline PICA Vulkan e la
pipeline dell'erba vengono create con
`VkPipelineRenderingCreateInfoKHR` quando la capability e disponibile; shader,
chiavi pipeline e submission originali restano invariati. La diagnostica
`native_pica_dynamic_rendering_count` prova l'uso reale per frame e
`nri_pica_rendering_scope_owned` distingue lo scope NRI dal fallback Vulkan.
Il solo scope NRI puo essere disattivato con
`OOT3D_GRAPHICS_NRI_PICA_SCOPE=0`; il percorso legacy puo essere forzato con
`OOT3D_GRAPHICS_PICA_DYNAMIC_RENDERING=0`: questo mantiene un riferimento A/B
e supporta hardware privo dell'estensione. Il render pass legacy disattiva
automaticamente anche il draw NRI posseduto, evitando di collegare una pipeline
dynamic-rendering a uno scope incompatibile.

Il primo gradino della migrazione pipeline e operativo come
`NriPicaPipelineBridge`: le pipeline presenti nella cache Vulkan originale
vengono wrapped una sola volta con `CreatePipelineVK` e ogni bind dei draw
PICA viene registrato tramite `CmdSetPipeline` NRI. I wrapper non possiedono
gli handle Vulkan e vengono distrutti prima della cache originale, anche
durante una ricreazione MSAA. I contatori
`nri_pica_pipeline_bind_count` e `nri_pica_pipeline_wrapped` devono coprire
tutti i draw PICA del frame.

Il secondo gradino rimuove l'incompatibilita strutturale dei tre
`COMBINED_IMAGE_SAMPLER` PICA. `PicaNriShaderContract` conserva i binding
buffer/texture/storage originali 0-6, separa texture e sampler e assegna i
sampler ai binding 7-9. La trasformazione usa macro locali tipizzate
`sampler2D`/`usampler2D`, quindi non modifica le chiamate
`texture`, `texelFetch` e `textureSize` prodotte dal frontend PICA. Il bridge
crea e possiede gia il pipeline layout NRI corrispondente, inclusi i 32 byte
di root constants; ogni shader reale della scena viene trasformato e
compilato in SPIR-V NRI accanto al modulo Vulkan. I contatori
`nri_pica_shader_contract_count` e
`nri_pica_descriptor_layout_owned` verificano la copertura uno-a-uno dei draw.
Il bridge crea ora anche una pipeline grafica NRI parallela per ogni entry
della cache, traducendo topology, culling/front-face, MSAA, depth/stencil,
blend/logic e i cinque MRT. Poiche NRI non espone i formati vertex
`SSCALED`/`USCALED` ne i formati 8/16-bit a tre componenti usati da PICA,
`PicaNriVertexInput` definisce un layout canonico float4 e un repacker esatto
per byte signed/unsigned, short signed e float. La pipeline NRI usa gia questo
layout e il repacker e ora collegato agli upload per produrre stream float4
separati accanto ai byte originali, conservati per un fallback per-draw sicuro.
Due pool descriptor per-frame sono posseduti da NRI e aggiornano i tre constant
buffer, le tre coppie texture/sampler e la storage image Shadow2D del contratto
PICA. Pipeline layout, pipeline, descriptor set, root constants, vertex/index
binding e draw sono registrati tramite NRI nello stesso scope dynamic-rendering
condiviso. `nri_pica_owned_pipeline_draw_count` e
`nri_pica_owned_draw_count` coprono ora ogni draw PICA del frame, incluso il
primo draw dopo ciascuna apertura di scope e anche in MSAA;
`nri_pica_descriptors_owned` certifica il percorso descriptor attivo. Il
resolve multisample NRI e qualificato sulla scena giocabile; il fallback
Vulkan resta esplicito e coperto dai gate, non un ritorno silenzioso a Off.
Anche gli arena per-frame uniform e vertex/index sono buffer committed
`HOST_UPLOAD` posseduti da NRI. `PicaNriUpload` valida prima tutti i range e
poi copia soltanto gli intervalli del draw agli stessi offset; gli arena
Vulkan originali continuano a ricevere gli stessi byte per il fallback
immediato. `nri_pica_owned_upload_draw_count` deve coincidere con gli owned
draw e `nri_pica_upload_bytes` deve essere non zero. Il solo ownership degli
upload si disattiva con `OOT3D_GRAPHICS_NRI_PICA_UPLOADS=0`, senza disattivare
pipeline, descriptor o draw NRI. Il fallback del solo draw/descriptor si forza
con
`OOT3D_GRAPHICS_NRI_PICA_DRAWS=0`, mantenendo pipeline e shader paralleli per
il confronto; il fallback A/B dell'intera migrazione PICA NRI si forza con
`OOT3D_GRAPHICS_NRI_PICA_PIPELINES=0`.

Anche lo staging delle texture PICA e ora isolato in
`NriPicaTextureUploadPass`. Due arena per-frame committed `HOST_UPLOAD`
rispettano gli alignment row/slice esposti dal device NRI; un planner puro
verifica offset, pitch, capacita e overflow prima che il packer copi le righe
RGBA8 senza toccare il padding. Transizioni
`Undefined -> TransferWrite -> ShaderRead` e
`CmdUploadBufferToTexture` sono registrati da NRI sul command buffer gia
wrapped. `NriPicaTextureImageOwner` crea inoltre la texture device-local e la
sampled view come risorse committed NRI, estraendo `VkImage` e `VkImageView`
native per sampler, descriptor e consumer Vulkan ancora attivi. Il record
conserva l'ownership esplicita: descriptor/view, immagine e memoria vengono
distrutti una sola volta da NRI, prima del device interop. La sola allocazione
delle immagini puo tornare a Vulkan con
`OOT3D_GRAPHICS_NRI_PICA_TEXTURE_IMAGES=0`, mentre lo staging puo tornare
selettivamente al percorso originale con
`OOT3D_GRAPHICS_NRI_PICA_TEXTURE_UPLOADS=0`. I contatori
`nri_pica_texture_upload_count`, `nri_pica_texture_upload_bytes` e
`nri_pica_texture_upload_owned` distinguono gli upload iniziali reali dalle
texture gia residenti; `nri_pica_owned_texture_image_count` e
`nri_pica_texture_images_owned` certificano invece l'allocazione NRI.

Le snapshot device-local del display transfer riusano lo stesso owner sampled
con una policy indipendente. Immagine, memoria e sampled view sono committed
NRI; gli handle Vulkan native restano disponibili agli effetti e allo
scanout. Anche il command recording del relativo snapshot e ora isolato in
`NriPicaDisplayCopyPass`: importa sorgente e destinazione quando appartengono
ai fallback Vulkan, registra in due batch due transizioni verso
`TransferRead/TransferWrite` e due di ripristino, quindi esegue
`CmdCopyTexture`. Il layout finale resta identico al percorso originale,
percio effect graph, scanout e render pass legacy non richiedono adattamenti.
La diagnostica
`nri_pica_owned_display_image_count` e
`nri_pica_display_images_owned` misura le creazioni reali;
`nri_pica_display_copy_count`,
`nri_pica_display_copy_barrier_count` e
`nri_pica_display_copies_owned` certificano invece i comandi.
`OOT3D_GRAPHICS_NRI_PICA_DISPLAY_IMAGES=0` ripristina soltanto l'allocazione
Vulkan; `OOT3D_GRAPHICS_NRI_PICA_DISPLAY_COPIES=0` ripristina soltanto copia
e barrier Vulkan. I due fallback sono quindi indipendenti.

I render target PICA sono ora creati transazionalmente da
`NriPicaRenderTargetOwner`: color, normal/material guide, rigid-motion guide,
ambient guide RGB, Shadow2D storage e depth formano un bundle obbligatorio di
sette immagini committed; con MSAA vengono aggiunti cinque color attachment
e una depth multisample, per tredici immagini totali. L'API owned-texture
comune traduce
usage e sample count e crea descriptor sampled/storage/color/depth NRI
tipizzati. Il backend conserva view Vulkan combinate soltanto per il render
pass legacy e le distrugge prima delle immagini NRI; memoria e texture hanno
un solo owner. Il bundle viene validato prima della pubblicazione e un errore
parziale distrugge tutte le immagini gia create prima di usare il fallback
Vulkan. `OOT3D_GRAPHICS_NRI_PICA_RENDER_TARGETS=0` isola il confronto A/B.
`NriPicaRenderTargetInitPass` importa anche i bundle del fallback, porta
direttamente color/depth nei layout attachment, azzera i cinque MRT tramite
scope NRI con load-op clear e inizializza Shadow2D con `CmdClearStorage`.
Shadow viene poi pubblicata in stato storage read/write per tutti gli shader.
Con MSAA il pass ripete lo scope sui cinque attachment multisample, mantenendo
inizializzati anche i resolve single-sample. I comandi sono registrati sul
command buffer di frame wrapped, dopo aver chiuso un eventuale scope del
target precedente: vengono cosi rimossi i submit immediati e i relativi
`vkQueueWaitIdle`. Il fallback esatto resta selezionabile con
`OOT3D_GRAPHICS_NRI_PICA_RENDER_TARGET_INITIALIZATION=0`.

I clear dinamici prodotti dai memory-fill PICA sono ora isolati in
`NriPicaMemoryFillClearPass`. Shadow2D viene portata da storage read/write a
storage clear e ritorno con due barrier NRI e viene azzerata tramite
`CmdClearStorage`; color, normal/material guide, rigid-motion guide e
depth/stencil vengono invece azzerati con `CmdClearAttachments` dentro lo
scope raster NRI gia attivo. Se lo scope e legacy, soltanto il clear degli
attachment torna al comando Vulkan originale, mentre Shadow2D puo restare
NRI. `OOT3D_GRAPHICS_NRI_PICA_MEMORY_FILL_CLEARS=0` forza il fallback
completo per il confronto A/B. I contatori separati
`nri_pica_memory_fill_shadow_clear_count`,
`nri_pica_memory_fill_attachment_clear_count` e
`nri_pica_memory_fill_clear_barrier_count` rendono visibile anche il caso
ibrido. L'hook test-only `OOT3D_GRAPHICS_TEST_PICA_MEMORY_FILL=1`, esposto
dal launcher come `-PicaMemoryFillSmoke`, inietta una sola operazione sulla
prima coppia color/depth reale del checkpoint e non modifica il percorso
normale.

Ogni texture Vulkan wrapped conserva il formato NRI autorevole, usato da tutte
le view sampled, storage, color attachment e depth attachment. La creazione
fallisce prima della registrazione se il formato non e rappresentabile: una
view `Format::UNKNOWN` non puo quindi propagarsi al driver. Questo contratto
tipizzato e condiviso da PICA, scanout, NIS, DLSS e dai pass compute.

Dopo la fondazione NRI/depth e il primo SSAO, la priorita delle applicazioni
visibili e vincolante: **erba interattiva, poi toon, poi riflessi**. L'erba e
un pass geometrico del mondo e non un post-process; puo quindi arrivare senza
aspettare fragment lighting PICA, roughness, motion vector o history temporale.

Il progetto Godot BoTW Toon Shader e utile come riferimento visivo e ha licenza
CC0, ma non e adatto come codice da integrare direttamente: dipende dal modello
`spatial` di Godot e dai built-in `NORMAL`, `VIEW`, `LIGHT`, `ATTENUATION`,
`diffuse_toon` e `specular_toon`. OoT3D dispone invece di TEV PICA, colori di
fragment lighting primario/secondario, LUT, normal quaternion, fog, alpha test
e regole di blend proprie. Il risultato corretto e quindi un'implementazione
renderer-native che conservi quelle semantiche.

## Obiettivi non negoziabili

- Ottenere presto una prima build utilizzabile con SSAO e, subito dopo, erba
  interattiva.
- Distribuire l'erba esclusivamente sulle mesh che usano una o piu texture
  indicate dall'utente, con campionamento, densita e budget configurabili.
- Far reagire l'erba sia a vento procedurale sia al collider autorevole del
  personaggio giocabile Link, senza dipendere dalla posa grafica dello
  scheletro.
- Consegnare cel shading e SSR dopo l'erba, senza eliminarli dalla strategia.
- Aggiungere SSR senza rendere riflettenti materiali che nell'originale non lo
  erano.
- Sfruttare profondita, normali, LUT, colori speculari, bump mapping, TEV, fog,
  alpha test e classificazione dei draw gia presenti nel percorso OoT3D.
- Conservare tutte le funzioni gia previste: AA comune e avanzato, risoluzione
  di output e interna, finestra/borderless/fullscreen esclusivo, 30/60/libero e
  moltiplicatore FOV globale 1.00-1.50.
- Non applicare post-process a HUD, menu, bottom-screen overlay o ImGui.
- Lasciare sempre disponibile un preset `Authentic`, con effetti disattivati,
  usato anche per regression test e bisect.
- Non accelerare simulazione, audio, input o clock guest quando cambia il frame
  rate di rendering/presentazione.

Non sono prerequisiti per la prima consegna la conversione completa del
renderer a deferred rendering, un G-buffer PBR completo, il ray tracing o la
migrazione della swapchain a NRI.

## Ricognizione del renderer reale

### Backend Vulkan

Il renderer e implementato principalmente in:

- `src/fast/backends/gfx_vulkan.cpp`;
- `src/fast/backends/gfx_vulkan_pica.cpp`;
- `include/fast/backends/gfx_vulkan.h`;
- `include/fast/backends/gfx_rendering_api.h`.

Il percorso PICA crea gia un render target offscreen per indirizzo fisico e
namespace. Il `ColorImage` e `VK_FORMAT_R8G8B8A8_UNORM` e include gia
`VK_IMAGE_USAGE_SAMPLED_BIT`. Il `DepthImage`, invece, e creato soltanto come
depth/stencil attachment e transfer destination: deve diventare campionabile o
essere copiato in una texture `R32_SFLOAT` compatibile.

Il ramo `Present == false` di
`SubmitOot3dNativePicaDisplayTransfer` termina il render pass, identifica il
target sorgente e ne produce lo snapshot destinato allo scanout. E il primo
confine stabile in cui inserire un effect graph. Il ramo `Present == true` deve
restare soltanto scanout/composizione, senza ricalcolare gli effetti.

`GfxNativePicaDrawView` espone gia viewport, parametri di profondita, stato di
depth write/test, blend, stencil, formato e indirizzi del framebuffer. Manca
pero un contratto esplicito per:

- matrice di proiezione e inversa;
- identita della camera/view e camera cut;
- classificazione perspective/orthographic;
- classe scena, materiale ed esclusione da effetti;
- matrice view-projection precedente per gli effetti temporali.

### Semantiche originali riutilizzabili

Il frontend PICA del runtime produce gia i varying `pica_normquat` e
`pica_view`. Il generatore fragment espone inoltre le sorgenti TEV
`primary_fragment_color` e `secondary_fragment_color`, conserva il byte
rounding PICA e applica alpha test, fog LUT e profondita PICA.

Il limite attuale e esplicito: il generatore rifiuta ancora il percorso di
fragment lighting e inizializza a zero i due colori di fragment lighting. La
prima SSAO puo funzionare senza risolvere questo blocco, usando depth e normali
ricostruite; il cel shading PICA corretto e la composizione AO corretta
richiedono invece di collegarlo.

I dati CMB gia decodificati includono:

- posizioni, normali, `Uv0..2` e `NativeSourceUv0` per vertice;
- tre indici texture mapper per materiale, sampler e wrap mode;
- nome, dimensioni, mip RGBA8 decodificati e `Rgba8Hash` stabile per texture;
- `NativeGeometryId`, `NativeGeometryContentVersion`, `ModelToWorld`,
  `TransformBakedIntoVertices` e bounds per identita/invalidation/culling;
- flag fragment, vertex e hemisphere lighting/occlusion;
- colori emission, ambient, diffuse, specular 0 e specular 1;
- configurazione e input delle LUT PICA;
- bump mode e texture unit della normal map;
- programmi TEV e constant colors;
- alpha test, depth write/test, blend e culling.

Questi dati sono piu utili di una roughness PBR inventata globalmente. Devono
alimentare una classificazione per draw e profili di materiale, con fallback
conservativo quando la semantica non e risolta.

Sono anche sufficienti a costruire la distribuzione dell'erba prima del
raster: il sampler puo percorrere i triangoli CMB, interpolare le UV e leggere
la texture CPU decodificata. La regola deve usare nome asset piu hash del
contenuto, non indice CMB, handle Vulkan o indirizzo fisico temporaneo.

Il renderer chiama pero
`StripOot3dNativeRenderModelTexturePayloads` per liberare gli RGBA8 dopo
l'upload, conservando hash e byte count. La strategia non deve disabilitare
globalmente questo risparmio: il catalogo usa i metadati persistenti e, quando
l'utente sceglie una texture, un servizio asset la ri-decodifica in background
oppure la intercetta prima dello strip. Rimane residente soltanto una mask
compatta per canale/mip delle regole attive.

Nel runtime nativo esiste inoltre gia una sorgente autorevole per Link:
`LinkInstance` espone actor position, `ColliderRadius` e
`ColliderHeight`; i valori correnti derivano dalla scena e non devono essere
hardcoded nel renderer. Il bridge di gameplay deve pubblicare anche la
posizione precedente, derivabile conservando il campione del tick precedente,
cosi da usare un volume swept ed evitare tunneling a 30 FPS.

## Architettura obiettivo

### 1. NRI interop prima della migrazione completa

Bloccare NRI alla release `v180`, commit
`4b485316463969f182db15e67aad2aec2f40a3d7`. Non dipendere da `main`.

Il backend corrente dichiara Vulkan 1.1; NRI v180 richiede come baseline
Vulkan 1.2 e `VK_KHR_synchronization2`. La prima modifica infrastrutturale e:

1. richiedere Vulkan 1.2 e abilitare synchronization2;
2. creare un device NRI con `nriCreateDeviceFromVKDevice`;
3. avvolgere il command buffer corrente con `CreateCommandBufferVK`;
4. avvolgere i color/depth target PICA con `CreateTextureVK`;
5. mantenere ownership e distruzione degli handle originali nel backend
   Vulkan, invalidando i wrapper quando il target viene ricreato.

Questa regola continua a valere per i target PICA importati. L'output RGBA16F
condiviso dagli upscaler e invece la prima risorsa migrata: NRI crea una texture
committed device-local, ne possiede memoria e descriptor SRV/UAV e restituisce
al backend soltanto `VkImage`/`VkImageView` nativi per interop esterno e
scanout. La
distruzione passa da un'API distinta da `ForgetTexture`, evitando di confondere
wrapper non proprietari e risorse possedute.

La stessa ownership copre ora motion RGBA16F e reactive mask R16F. NRI crea e
distrugge entrambe le texture e le rispettive view sampled/storage; il compute
motion usa direttamente descriptor NRI, mentre TAA, FidelityFX e NGX consumano
le sampled view o gli stessi descriptor senza copie intermedie. Anche uniform
buffer host-upload, sampler, descriptor pool/set, pipeline layout, pipeline
compute e registrazione del dispatch sono ora NRI. Un access bridge interno e
ristretto espone al modulo motion soltanto device, command buffer e view NRI,
senza contaminare l'API pubblica del renderer. Restano temporaneamente Vulkan
le barrier ai bordi del pass, in attesa della migrazione del resource-state
tracker.

I draw PICA usano pipeline, descriptor, root constants, stream vertex/index e
draw posseduti da NRI dentro uno scope dynamic-rendering NRI. La submission
Vulkan originale resta disponibile come fallback A/B per-draw e per hardware
non compatibile; la traduzione PICA e le chiavi della cache rimangono condivise
fra i due percorsi.

NRI non inserisce barrier automaticamente. L'implementazione usa un unico
contratto `ResourceStateTracker`: pianifica senza mutare lo stato, la primitive
interop valida command buffer/texture/subresource ed emette
`nri::TextureBarrierDesc`, poi il tracker effettua il commit. Motion/reactive,
composite, history TAA, Hi-Z per-mip, SSR e output degli upscaler condividono
questo percorso. Non sono ammessi comandi Vulkan e NRI intercalati senza un
confine di stato dichiarato. Le eccezioni esplicite sono CACAO, perche il
context FidelityFX registra direttamente comandi Vulkan. Begin/end rendering
PICA e scanout, barrier globali PICA, pipeline, descriptor e draw sono NRI.

Per hardware privo dei requisiti NRI, il percorso `Authentic` Vulkan puo
restare disponibile; le opzioni avanzate devono risultare non disponibili con
una motivazione esplicita.

### 2. Moduli

- `NriInteropContext`: device wrapper, interfacce NRI, command buffer e cache
  dei wrapper non-owning.
- `SceneSurfaceRegistry`: associa namespace/indirizzi PICA a colore,
  profondita, extent, view metadata e guide buffer.
- `SceneViewBridge`: riceve proiezione corrente/precedente, projection kind,
  view id e camera cut dal runtime.
- `EffectGraph`: compila dipendenze e ordine stabile dei pass, rifiutando
  cicli e dipendenze mancanti.
- `DisplayEffectPlan`: traduce una sola volta stato UI, capability della view
  e override di sviluppo nei nodi CACAO/Hi-Z/reflection/composite/motion/
  TAA/upscaler/SMAA/scanout. Il backend consuma i flag del piano e non
  ridetermina piu le combinazioni; risorse, barrier e timestamp restano
  responsabilita dei moduli dei singoli pass.
- `DepthPreparationPass`: depth-only view o copia `R32_SFLOAT`, linearizzazione,
  normali da depth e piramide Hi-Z.
- `GrassSurfaceExtractor`: trova le superfici CMB ammissibili e campiona
  deterministicamente texture e triangoli.
- `GrassSceneBridge`: registra modelli ambientali attivi, room lifetime e
  trasformazioni locali/world senza dedurli dai draw raw.
- `GrassTextureSourceCache`: cataloga identita persistenti, acquisisce o
  ri-decodifica on demand gli RGBA8 prima/dopo lo strip e conserva soltanto le
  mask scalari richieste dalle regole attive. Espone anche la media RGB
  alpha-weighted della texture assegnata, nello stesso dominio UNORM del
  target PICA.
- `GrassPlacementCache`: conserva anchor locali per chiave
  geometria/texture/regola e sostituisce le ricostruzioni in modo atomico.
  Gli snapshot sono immutabili e condivisi: il frame non ricopia piu tutti
  gli anchor. Ogni snapshot include cluster spaziali deterministici per il
  culling gerarchico.
- `GrassInteractionBridge`: trasferisce collider, posizione corrente e
  precedente di Link dal gameplay al render frame.
- `GrassInteractionField`: campo locale compute per impulso, persistenza e
  ritorno elastico dopo il passaggio di Link.
- `InteractiveGrassPass`: culling gerarchico, LOD stabile per anchor, vento,
  interazione e shading delle blade nel world target. Le blade scrivono
  depth e guide normali/ambientali, cosi si occludono reciprocamente e
  partecipano ai consumer della scena.
- `CacaoPass`: adapter FidelityFX CACAO con preset, target NRI e fallback.
- `LinearSceneColorPass`: conversione dichiarata UNORM/sRGB verso il working
  color lineare RGBA16F, con ownership e barrier NRI.
- `HiZReflectionPass`: SSR corrente-frame con confidence mask e filtro
  spaziale.
- `FidelityFxSssrPass`: adapter SSSR temporale che consuma working color,
  depth, motion e guide, con fallback per-view verso Hi-Z.
- `PicaReflectionEnvironmentAccumulator`: estrae fog/global ambient dai draw
  world idonei e mantiene un profilo low-frequency stabile per target.
- `ReflectionIblPass`: genera e possiede via NRI environment cube GGX e LUT
  split-sum BRDF, indipendenti dal lifetime screen-size di SSSR.
- `ReflectionMaterialResolvePass`: applica il contratto materiale PICA alla
  radiance FidelityFX e pubblica RGB+weight per composite e scanout.
- `ReflectionMaterialProfile`: modello e resolver pure-CPU delle regole
  hash/dimensioni/mapper slot e dei preset acqua/metallo/polished/custom.
- `TextureCatalogViewer`: catalogo e preview ImGui condivisi; i moduli grass e
  reflection aggiungono badge e azioni senza duplicare ordinamento o stato.
  Selezione e preview non mutano le regole finche l'utente non sceglie
  esplicitamente un'assegnazione.
- `TexturePreviewArtifact`: esportatore BMP di sviluppo, opt-in e indipendente
  dalla UI, alimentato dagli stessi texel RGBA decodificati del viewer.
- `ReflectionMaterialEditor`: controlli modulari dei parametri della sola
  regola reflection selezionata.
- `PicaGuidePass`: produce normali e proprieta materiale dai draw PICA.
- `PicaGuideSamplingBarriers`: definisce in un solo modulo le cinque
  transizioni depth/normal/material/rigid-motion/ambient tra attachment e
  shader-read. Il ritorno al raster attende sia compute sia fragment, coprendo
  `SceneCompositePass` e scanout diretto senza inversioni manuali nel backend.
- `PicaToonPolicy`: modifica il fragment lighting prima del TEV.
- `ToonCompositePass`: posterizzazione opzionale e outline depth/normal aware.
- `PicaScanoutPolicy`: costruisce i push constants finali senza dipendenze
  Vulkan e decide se AO/riflessioni sono gia incorporati nel temporal output;
  il backend si limita a binding e draw.
- `TemporalHistoryManager`: motion vector, history, camera cut e disocclusion;
  entra in uso solo nelle milestone temporali.
- `GraphicsSettingsService`: stato current/pending/last-known-good, capability,
  persistenza e applicazione transazionale.

La swapchain resta fuori da `EffectGraph`: ora e posseduta dal modulo
`NriSwapchain`, separato dai pass avanzati e sostituibile atomicamente con il
fallback Vulkan.

Il planner per-view e collegato al display transfer reale. Gli overlay alpha
producono soltanto il nodo scanout; la scena avanzata ordina deterministicamente
guide, CACAO, Hi-Z/riflessioni, outline, composite, motion e consumer
temporale prima dello scanout. I test puri qualificano ordine, cicli, gate
SMAA e soppressione overlay; il gate giocabile conserva la parita
Authentic/Advanced.

### 3. Contratto di view

Il moltiplicatore FOV e gli effetti devono usare la stessa matrice effettiva.
Estendere il bridge centrale del frustum con un contratto equivalente a:

```cpp
enum class ProjectionKind { Unknown, Perspective, Orthographic };

struct SceneViewInfo {
    uint64_t ViewId;
    uint64_t RenderTargetNamespace;
    uint32_t ColorPhysicalAddress;
    uint32_t DepthPhysicalAddress;
    ProjectionKind Projection;
    float ProjectionMatrix[16];
    float InverseProjectionMatrix[16];
    float ViewProjectionMatrix[16];
    float PreviousViewProjectionMatrix[16];
    bool CameraCut;
};
```

La view va associata alla submission/target, non letta da uno stato globale al
momento del present: cutscene, mirror, sottocamere e bottom screen possono
produrre matrici diverse nello stesso refresh.

### 4. Classificazione dei draw e guide buffer

Introdurre classi esplicite, senza affidarsi soltanto a euristiche nel
post-process:

```cpp
enum class SceneDrawClass {
    Unknown,
    WorldOpaque,
    WorldAlphaTested,
    InteractiveFoliage,
    WorldTranslucent,
    Sky,
    EffectParticle,
    HudOrthographic,
    FrontendOverlay
};
```

Il frontend compila per ogni draw anche un piccolo record di effect metadata:

- `aoWeight`;
- `reflectivity`;
- `roughnessProxy`;
- `toonGroup` e outline enable;
- `emissive`, `receivesAo`, `receivesReflection`;
- projection kind e draw class.

Le superfici sorgente dell'erba ricevono separatamente `grassRuleId` e
`grassEligible`. Le istanze generate sono classificate
`InteractiveFoliage`, scrivono depth e coverage, ma hanno reflectivity zero
di default.

La classificazione usa, in ordine:

1. semantica esplicita del runtime/asset quando disponibile;
2. stato PICA e firma stabile del materiale/pipeline;
3. un database di override per materiali verificati, mai l'indice CMB locale;
4. fallback sicuro: niente SSR, niente toon materiale e AO ridotta.

Il primo vertical slice puo usare normali ricostruite dalla depth. La fase
PICA-aware aggiunge un attachment guida con normale octahedral/view-space e
proprieta materiale. Il formato definitivo va scelto con una prova di banda e
compatibilita; evitare un G-buffer PBR completo finche non serve.

Quando gli effetti sono disattivati, il render pass e le pipeline devono usare
la configurazione originale a due attachment, senza costo guida e senza
alterare le chiavi shader autentiche.

### 5. Confine world/overlay

Eseguire l'erba dopo le superfici opaque/alpha-tested che la sostengono e prima
della chiusura del mondo. Eseguire poi SSAO e SSR prima di trasparenze,
particelle, HUD e ImGui. Il display transfer rimane il fallback tecnico, ma non
e sufficiente da solo a garantire che HUD e riflessi non si contaminino.

Il runtime deve quindi emettere un marker `FinalizeWorldSurface` associato al
target e, subito prima, un hook `SubmitInteractiveWorldGeometry` nel quale
registrare il draw grass sullo stesso color/depth target. La prima
implementazione puo derivare il confine dalla transizione verificata
perspective-to-orthographic e dagli stati depth/blend, ma la decisione deve
essere registrata nei diagnostici e coperta da test. Se il marker non e
affidabile:

- l'erba non viene disegnata dopo HUD/overlay e la capability
  `WorldGeometryInsertionPoint` risulta assente;
- SSAO preview puo essere composta al display transfer usando il coverage mask;
- SSR resta disabilitato per quel frame/target;
- HUD e overlay conservano sempre il colore originale.

Non riordinare draw PICA per creare artificialmente il confine.

## Ordine del frame

L'ordine finale per il top-screen world target e:

1. raster PICA opaque/alpha-tested verso colore e profondita originali;
2. aggiornamento compute del `GrassInteractionField`, culling e LOD;
3. draw dell'erba alpha-tested nel colore/depth/guide del mondo;
4. resolve MSAA di colore, profondita e guide, quando attivo;
5. preparazione depth e piramide Hi-Z condivisa;
6. CACAO;
7. SSR Hi-Z e filtro/confidence;
8. composizione AO/riflessioni e outline sullo snapshot scena quando un
   consumer avanzato richiede il working target;
9. draw PICA translucent, particelle ed effetti che devono stare sopra;
10. TAA o upscaler temporale, quando disponibile;
11. SMAA 1x, se selezionato come AA spaziale finale;
12. FXAA nello scanout, se selezionato;
13. HUD, bottom-screen overlay e ImGui alla risoluzione di output;
14. scanout sulla swapchain e present.

Nelle prime release, prima della separazione robusta world/overlay, i pass 5-8
e 11 possono essere eseguiti al display transfer dietro l'opzione
`Experimental`,
con coverage mask e fallback automatico.

Il target PICA `R8G8B8A8_UNORM` va conservato per byte rounding, blend e parita
con l'hardware originale. Gli effetti possono convertire uno snapshot in un
working target `R16G16B16A16_SFLOAT`; non cambiare direttamente il target PICA
in HDR. Il contratto operativo non presume implicitamente sRGB: la policy
interroga la modalita del raster PICA, converte nello spazio lineare prima di
SSSR e dichiara separatamente se lo scanout debba codificare sRGB.

## Erba interattiva texture-driven

### Scelta e perimetro della prima release

Implementare un sistema nativo, non integrare NVIDIA Turf Effects: quest'ultimo
dimostra che geometria, LOD e interazione fisica sono una direzione valida, ma
e un SDK legacy DX11 e introdurrebbe un secondo modello di risorse incompatibile
con il percorso Vulkan/NRI.

La prima versione usa:

- placement CPU asincrono sui triangoli CMB gia decodificati;
- anchor compatti caricati in un instance/storage buffer;
- due o tre quad incrociati per ciuffo, istanziati in pochi draw;
- vento procedurale nel vertex shader;
- campo di interazione compute centrato su Link;
- alpha test/dither con depth write, e alpha-to-coverage quando MSAA e attivo;
- culling frustum/distanza e LOD per budget.

Questa soluzione segue la tecnica consolidata di pochi poligoni incrociati e
animazione per grass object descritta in GPU Gems, ma la adatta all'hardware
attuale usando instancing e NRI. Non usare geometry shader, una draw call per
ciuffo o simulazione fisica CPU per blade.

Il primo target sono room e mesh ambientali statiche con topologia, UV,
materiale e trasformazione risolti. Mesh skinned, superfici deformabili e draw
visibili soltanto come stream PICA raw sono escluse finche il loro producer non
fornisce lo stesso contratto. Non tentare di ricostruire il placement dal
framebuffer.

Lo stato implementato collega ogni superficie pubblicata allo snapshot PICA
che l'ha prodotta. Le tre luci CMB vertex-lit vengono decodificate dagli
uniform nativi e trasformate dallo spazio view allo spazio world; fog color,
flip, depth scale/offset, W-buffering e LUT a 128 ingressi riusano il medesimo
contratto del fragment shader PICA. Se l'evidenza nativa manca, luce e fog
restano neutre invece di introdurre valori di scena inventati.

Le regole separano due forme di distribuzione: jitter individuale
stratificato e gruppi continui world-space, ciascuno con forza, scala,
copertura e seed propri. Lo spacing minimo e applicato con una griglia
spaziale. Prima di vento, collisione e generazione geometrica il pass scarta
cluster e blade fuori frustum/distanza; il far field riduce
deterministicamente densita, segmenti e piani fino a un billboard rivolto
alla camera. La media texture puo essere miscelata con i colori root/tip e
ha controlli indipendenti di luminosita.

Sul checkpoint Kokiri `hudtest_after120`, con la stessa regola e i primi 60
frame di presentazione, culling+LOD riducono il massimo stabilizzato da 9.960
a 2.027 blade. Nei campioni GPU disponibili il pass scende da circa 0,79 a
0,18 ms; la validazione visiva interna A/B di luce+fog+media texture modifica
il 6,18% del framebuffer mantenendo invariati draw PICA e numero di blade.

`GrassSceneBridge` usa direttamente `NativeGeometryId`,
`NativeGeometryContentVersion`, `ModelToWorld` e
`TransformBakedIntoVertices`. Registra/unregistra le istanze con il lifetime
della room; un cambio content version invalida gli anchor. Un producer raw
PICA puo diventare ammissibile solo pubblicando source geometry id,
material/texture identity e local-to-world equivalenti.

### Identita della texture e regole di selezione

Una regola seleziona le mesh il cui materiale usa la texture indicata in uno
dei tre `TextureMapperTextureIndices`. Il selettore persistente e:

```cpp
struct GrassTextureSelector {
    std::string AssetName;
    uint64_t Rgba8Hash;
    uint16_t Width;
    uint16_t Height;
    uint8_t MapperSlotMask; // bit 0..2
};
```

L'hash del payload decodificato e l'identita primaria; nome e dimensioni
servono per disambiguazione, diagnostica e migrazione. Indici locali,
`VkImage`, indirizzi guest e descriptor non sono identita persistenti. La UI
deve mostrare preview, nome, hash, dimensioni, slot materiale e numero di mesh
che corrispondono prima di applicare la regola.

`GrassTextureSourceCache` non presume che `Rgba8` sia ancora nel render
model. Se i pixel sono gia stati strippati, riapre la sorgente asset e
ri-decodifica soltanto texture/mip richiesti su worker CPU. Dopo la conversione
in mask a 8 o 16 bit puo rilasciare gli RGBA; la UI mostra `Loading mask` e
non blocca il render thread. Il readback dalla texture GPU e solo un fallback
diagnostico, non il percorso normale.

Se il materiale usa una texture animata:

- default: campionare `NativeSourceUv0` con la trasformazione statica
  authored e mantenere immobile la distribuzione;
- una regola puo scegliere esplicitamente l'UV effettiva al momento del load;
- animazione UV o cambio frame non devono spostare ogni frame i ciuffi;
- una texture senza identita/pixel decodificati e dichiarata non supportata,
  non sostituita con un match euristico.

Separare sempre la **texture di distribuzione**, presa dal materiale della
mesh, dalla **texture/atlas delle blade**. Il colore del terreno campionato
puo essere usato come tint opzionale, ma non deve diventare implicitamente la
grafica del filo d'erba.

### Campionamento configurabile e deterministico

Per ogni regola esporre:

- canale `R`, `G`, `B`, `A` o luminanza;
- invert, soglia minima/massima, gamma/curva e density multiplier;
- mip esplicito; mip 0 e il default;
- sampler del materiale come default, con override clamp/repeat/mirror;
- UV authored o effective-static e mapper slot mask;
- densita per unita di area, distanza minima e seed;
- altezza/larghezza minima e massima delle blade;
- pendenza massima, normal offset e limite istanze per mesh/room;
- distanza di draw e soglie LOD.

Pipeline di placement:

1. filtrare i batch statici che referenziano la texture e lo slot richiesti;
2. calcolare area e normale di ogni triangolo nello spazio di placement,
   scartando degenerati e pendenze non ammesse;
3. generare candidati proporzionalmente all'area, con sequenza stratificata
   deterministica e coordinate baricentriche;
4. interpolare UV e normale, applicare il wrap scelto e campionare il mip RGBA8
   CPU;
5. trasformare il valore attraverso channel/invert/threshold/gamma e usarlo
   come probabilita/densita;
6. applicare minimum spacing con una griglia locale e produrre gli anchor;
7. trasformare gli anchor locali per ogni istanza della room/mesh.

Il seed deriva da identita scena/room/modello/mesh/triangolo piu hash della
regola. La stessa scena e la stessa configurazione devono produrre gli stessi
anchor tra avvii, 30/60/libero e GPU diverse. La densita e area-based, quindi
non deve cambiare sensibilmente se una superficie equivalente viene
ritessellata.

Traslazioni e rotazioni rigide riusano gli anchor locali. Se `ModelToWorld`
contiene scala, l'area va corretta nello spazio world; una scala non uniforme
entra nella chiave di placement o forza una cache per istanza. Non applicare
una densita dichiarata per area locale fingendo che sia world-space.

La cache usa:

```text
NativeGeometryId + NativeGeometryContentVersion + transformScaleClass
+ textureHash + scalarMaskHash + ruleHash + samplerVersion
```

I cambi a vento, collisione, tint, distanza o LOD sono uniform e immediati.
Texture, canale, UV, soglia, gamma, densita, spacing, slope o seed invalidano
soltanto le entry interessate. La ricostruzione avviene su worker CPU; la cache
vecchia resta visibile finche la nuova non e pronta e lo swap dei buffer
avviene a fine frame. Nessun hitch sincrono nel menu.

### Geometria, shading e integrazione PICA

Un anchor GPU contiene almeno posizione locale, normale/tangente, valore della
mask, scala, rotazione casuale e seed. Le blade:

- fissano la radice e aumentano bend/rumore verso la punta;
- sono two-sided;
- usano vicino 3 piani, medio 2, lontano 1 o cluster impostor;
- fanno fade dither tra LOD, mai pop per sostituzione netta;
- scrivono depth, world coverage e normal/material guide;
- ricevono fog, ambient/hemisphere e tint della scena originale quando questi
  dati PICA sono disponibili;
- hanno `receivesAo=true`, `receivesReflection=false` e reflectivity zero
  per default.

Il pass avviene prima del resolve depth e di CACAO: l'erba puo quindi ricevere
AO e contribuire correttamente a depth/occlusione. Per MSAA usare
`alphaToCoverageEnable` quando supportato dalla pipeline; senza MSAA usare
alpha test con fade/dither stabile. Le reactive/transparency mask temporali
devono includere le blade mosse da vento o Link.

La prima release puo usare un draw indiretto per LOD/bucket dopo un compute
cull. Se la capability non e disponibile, deve restare un numero piccolo e
deterministico di draw istanziati per room e LOD, non uno per anchor.

### Vento

Il vento somma una componente direzionale e una variazione spaziale per
istanza, seguendo la separazione tra main bending e dettaglio usata nella
vegetazione di Crysis:

- direzione e intensita globale;
- velocita, scala spaziale, ampiezza delle raffiche e turbolenza;
- stiffness e fase per anchor;
- fattore root-to-tip e limite massimo di piega;
- eventuali volumi vento locali in una release successiva.

Il calcolo usa posizione world piu seed dell'anchor, cosi il movimento non
segue la camera e ciuffi vicini non risultano perfettamente sincronizzati. Il
tempo proviene da un `VisualClock` del renderer controllato da pausa,
savestate e cambio scena, non da `steady_clock`: velocita e fase apparente
restano uguali con cap 30, 60 o libero.

### Collisione con Link

Definire un contratto piccolo, indipendente dal tipo runtime concreto:

```cpp
struct GrassInteractor {
    uint64_t StableId;
    float PreviousBase[3];
    float CurrentBase[3];
    float Velocity[3];
    float Radius;
    float Height;
    bool Teleported;
};
```

`GrassInteractionBridge` crea l'interactor `PlayerLink` dalla actor
position e dal collider corrente di gameplay. Non campiona bone, bounding box
visiva o valori fissi: forme child/adult o stati speciali devono propagare le
dimensioni effettive.

Per persistenza e ritorno elastico, usare una texture locale
`RGBA16_SFLOAT` in spazio XZ attorno a Link:

- `RG`: vettore di piega;
- `BA`: velocita del campo;
- origine snapped alla griglia texel per evitare swimming;
- update compute come molla smorzata verso zero;
- splat del cilindro/capsula swept tra posizione precedente e corrente,
  modulato dalla velocita;
- controllo verticale della blade contro base/height di Link;
- clamp dell'impulso e della piega massima.

Il volume swept impedisce che Link attraversi l'erba senza toccarla durante
corsa, rotolata o frame a 30 Hz. Il field viene traslato per celle intere
quando Link si muove; teletrasporto, savestate, cambio room o salto oltre la
copertura lo azzerano. Un fallback senza compute applica la deformazione
diretta del volume swept e ritorna subito a riposo: reagisce correttamente, ma
non conserva la scia.

Parametri configurabili:

- raggio e altezza come moltiplicatori del collider autorevole;
- forza push e contributo velocita;
- spring, damping, recovery time e max bend;
- raggio world e risoluzione del field;
- debug di collider, swept volume, field e vettore finale.

Il contratto e gia estendibile a NPC, cavallo, esplosioni o vento locale, ma la
release prioritaria processa soltanto Link per mantenere costo e scope
controllati.

## SSAO: scelta e integrazione

### Scelta

Usare FidelityFX CACAO come implementazione primaria.

Motivi:

- supporta GLSL e si adatta al backend Vulkan;
- richiede depth e matrici, mentre il normal buffer e opzionale;
- puo ricostruire le normali dalla profondita nel primo vertical slice;
- offre cinque livelli di qualita e un percorso downsampled;
- ha un'integrazione e una licenza esplicite nel progetto FidelityFX.

XeGTAO resta un riferimento qualitativo, ma non e la prima scelta: il progetto
Intel e archiviato, e il codice di integrazione ufficiale e orientato a
DirectX/HLSL. Non introdurre entrambi prima di avere metriche che dimostrino un
vantaggio reale.

### Consegna incrementale

1. Rendere sampleable la depth PICA. `FindDepthFormat` deve verificare sia
   `DEPTH_STENCIL_ATTACHMENT` sia sampling; se non esiste un formato comune,
   usare una copia/conversione `R32_SFLOAT`.
2. Validare depth PICA, Z/W-buffering e inverse projection con debug view di
   depth raw, linear depth e world/view position.
3. Eseguire CACAO Low/Medium con normali ricostruite dalla depth.
4. Comporre in modo conservativo sul colore finale solo dove `receivesAo` e
   coverage world sono validi.
5. Quando il fragment lighting PICA e collegato, applicare AO al contributo
   ambientale/hemisphere prima del TEV, evitando di oscurare emission e luce
   diretta.
6. Usare il normal guide PICA nelle qualita superiori.

I punti 3-6 sono operativi: il contatore
`cacao_normal_guide_pass_count` distingue il ramo Medium PICA-aware dal
fallback con normali ricostruite, senza dedurre il comportamento dalla sola
impostazione UI.

Quando l'erba e attiva, la depth/coverage alpha-tested delle blade entra negli
input CACAO; il material guide impedisce pero che una quad trasparente venga
trattata come superficie piena fuori dalla sua coverage.

Il moltiplicatore post-process del primo rilascio deve essere chiaramente
marcato come approssimazione; non deve diventare il percorso definitivo.

Nel percorso operativo CACAO viene pianificato quando il presentation transfer
ha gia risolto l'associazione esatta tra display image e depth/guide target.
Non deve essere eseguito sul primo transfer del frame che supera una soglia di
dimensione: quell'euristica puo scegliere una surface secondaria e separare AO
dal composite usato da TAA/FSR/DLSS. Le transizioni del target CACAO rendono la
scrittura compute visibile sia ai consumer compute sia allo scanout fragment.

Checkpoint implementato: `CacaoSettingsProfile` e il solo mapping tra
`EffectsSettings` e il pass FidelityFX. Qualita, radius, strength, shadow
power/clamp, horizon threshold, fade, blur, sharpness e detail strength
attraversano un contratto puro testabile prima di essere tradotti nella
struttura nativa CACAO. L'override `OOT3D_GRAPHICS_CACAO_STRESS` e
deliberatamente non persistente e serve soltanto alla qualifica automatica.

`tools/Test-Oot3dCacaoControls.ps1` confronta profilo corrente e stress sulla
stessa scena con CACAO Medium: otto frame e 1.402 draw per caso, sette pass AO,
parita PICA/NRI e validation pulita. Lo stress modifica il 78,513% dei pixel
campionati con delta canale medio 19,096, dimostrando che i controlli avanzati
non sono soltanto stato UI.

## Screen-space reflections: scelta e integrazione

### Perche Hi-Z ha preceduto FidelityFX SSSR

FidelityFX SSSR e un'ottima destinazione high-quality, ma richiede color buffer
direttamente illuminato, depth, normali, roughness, motion vector, environment
map e BRDF LUT. Il renderer PICA corrente produce colore LDR e depth, ma non un
G-buffer PBR ne motion vector. Forzare valori sintetici globali produrrebbe
riflessi instabili e materiali visivamente errati.

### Prima implementazione: `HiZReflectionPass`

Costruire un SSR NRI mirato ai dati realmente disponibili:

- piramide di profondita `R32_SFLOAT`, condivisa con la preparazione AO;
- normale da depth nel prototipo, normal guide PICA appena disponibile;
- ray marching view-space gerarchico con max distance, max steps e thickness;
- snapshot colore immutabile come sorgente, per evitare feedback;
- reflectivity/roughness proxy per materiale;
- edge fade, hit confidence, rejection di sky/HUD/particle;
- foliage interattivo non riflettente per default, pur mantenendolo come
  occluder depth;
- filtro bilaterale spaziale guidato da depth/normal;
- pattern deterministico nel percorso senza history, per non introdurre
  scintillio a 30 FPS.

Stato implementato del pyramid: la texture mipmapped `R32_SFLOAT` e committed
e posseduta da NRI; sampled view completa e SRV/UAV per singolo mip vengono
create dallo stesso owner. Sampler, descriptor, root constants, pipeline e
tutti i dispatch di riduzione sono NRI. Le barrier tra mip e ai bordi sono NRI
e vengono tracciate separatamente per mip: dopo ogni dispatch il mip prodotto
diventa leggibile dal livello successivo, senza una transizione finale
whole-image sovrapposta. Le dimensioni seguono ora esattamente la mip chain
Vulkan, con divisione intera per due e al massimo
`floor(log2(max(width,height)))+1` livelli. La riduzione ripartisce
proporzionalmente i texel sorgente quando una dimensione e dispari, quindi
copre anche l'ultima riga/colonna senza richiedere un livello piu grande di
quello ammesso dall'immagine.

Anche `HiZReflectionPass` e ora migrato: raw ray-march target e filtered target
RGBA16F sono texture committed NRI. Le due pipeline ray march e filtro
bilaterale, i sampler lineare/nearest, descriptor, root constants e dispatch
sono NRI. Anche le transizioni raw-write, raw-read, filtered-write e
filtered-read usano il resource-state tracker e texture barrier NRI; il
filtered target continua a essere consumato senza copie da composite, TAA e
upscaler.

Iniziare con profili espliciti e verificati, per esempio acqua e materiali con
segnale speculare PICA affidabile. `Unknown` non riceve riflessi. Un debug view
deve mostrare material class, roughness proxy, ray hit e confidence.

### Provider FidelityFX SSSR operativo

Il renderer integra ora FidelityFX SDK 1.1.4 / SSSR 1.5.0 come provider
modulare separato da `HiZReflectionPass`. `ReflectionProvider` seleziona
esplicitamente `Off`, `HiZ` o `FidelityFxSssr` per ogni view; se motion,
history temporale o depth compatibile non sono disponibili, il piano degrada
alla reflection Hi-Z senza disattivare globalmente l'effetto. Le view con
W-buffer, non compatibili con SSSR, seguono lo stesso fallback.

L'adapter SSSR consuma il working color lineare RGBA16F, depth D32, motion
RGBA16F e normal/material guide gia pubblicate dal renderer. Working input e
target output RGBA16F sono committed e posseduti da NRI, mentre il context
FidelityFX usa gli handle Vulkan nativi estratti dalle risorse condivise.
Output, validita della history, input lineare, ownership NRI e motivazione del
fallback sono esposti nella diagnostica.

`LinearSceneColorPass` preserva il colore scena RGBA8 originale e ne converte
lo snapshot immutabile in RGBA16F. Il pass usa una policy testabile che segue
la codifica effettiva del raster PICA e non tratta automaticamente ogni UNORM
come sRGB. Il risultato attraversa sia lo scanout SSSR diretto, sia
composite+TAA/upscaler, con conversione finale coerente al formato swapchain.

L'environment map non e piu una risorsa neutra. Un accumulatore per render
target osserva fog e global ambient dagli uniform PICA dei draw world
depth-writing, conserva l'ultimo profilo valido e fornisce un fallback
deterministico. `ReflectionIblPass` genera via compute NRI una cube RGBA16F
32x32 a sei mip, prefiltrata GGX, e una LUT BRDF RG16F 64x64 split-sum. Le
risorse sono committed e possedute da NRI; descriptor, pipeline, dispatch e
barrier sono anch'essi NRI. La firma quantizzata del profilo evita di
rigenerare le risorse quando l'ambiente e invariato.

Il denoiser SSSR scrive radiance ma non promette una confidence utilizzabile
nel canale alpha. Per questo `ReflectionMaterialResolvePass` e un modulo
separato: campiona normal/material guide e LUT BRDF, ricostruisce NdotV e
scrive RGB radiance piu un peso alpha derivato da coverage B, reflectivity R e
roughness G. Composite e scanout consumano esclusivamente questo output, non
l'alpha grezzo FidelityFX.

Prima della qualifica visiva `Ultra` restano:

- identificazione e calibrazione multi-scena degli hash effettivi di acqua,
  metallo e superfici polished; il default resta deliberatamente non
  riflettente. Il workspace corrente contiene un solo checkpoint Kokiri:
  l'inventario per-texture e il gate parametrico sono operativi, ma la
  copertura non viene dichiarata multi-scena finche non esistono savestate
  distinti;
- illuminazione HDR, exposure e tone mapping, se adottati dal profilo;
- estensione del confronto misurato di qualita, costo e stabilita contro Hi-Z
  alle future scene acqua/metallo. Il confronto deterministico sul checkpoint
  Kokiri e gia operativo.

Il percorso Hi-Z resta sempre disponibile su hardware o viste che non
soddisfano i requisiti temporali. L'SDK e pinned e compilato come dipendenza
dedicata; le compatibility patch Vulkan sono tracciate, idempotenti e limitate
all'adapter: alias core Vulkan 1.1, selezione FP32 quando `shaderFloat16` non e
abilitato sul device esistente e descriptor pool completo per gli storage
buffer.

## Cel shader: `PicaToon`

### Decisione

Non effettuare un port letterale del Godot BoTW shader. Adottarne, grazie alla
licenza CC0, soltanto idee verificabili come soglie morbide, rim light e
variazione controllata delle bande.

`PicaToon` e composto da due livelli.

### Livello 1: `PostToonPreview`

Disponibile soltanto dopo la release dell'erba interattiva:

- quantizzazione in 2-6 bande del contributo di illuminazione PICA
  (`pica_primary_color` nella preview), mai del texel risultante dal TEV;
- shadow tint e saturazione configurabili;
- outline da discontinuita depth;
- esclusione tramite world/HUD coverage mask.

Serve a consegnare subito uno stile visibile e a tarare l'UI, ma non sostituisce
la luce per materiale. Il colore e i gradienti interni dell'albedo restano
intatti dentro ogni banda: la preview applica al risultato TEV soltanto il
rapporto tra illuminazione continua e illuminazione quantizzata. Rim e shadow
tint sono contributi separati e non cambiano il criterio di campionamento delle
texture.

### Livello 2: `PicaMaterialToon`

Collegare il fragment lighting PICA e calcolare i contributi primario e
secondario usando:

- `pica_normquat` e `pica_view`;
- luci e colori originali;
- LUT e scale PICA;
- colori diffuse/specular 0/specular 1;
- bump normal map quando riconosciuta;
- self-shadow e shadow2D gia previsti dal renderer.

Applicare la quantizzazione al termine del fragment lighting, prima che
`primary_fragment_color` e `secondary_fragment_color` entrino nel programma
TEV. Conservare nell'ordine originale:

1. texture sampling e normal mapping;
2. fragment lighting PICA;
3. quantizzazione toon/rim opzionale;
4. TEV e byte rounding;
5. alpha test;
6. fog LUT;
7. depth, blend e output.

In questo modo il cel shading non distrugge animazioni dei materiali, constant
colors, fog, trasparenze o maschere originali.

L'outline finale usa depth, normal guide e `toonGroup`. Va composto dopo il
resolve temporale per restare nitido; se si usa FXAA/SMAA, questi possono
levigare l'outline come ultimo AA spaziale. Sky, particelle, HUD e materiali
opt-out non generano linee.

L'erba usa un `toonGroup` foliage separato: inizialmente riceve tint/fog PICA
ma non outline per singola blade; dopo la validazione artistica puo adottare
bande diffuse leggere senza aumentare il contrasto alpha.

Parametri principali:

- light bands 2-6;
- band softness;
- shadow tint/intensity;
- rim strength/width/tint;
- outline width, depth sensitivity e normal sensitivity;
- opzionale shadow warble, disattivato di default e ancorato a coordinate
  stabili per evitare shimmering.

## Antialiasing e upscaling

Conservare le modalita gia previste:

```cpp
enum class AntiAliasingMode {
    Off,
    Fxaa,
    Smaa1x,
    Msaa,
    Taa,
    Upscaler
};
```

`Upscaler` seleziona separatamente provider (`Nis`, `Fsr`, `Xess`, `Dlss`) e
quality mode. Questa separazione evita di modellare erroneamente NIS come TAA e
permette di mantenere indipendenti capability e requisiti delle guide.

Ordine di consegna:

1. Off e FXAA nel primo effect graph;
2. SMAA 1x;
3. MSAA 2x/4x/8x reale, con sample count capability-gated e resolve di colore,
   depth e guide;
4. TAA interno dopo motion vector/history;
5. provider `NRIUpscaler` (NIS, FSR, XeSS e DLSS) solo quando tutti gli input
   richiesti dal provider sono validi; DLSS-RR resta non selezionabile finche il
   renderer non produce gli input di ray reconstruction appropriati.

Il checkpoint SMAA 1x e operativo sul preset High dello shader ufficiale
`iryoku/smaa`, bloccato al commit
`71c806a838bdd7d517df19192a20f0c61b3ca29d` e distribuito secondo licenza MIT.
`Smaa1xPass` mantiene separati edge detection, blending-weight calculation e
neighborhood blending; possiede via NRI le tre pipeline, edge/weight/output,
sampler, descriptor e barrier. AreaTex 160x560 e SearchTex 64x16 sono embedded
come payload RGBA8 compresso, decodificati con controllo dimensionale e
caricati una sola volta riusando `NriPicaTextureUploadPass`. Un errore di
shader, lookup o capability disabilita soltanto SMAA: non viene riattivata
l'approssimazione rimossa e FXAA, MSAA, TAA e Authentic restano indipendenti.
Il pass viene eseguito dopo il `SceneCompositePass`, cosi AO e outline sono
gia presenti nell'input, e il risultato RGBA16F viene consegnato allo scanout
prima di HUD e ImGui.

NIS e uno scaler/sharpener, non TAA. TAA e temporal upscaler sono mutuamente
esclusivi. Anche MSAA piu temporal upscaler va trattato come combinazione
avanzata non predefinita, perche aumenta il costo e complica depth/guide
resolve.

Il provider NIS e operativo tramite `NRIUpscaler`: ShaderMake genera le
permutazioni SPIR-V durante la build, il renderer avvolge command buffer e
texture Vulkan esistenti, scrive un output RGBA16F separato e lo consegna allo
scanout prima di HUD e ImGui. La quality determina soltanto la scala effettiva
del mondo; il moltiplicatore scelto dall'utente rimane conservato e torna in
vigore uscendo dalla modalita upscaler. Ogni provider mantiene una capability
distinta e viene selezionato soltanto se il relativo SDK/runtime e disponibile.
Il contratto Vulkan e ora coerente end-to-end: la patch
`oot3d_nri_vulkan12_shaders.cmake` forza ShaderMake a SPIR-V compatibile con il
device Vulkan 1.2, mentre il backend abilita soltanto le feature 1.2 supportate
che NRI usa realmente (timeline semaphore, FP16 e descriptor image
update-after-bind). La quality viene propagata fino a `UpscalerDesc::mode`;
NIS Quality non viene quindi piu creato erroneamente come Native con un input
ridotto.

FSR e operativo come provider temporale FidelityFX via NRI. Consuma colore
scena, mip zero R32F della depth lineare Hi-Z, motion RGBA16F e reactive mask
R16F, usa una sequenza jitter con phase count dipendente dalla quality e resetta
la history su camera cut o invalidazione della view. L'output RGBA16F resta
separato dalla scena e precede HUD e ImGui. I wrapper NRI delle immagini native
vengono dimenticati esplicitamente prima di resize o distruzione Vulkan, cosi
NRI/FidelityFX non possono trattenere handle obsoleti. Il backend abilita
`shaderInt16` solo quando supportato e usa questa condizione nella capability
FSR. Input, depth, motion, reactive e output ricevono descriptor NRI espliciti;
il precedente dispatch con output descriptor nullo non e piu accettato.

DLSS Super Resolution e operativo tramite il provider NGX Vulkan di NRI. Il
backend interroga NGX prima della creazione di instance/device, abilita solo le
estensioni Vulkan richieste e distribuisce `nvngx_dlss.dll` accanto al gioco.
DLSR consuma depth hardware PICA, motion e reactive mask; i target W-buffer
vengono rifiutati perche non rispettano quel contratto. La capability resta
quindi visibile soltanto su GPU NVIDIA e con requisiti NGX soddisfatti. La patch
locale a NRI separa correttamente DLSR, che non richiede estensioni ray tracing,
da DLRR, che continua a richiederle.

XeSS resta una capability distinta e disabilitata. NRI v180 vincola il proprio
adapter XeSS a D3D12 e non offre quindi quel provider al backend Vulkan;
abilitarlo richiedera un adapter Vulkan esterno o una futura revisione NRI.

Le reactive/transparency mask derivate dalla classificazione PICA e dal moto
dell'erba servono sia agli upscaler sia a stabilizzare SSR e outline.

## Risoluzione e modalita finestra

Separare sempre:

- output resolution: finestra e swapchain;
- internal resolution: color/depth/guide/effects di gioco;
- UI resolution: uguale all'output;
- effect resolution: full, half o adaptive per singolo pass.

Sostituire il booleano fullscreen con:

```cpp
enum class WindowMode { Windowed, Borderless, ExclusiveFullscreen };
```

- `Windowed` ripristina posizione e dimensione precedenti.
- `Borderless` usa il desktop mode del monitor selezionato.
- `ExclusiveFullscreen` applica risoluzione e refresh enumerati da SDL.

La modifica di monitor, extent o mode si applica a fine frame con stato
`current`, `pending`, `lastKnownGood` e conferma temporizzata. Gestire finestra
minimizzata, DPI, Alt+Tab e cambio monitor.

Con `NRISwapChain` attiva, il present worker proprietario e escluso nello
stesso gate: non esistono due owner di acquire/present. Il worker e la gestione
diretta Vulkan restano disponibili soltanto nel fallback atomico.

Checkpoint implementato: `RenderResolutionPolicy` centralizza la scala
effettiva, l'override temporaneo degli upscaler e la trasformazione dal layout
PICA ruotato all'extent interno, senza modificare il valore scelto dall'utente.
La diagnostica espone separatamente `output_width/height`,
`internal_resolution_scale` e `internal_target_width/height`; l'override
`OOT3D_GRAPHICS_RENDER_SCALE` resta non persistente e riservato ai gate.

`tools/Test-Oot3dRenderResolution.ps1` esegue una transizione reale
`1.0 -> 1.5` con CACAO e TAA. Su 32 frame mantiene output/UI a `1280x720`,
porta il target osservato da `240x320` a `360x480`, conserva parita draw e
validation pulita, invalida la history TAA al primo frame ricreato e ne
verifica il recupero nei frame successivi.

## Frame rate e frame pacing

Separare `simulationRate`, `renderRate` e `presentRate`:

- `Original30`: la simulazione conserva timing e determinismo originali; si
  presenta un nuovo stato di gioco a 30 Hz.
- `Fixed60`: presentazione a 60 Hz usando il percorso di visual sampling gia
  presente quando la simulazione non produce un nuovo stato a ogni refresh.
- `Uncapped`: nessun cap di presentazione; usare interpolazione tra gli ultimi
  due stati validi, senza avanzare piu velocemente clock guest, input o audio.

Il runtime contiene gia infrastruttura di visual frame accumulation e
interpolation: riutilizzarla e validarla invece di introdurre un secondo
interpolatore. Se una transizione non e interpolabile, presentare lo stato
valido piu recente e segnalarlo nelle metriche.

Non rappresentare `Uncapped` passando zero al pacer esistente. Usare un enum o
`std::optional<uint32_t> frameCap`. VSync resta indipendente; tearing si abilita
solo per `Uncapped + VSync off` e solo quando supportato.

AO/SSR temporali e TAA devono indicizzare la history per rendered scene frame,
non per ogni present duplicato/interpolato.

Checkpoint implementato: `PresentationPacingPolicy` traduce esplicitamente
`Original30`, `Fixed60` e `Uncapped` in stato `enabled` e rate opzionale,
separandolo dalla policy di simulazione guest. L'override di collaudo
`OOT3D_GRAPHICS_FRAME_RATE` e non persistente. Il percorso ImGui gestisce ora
anche i presentation-only frame che precedono il primo display transfer guest:
se l'overlay non ha aperto un render pass, viene stabilito il pass swapchain
ordinario prima dei draw UI.

`tools/Test-Oot3dFramePacing.ps1` esegue tutti e tre i modi sulla stessa scena,
verifica target 30/60/unlimited, otto presentation frame, simulazione guest
invariata a 30 Hz con update rate nativo 2, parita draw PICA/NRI e validation
Vulkan/NRI pulita. La callback Vulkan pubblica inoltre il testo degli errori
sullo stderr quando la validation opt-in e attiva, evitando contatori privi
di causa durante i gate.

## FOV globale

Il moltiplicatore resta nell'intervallo `[1.00, 1.50]`, default `1.00`, step
`0.05`.

Estendere la policy gia centrale su `Mtx4x4BuildFrustum`:

- applicarla a gameplay, cutscene, sottocamere e debug camera;
- non toccare `Mtx4x4BuildOrthographicProjectionRotated`;
- combinare una sola volta widescreen e FOV;
- moltiplicare l'angolo, non i coefficienti della matrice;
- limitare l'angolo finale a un valore sicuro inferiore a 180 gradi, proposto
  150 gradi;
- pubblicare la stessa matrice finale a `SceneViewBridge` per SSAO, SSR, TAA e
  motion vector.

Formula di riferimento:

```text
effectiveVerticalFov = min(baseVerticalFov * multiplier, 150 degrees)
halfHeight = nearPlane * tan(effectiveVerticalFov / 2)
halfWidth = halfHeight * outputAspect
```

Non modificare permanentemente il valore FOV della camera: transizioni e
logica guest devono continuare a usare il valore originale.

Checkpoint implementato: `PerspectiveFovPolicy` isola il calcolo puro dalla
window orchestration, limita il moltiplicatore a `[1.00, 1.50]`, conserva il
centro ottico e combina l'espansione widescreen una sola volta. L'hook globale
dei frustum prospettici usa il risultato sia per la matrice del gioco sia per
`SceneViewRuntime`; le proiezioni ortografiche restano escluse. In questo modo
gameplay, cutscene e sottocamere che attraversano il costruttore comune
ricevono la stessa policy senza alterare lo stato camera originale.

`tools/Test-Oot3dFov.ps1` protegge il contratto numerico e confronta sulla
scena giocabile FOV `1.00` e `1.50`, con validation Vulkan/NRI, parita dei draw
e differenza visiva minima. Il checkpoint corrente copre 998 draw per caso e
modifica il 74,283% dei pixel campionati senza errori di validation.

## Modello dei settaggi

```cpp
enum class GraphicsPreset { Authentic, Enhanced, Toon, Custom };
enum class FrameRateMode { Original30, Fixed60, Uncapped };
enum class AmbientOcclusionMode { Off, Cacao };
enum class ReflectionMode { Off, HiZ, FidelityFxSssr };
enum class ToonMode { Off, PostProcessPreview, PicaMaterial };
enum class GrassQuality { Off, Low, Medium, High, Custom };
enum class GrassSampleChannel { Red, Green, Blue, Alpha, Luminance };
enum class GrassUvSource { AuthoredStatic, EffectiveAtLoad };
enum class GrassWrapOverride { Material, Clamp, Repeat, Mirror };

struct GrassPlacementRule {
    uint64_t RuleId;
    bool Enabled;
    GrassTextureSelector Target;
    GrassSampleChannel Channel;
    GrassUvSource UvSource;
    GrassWrapOverride Wrap;
    bool Invert;
    uint8_t MipLevel;
    float ThresholdLow;
    float ThresholdHigh;
    float Gamma;
    float DensityPerArea;
    float MinimumSpacing;
    float MaximumSlopeDegrees;
    float BladeHeightMin;
    float BladeHeightMax;
    float BladeWidthMin;
    float BladeWidthMax;
    uint32_t Seed;
};

struct InteractiveGrassSettings {
    GrassQuality Quality;
    uint32_t MaxInstancesPerRoom;
    float DrawDistance;
    float Lod0Distance;
    float Lod1Distance;
    float WindDirectionDegrees;
    float WindStrength;
    float WindSpeed;
    float WindSpatialScale;
    float GustStrength;
    float Turbulence;
    float ColliderRadiusMultiplier;
    float ColliderHeightMultiplier;
    float CollisionPush;
    float VelocityInfluence;
    float RecoverySeconds;
    float MaximumBend;
    uint16_t InteractionFieldResolution;
    float InteractionFieldWorldRadius;
    std::vector<GrassPlacementRule> Rules;
};

struct EffectsSettings {
    AmbientOcclusionMode AmbientOcclusion;
    uint8_t AoQuality;
    float AoRadius;
    float AoStrength;
    ReflectionMode Reflections;
    uint8_t ReflectionQuality;
    float ReflectionMaxDistance;
    float ReflectionThickness;
    ToonMode Toon;
    uint8_t ToonBands;
    float ToonBandSoftness;
    float RimStrength;
    float OutlineWidth;
};

struct GraphicsSettings {
    GraphicsPreset Preset;
    WindowMode Window;
    uint32_t DisplayIndex;
    uint32_t OutputWidth;
    uint32_t OutputHeight;
    uint32_t RefreshRate;
    float InternalResolutionScale;
    AntiAliasingMode AntiAliasing;
    uint8_t MsaaSamples;
    FrameRateMode FrameRate;
    bool VSync;
    float FovMultiplier;
    InteractiveGrassSettings Grass;
    EffectsSettings Effects;
};
```

Preset iniziali:

- `Authentic`: erba/effetti off, render scale 1.0, presentazione originale.
- `Enhanced`: erba Medium sulle sole regole create dall'utente, CACAO Medium,
  SSR Hi-Z conservativo solo sui materiali validati, AA spaziale e nessuna
  alterazione artistica toon.
- `Toon`: erba Medium, `PicaMaterialToon`, outline, CACAO moderato e SSR
  ridotto/off per evitare un risultato incoerentemente realistico.
- `Custom`: tutti i controlli compatibili.

Un preset non sceglie mai automaticamente una texture di distribuzione: senza
almeno una regola valida l'erba resta inattiva e la UI spiega il motivo.

Un preset imposta valori, ma la prima modifica manuale passa a `Custom` senza
cancellare l'ultimo preset selezionato.

Persistenza:

```text
Graphics.SchemaVersion
Graphics.Preset
Graphics.Window.Mode
Graphics.Window.Display
Graphics.Output.Width
Graphics.Output.Height
Graphics.Output.RefreshRate
Graphics.RenderScale
Graphics.AA.Mode
Graphics.AA.MsaaSamples
Graphics.AA.TemporalQuality
Graphics.FrameRate.Mode
Graphics.Presentation.VSync
Graphics.Camera.FovMultiplier
Graphics.Grass.Quality
Graphics.Grass.Budget.*
Graphics.Grass.Wind.*
Graphics.Grass.LinkInteraction.*
Graphics.Grass.Rules[]
Graphics.Effects.AO.*
Graphics.Effects.Reflections.*
Graphics.Effects.Toon.*
```

Lo schema `Graphics` versione 1 e ora operativo e serializza l'intero modello,
incluse tutte le regole grass e reflection. Enum e hash sono salvati con nomi
leggibili e hash esadecimali a 16 cifre; valori mancanti o malformati vengono
riparati tramite la stessa validazione del runtime. Una versione futura non
riconosciuta viene preservata senza riscrittura distruttiva.

Le chiavi esistenti `Window.*`, `gMSAAValue`, `gInternalResolution`,
`gAdvancedResolution.*`, `gVsyncEnabled`, `gSdlWindowedFullscreen` e le
varianti `CVars.gSettings.*` vengono migrate una volta. Il file JSON viene
scritto su un temporaneo e sostituito atomicamente. Gli override di
automazione via environment restano effimeri; una transazione
window/fullscreen non viene resa persistente finche il backend non la conferma,
e un rollback salva soltanto l'ultimo stato noto valido.

## Interfaccia utente

Creare una `GraphicsSettingsWindow` ImGui con:

- `Preset`: Authentic, Enhanced, Toon, Custom;
- `Display`: monitor, mode, risoluzione, refresh e VSync;
- `Prestazioni`: 30/60/libero, render scale, effect resolution e frame latency;
- `Antialiasing`: Off, FXAA, SMAA, MSAA, TAA e provider temporali;
- `Assegnazioni texture`: un solo catalogo ordinabile e una sola selezione
  stabile con viewer RGBA, hash/dimensioni/formato/osservazioni e badge delle
  funzioni assegnate. Il catalogo resta fuori dai capability gate dei singoli
  effetti: grass, riflessi e funzioni future aggiungono azioni e controlli
  modulari senza duplicare selezione, ordinamento o preview;
- `Vegetazione`: Off/Low/Medium/High/Custom, assegnazione dal catalogo
  condiviso, regole di campionamento, blade, budget e LOD;
- `Vento`: direzione, forza, velocita, scala, raffiche e turbolenza;
- `Interazione Link`: moltiplicatori collider, push, velocity, recovery,
  max bend, field radius e risoluzione;
- `Effetti`: AO, riflessi e relativi preset qualita;
- `Materiali riflettenti`: assegnazione esplicita acqua/metallo/polished dal
  catalogo condiviso, reflectivity, roughness e mapper slot;
- `Stile`: toon bands, rim, outline e reset;
- `Camera`: FOV 1.00-1.50;
- `Diagnostica`, solo nelle build di sviluppo: superfici grass eligible,
  texture mask, anchor/LOD, collider e interaction field, depth, normals, draw
  class, material guide, AO, SSR hit/confidence, motion e GPU timings.

Il browser texture deve consentire `Aggiungi regola`, preview della mask
risultante e conteggio stimato degli anchor prima del rebuild. I controlli che
richiedono rigenerazione mostrano stato `Rebuilding`; quelli uniform vengono
applicati al frame successivo.

La UI interroga `GraphicsCapabilities`, non tipi Vulkan/NRI. Le opzioni non
disponibili mostrano una causa concreta.

Capability minime:

```text
NriInterop
SampledSceneColor
SampledDepth
ValidViewMetadata
WorldOverlayBoundary
WorldGeometryInsertionPoint
DecodedStaticMesh
GrassSceneInstances
StableTextureIdentity
TexturePixelsOnDemand
LinkGrassInteractor
GrassComputeInteraction
NormalGuide
MaterialGuide
MotionVectors
LinearHdrWorkingColor
TemporalHistory
PresentTearing
ExclusiveFullscreen
```

Esempi:

- CACAO preview: `SampledDepth + ValidViewMetadata`;
- grass base: `WorldGeometryInsertionPoint + DecodedStaticMesh +
  GrassSceneInstances + StableTextureIdentity + TexturePixelsOnDemand +
  LinkGrassInteractor`;
- persistenza collisione grass: aggiunge `GrassComputeInteraction`; in sua
  assenza resta il fallback swept diretto;
- PICA-aware CACAO: aggiunge `NormalGuide + MaterialGuide`;
- SSR Hi-Z: aggiunge `WorldOverlayBoundary + MaterialGuide`;
- FidelityFX SSSR: aggiunge `MotionVectors + LinearHdrWorkingColor +
  TemporalHistory` e fallback IBL;
- PicaMaterialToon: richiede fragment lighting PICA collegato;
- TAA/upscaler: richiede motion, previous matrices, camera cut e history.

Le incompatibilita vanno risolte nel modello. La UI non deve soltanto
disabilitare widget lasciando uno stato impossibile nel file di configurazione.

## Piano di consegna ristrutturato

### Gate 0 - Baseline e osservabilita

- Screenshot/reference hash con preset Authentic.
- Il singolo screenshot automatico viene acquisito su un frame giocabile
  stabilizzato, non sul clear di bootstrap. Il gate condiviso valida BMP RGB24,
  payload completo, almeno 32 colori campionati, range canali di almeno 24 e
  almeno l'1% di pixel cromatici prima di accettarne l'hash.
- GPU marker e timestamp per PICA, display transfer, overlay e present.
- Debug dump di depth parameters, projection/view id e draw classification.
- Scene di riferimento: interno con contatti AO, acqua, foliage alpha-tested,
  personaggio, cutscene, fog, shadow2D, HUD e frontend bottom screen.
- Almeno una texture terreno candidata all'erba, con hash e mesh/UV verificate,
  piu una scena per corsa/rotolata di Link.
- Test resize, minimizzazione, Alt+Tab e savestate.

### Release A - Fondazione avanzata e SSAO

Percorso critico:

1. Vulkan 1.2 + synchronization2 e NRI v180 wrapper.
2. `SceneSurfaceRegistry`, wrapper color/depth/command buffer e state tracker.
3. `SceneViewBridge` collegato allo stesso hook widescreen/FOV.
4. Depth sampleable, linear-depth debug e normali ricostruite.
5. Effect graph con passthrough e CACAO Low/Medium.
6. Coverage mask per proteggere HUD/overlay.
7. UI con preset, AO, FXAA, render scale, window mode, frame-rate mode, FOV e
   texture catalog read-only per preparare le regole grass.
8. `PerspectiveFovPolicy` globale qualificata su scena reale e proiezioni
   ortografiche esplicitamente protette.

Questa release produce il primo risultato avanzato senza migrare pipeline PICA
o swapchain. Le limitazioni approssimative sono visibili nell'UI e nei log.

### Release B - Interactive Grass

Questa release ha precedenza esplicita su toon e reflections:

1. `GrassSceneBridge`, `GrassSurfaceExtractor`,
   `GrassTextureSourceCache` e selettore stabile per nome/hash/mapper slot,
   compatibili con lo strip degli RGBA.
2. Hook `SubmitInteractiveWorldGeometry` esplicito tra opaque/alpha-tested e
   translucent/HUD; niente injection tardiva al display transfer.
3. Sampler CPU area-based con channel, invert, threshold, gamma, mip, wrap,
   density, spacing, slope e seed.
4. Cache asincrona degli anchor e rebuild atomico delle sole regole cambiate.
5. `InteractiveGrassPass` NRI con quad incrociati istanziati, depth/guide,
   culling, LOD e budget.
6. Vento world-space root-to-tip guidato dal `VisualClock`.
7. `GrassInteractionBridge` con posizione/collider autorevoli di Link e
   volume swept.
8. `GrassInteractionField` compute con impulso, molla, damping, recovery e
   reset su teleport/savestate/room.
9. Tab Vegetazione/Vento/Interazione e debug view complete.

La release e accettata quando una texture scelta dalla UI distribuisce erba
soltanto sulle mesh corrispondenti e Link la piega senza tunneling a 30, 60 e
frame rate libero.

### Release C - PICA Enhanced e Toon

- Il fragment lighting PICA e collegato attraverso le overlay patch tracciate
  del runtime. Il checkpoint Kokiri corrente e vertex-lit e non abilita i
  relativi registri hardware: `pica_material_toon_draw_count=0` e quindi un
  risultato legittimo, non una regressione del generatore.
- LUT, colori primario/secondario, normal quaternion e view vector sono
  collegati al frontend tipizzato. Il bump PICA supporta NormalMap e TangentMap,
  selettore texture, route texture-2/coordinate-1 e ricostruzione Z nativa; la
  normal guide viene aggiunta soltanto alle varianti che la richiedono, mentre
  il percorso autentico conserva un solo color attachment. Restano da ampliare
  le scene reali che attivano tali registri e le shadow routes native. I gate
  correnti passano 29/29 test pipeline e 229/229 test grafici; il corpus AOT
  predefinito contiene 292 moduli e chiude Kokiri in modalita strict con zero
  miss sia in Authentic sia con CACAO.
- Estendere il confine world gia richiesto dall'erba con draw class/material
  metadata e guide attachment completi.
- Peso ambientale PICA pre-TEV e compositing CACAO condiviso operativi per i
  draw fragment-lit; il fallback vertex-lit resta intenzionalmente full-color.
  La guida RGB dedicata completa la separazione per-canale nei TEV misti e
  mantiene un fallback esplicito per gli shader vertex-lit/originali.
- `PostToonPreview`, `PicaMaterialToon` e normal/depth outline sono operativi.
  La preview quantizza il rapporto di illuminazione PICA senza posterizzare
  i texel; il percorso materiale si inserisce prima del TEV quando il marker
  fragment-lighting e disponibile e usa il fallback albedo-preserving sui
  draw vertex-lit. `tools/Test-Oot3dToon.ps1` prova contratti shader e ordine
  TEV/fog/alpha, quindi confronta baseline, preset approvato, controlli stress
  e outline sulla stessa scena con validation e parita draw.
- Grass riceve fog PICA e illuminazione CMB native, pubblica normal/ambient
  guide ed e quindi compatibile con i consumer ambient/toon della scena. Il
  raggruppamento foliage toon esplicito resta opt-in.
- SMAA 1x High ufficiale e MSAA reale 2x/4x/8x con resolve corretto delle
  guide sono operativi e capability-gated. Il gate dedicato SMAA verifica i
  tre dispatch, ownership NRI, validation, parita draw e differenza
  framebuffer sulla scena giocabile.
- `AntiAliasingFramePolicy` centralizza esclusivita e parametri per FXAA,
  SMAA, MSAA, TAA e upscaler. `Test-Oot3dAntiAliasingMatrix.ps1` qualifica
  sulla stessa scena FXAA, SMAA, MSAA 2x/4x/8x NRI, MSAA 4x fallback Vulkan
  e TAA per otto frame, richiedendo
  discriminator esatti, framebuffer non collassato e validation pulita.
  MSAA 2x/4x/8x certificano scope, pipeline e draw NRI multisample; il caso 4x gemello
  certifica il fallback Vulkan isolato; NIS resta nel gate upscaler dedicato
  e non viene modellato erroneamente come antialiasing.
- Il contratto draw PICA espone ora alpha-test, funzione di confronto e
  reference direttamente dal registro `0x104`, senza inferenze sul testo
  shader. Le pipeline Vulkan e NRI impostano alpha-to-coverage solo per
  geometria alpha-tested opaque-replace con depth write e sample count
  multisample. La matrice AA verifica inoltre che il checkpoint giocabile
  produca 126 draw alpha-tested/alpha-to-coverage in MSAA 2x/4x/8x e zero
  alpha-to-coverage in FXAA, SMAA e TAA, con zero errori Vulkan/NRI.
- La preparazione degli stream NRI e separata dalla decisione di submission
  tramite `PicaNriDrawOwnership`: pipeline e vertex data vengono preparati
  prima dello scope, ma l'ownership viene finalizzata soltanto dopo
  `BeginNativePicaRenderPass` sul target corrente. Questo elimina il fallback
  Vulkan del primo draw di ogni scope. Authentic e Advanced verificano ora
  parita esatta per-frame (`nri_pica_owned_draw_count ==
  native_pica_draw_count`) gia dal primo frame utile.

### Release D - Reflections

- Piramide Hi-Z condivisa e test per depth normale/reversed/W-buffer operativi.
- `HiZReflectionPass`, confidence e denoise spaziale operativi.
- Provider FidelityFX SSSR, IBL PICA e BRDF/resolve materiale operativi.
- Profili texture acqua/metallo/polished/custom e controlli per
  reflectivity/roughness sono implementati; calibrazione artistica e
  perfezionamento della UI materiali sono rinviati per decisione esplicita
  dell'utente e non bloccano le release successive.
- Il selettore/viewer texture unico conserva identita completa
  hash/dimensioni/formato, ordinamento, preview e navigazione
  precedente/successiva in uno stato condiviso. Grass e reflection mantengono
  assegnazioni indipendenti e i badge restano estendibili. Il click diretto
  sulle righe del combo reflection non e qualificato nel runtime corrente:
  la correzione viene deliberatamente rinviata insieme al filone materiali,
  senza dichiararla requisito aperto per le release successive.
- L'export BMP opt-in `OOT3D_TEXTURE_PREVIEW_DIRECTORY` rende ripetibile
  l'identificazione offline delle texture osservate. La debug view
  `Material reflectivity/class` separa la class/coverage in B dalla reactive
  mask temporale in A, evitando il precedente falso positivo a schermo pieno.
- Inventario diagnostico per-texture bounded e gate parametrico per
  checkpoint/hash/profilo operativi; Kokiri conta 34 identita distinte e
  attribuisce 14 draw all'hash polished di riferimento senza overflow.
- Confronto A/B Hi-Z/SSSR sul resolve diretto operativo: conserva draw e
  copertura materiale, misura tempi GPU e richiede una differenza framebuffer
  non collassata entro un envelope dichiarato. TAA resta un gate strutturale
  separato perche l'accumulo puo convergere i due risultati.
- Modalita temporal-stability operativa su quattro BMP consecutivi per
  provider: SSSR e Hi-Z conservano delta inter-frame equivalente sul checkpoint
  Kokiri e ogni cattura supera il gate anti-collasso.
- `tools/Test-Oot3dReflectionSceneMatrix.ps1` riusa lo stesso confronto senza
  duplicarne i gate e qualifica tre checkpoint giocabili: savestate corrente,
  foresta Kokiri e interno della casa di Link. Ogni caso usa un hash realmente
  presente nella propria inventory e richiede parita draw/materiale, immagini
  distinte, differenza provider entro envelope, limiti GPU e zero errori.
  Sul checkpoint RTX 3060, sei frame per provider e scena totalizzano 2.984
  draw e 240 draw profilati per provider; le inventory contano 34/32/15
  identita, la frazione di pixel diversa resta tra 0,020677 e 0,020694 e il
  delta canale medio minimo e 1,181. Hi-Z resta circa 0,101 ms medi e SSSR
  entro 1,682 ms medi. Il profilo `polished` rende il percorso misurabile:
  non sostituisce la successiva calibrazione artistica per materiale.
- Sky, foliage non assegnato, particle, HUD e unknown restano non riflettenti.
- Controlli SSR, material profile e debug view sono esposti nell'UI.

### Release E - Fondazione temporale

- Current/previous view-projection per ogni view id.
- Motion vector per geometria rigida e skinned; fallback camera-only dichiarato.
- Camera cut, disocclusion, reactive e transparency mask, includendo il moto
  delle blade.
- TAA interno e reset history su resize, FOV, savestate, teletrasporto e cambio
  scena.
- NRIUpscaler capability-gated: NIS spaziale, FSR temporale e DLSS Vulkan
  operativi; XeSS bloccato dall'adapter D3D12-only di NRI v180.
- Provider FidelityFX SSSR operativo con input temporali, output NRI e fallback
  Hi-Z per-view. Working color lineare RGBA16F, propagazione dell'encoding e
  scanout coerente sono operativi. Environment PICA prefiltrato GGX, LUT
  split-sum BRDF e resolve materiale RGB+weight sono operativi e modulari;
  i profili texture rendono operative reflectivity/roughness anche sui
  materiali vertex-lit. Restano calibrazione multi-scena e valutazione
  comparativa di qualita, costo e stabilita prima della qualifica `Ultra`.

### Release F - Ownership NRI completa

**Ownership dei draw PICA: chiusa.** La submission prepara pipeline e stream
prima dell'apertura dello scope, ma finalizza l'ownership contro lo scope
dynamic-rendering corrente tramite `PicaNriDrawOwnership`. Ogni frame utile,
compreso il primo, richiede quindi
`nri_pica_owned_draw_count == native_pica_draw_count`; qualsiasi bind NRI
fallito o pipeline non disponibile dentro lo scope NRI termina la submission,
senza emettere un draw Vulkan diretto. Il fallback Vulkan e ammesso soltanto
quando lo scope NRI non e richiesto. La
copertura runtime include Authentic, CACAO+Hi-Z+TAA, restore con cambio scena,
MSAA 2x/4x/8x e fallback Vulkan forzato. Validation Vulkan/NRI deve restare a
zero errori in tutti i casi.

- Output degli upscaler, memoria e descriptor SRV/UAV migrati a texture
  committed NRI; quality transition verificata con distruzione/ricreazione.
  Le transizioni output sono NRI per NIS, FSR e DLSS e vengono certificate da
  `upscaler_barriers_nri_owned=true`.
  `tools/Test-Oot3dUpscalerQualityTransitions.ps1` esegue la transizione
  Quality 1,5x -> Performance 2x sui tre provider: l'input passa da
  960x853 a 720x640 mantenendo l'output 1440x1280, ownership e scena; FSR e
  DLSS devono inoltre resettare la history nello stesso frame.
- Motion vector e reactive mask migrati a texture committed NRI, verificati
  attraverso TAA, FSR e DLSS e ricreati durante la quality transition.
- Pass motion migrato integralmente a uniform buffer, sampler, descriptor,
  pipeline layout/pipeline, dispatch e barrier NRI.
- History ping-pong e compute TAA migrati a texture committed, descriptor,
  root constants, pipeline e dispatch NRI; resize/render-scale verificato con
  ricreazione delle history e reset temporale. Le barrier ping-pong sono
  emesse in batch dal tracker.
- Working target e compute `SceneCompositePass` migrati a NRI; verificati con
  CACAO+Hi-Z+TAA, CACAO+Hi-Z+FSR e cambio render scale. Le transizioni
  `Undefined/ShaderRead -> ComputeWrite -> ShaderRead` usano ora il
  `ResourceStateTracker` e una primitive `nri::TextureBarrierDesc` condivisa;
  `scene_composite_barriers_nri_owned=true` ne certifica l'uso per frame.
- `LinearSceneColorPass` possiede target RGBA16F, compute, descriptor e barrier
  NRI. Converte lo snapshot PICA senza mutare il target autentico e pubblica
  `SceneSurfaceKind::LinearWorkingColor`; la diagnostica certifica ownership,
  policy sRGB e consumo lineare da parte di FidelityFX SSSR.
- Pyramid Hi-Z mipmapped, view per-mip e compute reduction migrati a NRI;
  verificati come input condiviso di SSR e FSR durante quality transition;
  stato e barrier sono indipendenti per ciascun mip.
- Ray march Hi-Z e filtro bilaterale SSR, inclusi entrambi i target ping-pong,
  migrati a NRI e verificati con TAA, FSR e resize, incluse le transizioni tra
  ray march e filtro.
- Target R32F CACAO migrato a texture committed NRI, mantenendo il context
  FidelityFX Vulkan come adapter di interoperabilita. Verificati attivazione,
  disattivazione, resize e compositing CACAO+TAA/CACAO+FSR; la diagnostica
  richiede `cacao_output_nri_owned=true` per ogni frame attivo.
- `ResourceStateTracker` separa pianificazione e commit: una transizione viene
  registrata solo dopo che la primitive NRI ha validato command buffer,
  texture wrapped e subresource. Motion/reactive, composite, TAA, Hi-Z, SSR e
  output NIS/FSR/DLSS condividono questa primitive. CACAO mantiene
  esplicitamente le sole barrier Vulkan residue nei moduli avanzati, perche il
  dispatch viene registrato dall'adapter FidelityFX Vulkan esterno.
- Le barrier delle guide PICA sono estratte in
  `PicaGuideSamplingBarriers`: il batch tipizzato copre depth e i quattro
  color guide in entrambe le direzioni. Il dependency scope di ritorno include
  compute e fragment, correggendo il precedente stage mask incompleto quando
  il composite temporale leggeva le guide. Il test puro verifica immagini,
  aspect, layout, access mask e simmetria; il gate giocabile
  Authentic/Advanced mantiene parita di draw e zero errori Vulkan/NRI.
- Policy dei push constants scanout estratta dal backend in
  `PicaScanoutPolicy`, con test su flip transfer, dimensioni, view metadata e
  soppressione di AO/SSR soltanto quando il composite e gia nel risultato
  TAA/FSR/DLSS.
- Pipeline grafica base/overlay, shader separati texture/sampler, descriptor,
  root constants e draw dello scanout migrati a `NriPicaScanoutPass`. La
  diagnostica richiede `nri_scanout_count`, `nri_scanout_pipeline_owned=true`
  `nri_scanout_descriptors_owned=true` e `nri_scanout_scope_owned=true`; sono
  verificati profilo Authentic, CACAO+Hi-Z+TAA e ricreazione swapchain
  borderless. Dynamic rendering viene abilitato soltanto quando supportato e
  il fallback Vulkan resta integro.
- Scope raster PICA estratto in `PicaDynamicRenderingScope`: cinque MRT,
  depth/stencil e resolve colore/depth MSAA sono verificati con test del
  contratto, Authentic, fallback legacy, MSAA 4x e CACAO+Hi-Z+TAA.
  `native_pica_dynamic_rendering_count` certifica gli scope realmente
  registrati e `nri_pica_rendering_scope_owned=true` il relativo begin/end
  NRI, incluso MSAA con resolve colore/depth. La pipeline grass usa lo stesso
  formato dinamico. Una patch configure stretta corregge il falso gate
  maintenance10 della validation NRI v180 soltanto per i depth attachment
  resolve Vulkan 1.2. Le dipendenze globali
  pre/post sono `nri::GlobalBarrierDesc` e
  `nri_pica_global_barrier_count == 2 *
  native_pica_dynamic_rendering_count` ne certifica la copertura completa.
- Cache pipeline PICA esistente wrapped da `NriPicaPipelineBridge`; tutti i
  bind della scena giocabile passano da NRI anche in MSAA 2x/4x/8x e sono
  confrontati per frame con `native_pica_draw_count`; lifetime e fallback
  restano verificati.
- Contratto shader/descriptor PICA NRI estratto in un modulo puro: i combined
  sampler diventano tre texture piu tre sampler senza cambiare la semantica
  di campionamento. Il pipeline layout e posseduto da NRI e le varianti
  SPIR-V dei draw rappresentabili della scena vengono compilate e conteggiate.
- Pipeline grafica PICA creata anche direttamente tramite NRI per ogni entry
  Vulkan, inclusi stati MRT/depth/stencil/blend/logic e MSAA. Un contratto
  vertex float4 canonico con repacker CPU elimina i formati scaled non
  rappresentabili da NRI senza alterare i valori PICA. Authentic e MSAA 4x
  richiedono copertura delle pipeline NRI senza ricadere a Off.
  Gli stream canonici vengono ora caricati accanto agli originali; pool/set
  descriptor, sampler, constant-buffer view, root constants, vertex/index
  binding e draw passano da NRI nei relativi scope. Authentic,
  CACAO+Hi-Z+TAA e MSAA 2x/4x/8x richiedono ownership esatta di tutti i draw e
  `nri_pica_descriptors_owned=true`. Il backend Vulkan usa per default gli
  arena host gia mappati tramite interop NRI, evitando una seconda copia
  identica di vertex, index e uniform per ogni draw.
  `OOT3D_GRAPHICS_NRI_PICA_UPLOADS=1` abilita esplicitamente gli arena
  committed `HOST_UPLOAD` NRI con copia sparsa transazionale per i test del
  percorso isolato; solo in quel caso Authentic, MSAA 4x e CACAO+Hi-Z+TAA
  richiedono
  `nri_pica_owned_upload_draw_count == nri_pica_owned_draw_count` e
  `nri_pica_upload_bytes > 0`.
- Staging e copie delle texture PICA migrati a due arena committed
  `HOST_UPLOAD` NRI per-frame, con pitch derivati dagli alignment del device,
  pack RGBA8 testato, barrier NRI e `CmdUploadBufferToTexture`. Authentic,
  MSAA 4x e CACAO+Hi-Z+TAA richiedono almeno un upload iniziale con
  `nri_pica_texture_upload_owned=true` e byte non zero; il fallback isolato
  deve lasciare i draw NRI invariati.
- Immagini texture PICA, memoria device-local e sampled view migrati a
  `NriPicaTextureImageOwner`, mantenendo gli handle Vulkan native estraibili.
  Lifetime e ordine di shutdown impediscono doppie distruzioni; Authentic,
  MSAA 4x e CACAO+Hi-Z+TAA richiedono 55 immagini NRI nel checkpoint corrente.
  Il fallback delle sole immagini lascia upload e draw NRI invariati.
- Immagini e sampled view delle snapshot display-transfer migrate allo stesso
  owner NRI con policy indipendente; Authentic e CACAO+Hi-Z+TAA richiedono
  creazioni NRI non nulle, transfer e scanout invariati nel fallback.
- Copia render-target-to-display e quattro transizioni per snapshot migrate a
  `NriPicaDisplayCopyPass`. Authentic e MSAA 4x richiedono nel checkpoint
  corrente 30 copie/120 barrier in sette frame; CACAO+Hi-Z+TAA richiede 45
  copie/180 barrier in dieci frame. Anche il render pass legacy conserva 30
  copie NRI, mentre il fallback delle sole copie mantiene invariati transfer,
  draw, target e scanout.
- Render target PICA migrati a un bundle NRI transazionale: sette immagini
  single-sample e tredici con MSAA, inclusi MRT, Shadow2D e depth. Nel
  checkpoint corrente Authentic richiede quattro bundle/28 immagini e MSAA
  4x quattro bundle/52 immagini. CACAO+Hi-Z+TAA e il render pass legacy sono
  verificati; il fallback dei soli target lascia scope/draw/scanout invariati.
  Le view combinate del render pass legacy restano Vulkan.
- Inizializzazione deterministica di MRT, depth/stencil, resolve MSAA e
  Shadow2D migrata a `NriPicaRenderTargetInitPass`: quattro bundle Authentic
  richiedono 28 immagini inizializzate/32 barrier; MSAA 4x richiede 52
  immagini/56 barrier. Percorso NRI e fallback hanno telemetria di draw e
  scanout identica e producono un framebuffer giocabile non collassato sullo
  stesso frame stabilizzato.
  Anche CACAO+Hi-Z+TAA e il render pass legacy sono verificati.
- Clear dinamici dei memory-fill PICA migrati a
  `NriPicaMemoryFillClearPass`: lo smoke sintetico sulla scena reale richiede
  un clear Shadow2D, un clear degli attachment e due barrier NRI, anche con
  MSAA 4x. Il fallback completo conserva 1.200 draw e 12 scanout; con render
  pass legacy Shadow2D resta NRI e gli attachment tornano a Vulkan.
- Swapchain, immagini, color-attachment view, acquire e present sono migrati a
  `NriSwapchain`. I semafori binari sono posseduti dal modulo e wrapped in NRI
  per riusare senza alterazioni la submission Vulkan del runtime originale.
  Il present worker viene escluso quando NRI e attiva e ripristinato nel
  fallback Vulkan atomico.
- La diagnostica certifica per frame acquire/present, image count, ownership
  della sincronizzazione e bypass del worker. Il gate automatico
  `tools/Test-Oot3dNriSwapchain.ps1` verifica scena reale, parita A/B di draw,
  display copy e scanout, fallback forzato e ricreazione
  borderless. Authentic e CACAO+Hi-Z+TAA sono verificati.
- Window mode, output resolution e VSync usano ora
  `PresentationSettingsTransaction`, separato da UI, runtime e backend. Una
  modifica deve essere acknowledged dal backend prima di entrare nei 15
  secondi di conferma; Keep la promuove, Revert o il timeout ripristinano
  soltanto i campi di presentazione last-known-good.
- `Oot3d::TitleRenderBackend::ApplyPresentationSettings` ricrea la swapchain
  nello stesso apply. Se
  la nuova configurazione fallisce, ripristina finestra, VSync e swapchain
  precedenti prima di restituire l'errore; un fallimento del rollback resta
  fatale invece di proseguire con ownership parziale. I contatori
  `presentation_apply_count`, `presentation_rollback_count` e
  `presentation_apply_failure_count` distinguono le tre operazioni.
- `tools/Test-Oot3dPresentationTransaction.ps1` verifica su scena reale
  baseline, rollback esplicito e fallimento iniettato dopo la ricreazione:
  2.210 draw, 55 display copy e 22 scanout devono restare identici.
- La selezione dell'adapter e la topologia delle queue sono estratte in
  `VulkanAdapterPolicy`, senza dipendenze da Vulkan o dal backend. La policy
  filtra adapter privi di graphics, present, swapchain, surface format o
  present mode; la selezione automatica mantiene la preferenza deterministica
  per GPU discrete. `OOT3D_GRAPHICS_VULKAN_ADAPTER=<indice>` consente una
  selezione esplicita e stretta: indice malformato, non enumerato o non
  presentation-capable termina l'inizializzazione invece di cambiare GPU in
  silenzio.
- Con graphics e present nella stessa family vengono richieste due queue
  quando disponibili, preservando il present worker nel fallback Vulkan; con
  una sola queue la presentazione resta sincrona. La ricerca esamina tutte le
  family e preferisce una family condivisa completa anche se una coppia
  separata appare prima nell'enumerazione. Family separate continuano a usare
  correttamente lo swapchain Vulkan concorrente, ma escludono lo swapchain NRI
  finche il wrapper non potra ricevere una queue di present distinta.
- La diagnostica registra count/index/vendor/device dell'adapter, family e
  numero di queue disponibili/richieste, indice della queue di present,
  present asincrono effettivo, eligibility NRI e una causa di fallback stabile:
  `0=None`, `1=MissingSurfaceCapabilities2`,
  `2=SeparateQueueFamilies`, `3=NriInitializationFailed`,
  `4=NriCreationFailed`. Il dettaglio testuale conserva la causa precisa
  restituita da NRI.
- `tools/Test-Oot3dVulkanAdapter.ps1` confronta selezione automatica ed
  esplicita sulla stessa scena, richiede stabilita dei dati in tutti i frame,
  coerenza della ownership NRI/fallback e parita di draw, display copy,
  scanout. Sul checkpoint RTX 3060 sono verificati adapter
  `0`, family `0:0`, 16 queue disponibili, 1.200 draw, 30 copie e 12 scanout.
  CACAO+Hi-Z+TAA conserva la stessa selezione con
  1.806 draw, 45 copie e 18 scanout. Le topologie multi-GPU e a family
  separate sono coperte deterministicamente dai test puri; resta richiesta
  la qualifica su hardware fisico aggiuntivo prima di eliminare il fallback.
- Il profiler GPU e ora diviso tra `GpuProfileFramePlan`, state machine pura,
  e `Oot3dVulkanGpuProfiler`, unico owner del query pool Vulkan. Quattordici
  scope ricevono coppie di query non sovrapposte: frame, raster PICA, raster
  toon, grass, CACAO, depth preparation/Hi-Z, SSR, motion vectors, composite,
  AA, upscaler, display transfer, scanout e UI overlay. Gli scope opzionali
  vengono letti separatamente, quindi una query mai scritta non rende
  `NOT_READY` l'intero frame.
- Il profiler e attivo soltanto insieme alla diagnostica opt-in e conserva due
  frame in flight: gli ultimi due record sono dichiaratamente unsettled e
  mantengono timing nulli. Ogni scope accetta il primo intervallo del frame:
  per raster PICA, display transfer e scanout ripetuti il valore non e quindi
  presentato come somma del frame. `toon_raster_ms` misura il raster PICA
  eseguito con toon attivo, mentre `toon_draw_count` e
  `pica_material_toon_draw_count` distinguono applicazione reale e percorso
  fragment-lighting; non viene presentato come costo differenziale del solo
  shader.
- `tools/Test-Oot3dGpuProfiling.ps1` verifica sulla scena giocabile tre run:
  Authentic, grass+Cacao+Hi-Z+PicaToon+TAA e NIS. Per ogni pass realmente
  contato richiede un timestamp non negativo in tutte le frame settled,
  oltre alla parita dei draw PICA/NRI. Sul checkpoint RTX 3060 sono stati
  misurati nello stesso run avanzato grass, raster toon, CACAO, depth prep,
  SSR, motion, composite, TAA, transfer, scanout e overlay; il run NIS
  certifica separatamente `upscaler_ms`.
- La validation e ora un sottosistema opt-in condiviso ma non monolitico:
  `Oot3dVulkanValidation` possiede layer/debug messenger,
  `RendererValidationTelemetry` mantiene contatori atomici separati e
  `NriInteropContext` collega la callback NRI soltanto con
  `OOT3D_VULKAN_VALIDATION=1`. La diagnostica salva per frame enable,
  info, warning ed errori di entrambe le API, aggiornandoli anche a fine
  frame per non perdere una segnalazione dell'ultimo command buffer.
- Il bring-up della validation ha corretto i feature enable
  `independentBlend`, `multiViewport`, `shaderImageGatherExtended` e
  buffer device address; le barrier NRI usano soltanto vertex, fragment e
  compute sui device senza geometry/tessellation/mesh/ray tracing. I
  `CmdClearStorage` condividono ora `NriStorageClearBinding`, owner del
  pipeline layout, pool e set richiesti dal contratto NRI. Le blend constants
  vengono registrate soltanto dalle pipeline che le dichiarano dinamiche.
- CACAO conserva integralmente le proprie transizioni Vulkan dell'output:
  il wrapper non emette piu una seconda transizione
  `GENERAL -> SHADER_READ_ONLY`. Questo mantiene esplicito il confine
  dell'adapter FidelityFX senza introdurre ownership concorrente.
- `tools/Test-Oot3dRendererValidation.ps1` esegue Authentic con memory-fill
  sintetico e CACAO+Hi-Z+TAA sulla scena Kokiri. Aggiunge una transizione
  avanzata nella stessa sessione, dal checkpoint di casa di Link al savestate
  rapido di Kokiri tramite restore test-only al frame 6. I tre casi richiedono
  layer Vulkan e NRI attivi, zero errori cumulativi e parita draw PICA/NRI; il
  profilo avanzato deve inoltre contare tutti e tre i pass e il consumer
  ambient-aware, forzando esplicitamente CACAO Medium per esercitare la normal
  guide senza dipendere dal salvataggio utente. Sul checkpoint
  RTX 3060 sono verificati 8+8+16 frame, 1.402 draw per caso stabile e 2.192
  draw nella transizione, 7 dispatch CACAO,
  tutti e 7 con normal guide PICA, 14 compositing ambient-aware, 14 Hi-Z/SSR
  e 6 TAA. La transizione conta due load distinti e conserva ownership esatta
  su tutti i 14 frame utili con CACAO+Hi-Z+TAA attivi, con zero errori
  Vulkan/NRI. Le transizioni automatizzate attivano CACAO dal frame 5 e
  confermano zero dispatch e zero composite dal frame 41 dopo la
  disattivazione. I warning Vulkan
  non bloccanti (layer ReShade locale e interfacce PICA con attributi/output
  opzionali) restano visibili e conteggiati; NRI riporta zero warning.
- `tools/Test-Oot3dFidelityFxSssr.ps1` esegue un checkpoint breve sulla scena
  giocabile con validation Vulkan/NRI attiva e provider FidelityFX forzato.
  Richiede parita draw PICA/NRI, dispatch SSSR e motion reali, output e wrapping
  NRI, environment derivato da PICA, cube/BRDF/resolve posseduti da NRI,
  fallback Hi-Z sulle sole view prive di input temporali, applicazione di una
  regola texture reale e zero errori. Sul checkpoint RTX 3060 sono verificati
  8 frame, 1.402 draw,
  6 dispatch SSSR, 6 usi IBL, una rigenerazione del profilo, 6 resolve
  materiale, 14 draw profilati tramite l'hash catalogo
  `be15aff93dfdcd88`, 6 conversioni/scanout lineari, 6 motion vector e 8
  fallback Hi-Z. L'inventario osserva 34 identita texture distinte e deve
  attribuire gli stessi 14 draw all'hash polished esatto, con parametri
  0,58/0,32 e nessun overflow. Con `-WithTaa` gli stessi sei frame devono
  inoltre eseguire composite e TAA, sempre con zero errori Vulkan/NRI. Il
  launcher cattura ora
  un frame stabilizzato e `Test-Oot3dFramebufferArtifact.ps1` ne impedisce
  l'accettazione se uniforme o quasi monocromatico. I checkpoint SSSR diretto
  e SSSR+TAA correnti superano 2.600 colori campionati, range 255 e frazione
  cromatica 0,87. Le warning non bloccanti
  delle immagini storage generate dall'SDK, dell'interfaccia PICA opzionale e
  del layer ReShade restano visibili; non vengono promosse artificialmente a
  errori.
- `tools/Test-Oot3dReflectionProviderComparison.ps1` confronta Hi-Z e SSSR
  sulla stessa scena/materiale, con validation attiva, parita di 1.402 draw,
  14 draw polished e 34 identita texture. Sul checkpoint RTX 3060 diretto
  misura circa 0,102 ms Hi-Z e 1,685-1,921 ms SSSR nei run ripetuti sul primo
  scope reflection; i BMP differiscono sull'1,864% dei pixel campionati, con
  delta medio 1,037 e massimo 247. Il test non usa il post-TAA come metrica
  qualitativa, perche l'accumulo temporale puo legittimamente ridurre la
  differenza tra provider. Con `-TemporalStability`, quattro frame per provider
  producono delta canale medio 0,5251345 Hi-Z e 0,5251220 SSSR, con frazione
  media di pixel cambiati 0,0355917 e 0,0355874: SSSR resta entro l'envelope
  Hi-Z senza flicker aggiuntivo misurabile sul checkpoint corrente.
- La matrice `tools/Test-Oot3dReflectionSceneMatrix.ps1` estende il gate A/B
  senza aggiungere un secondo modello di diagnostica: usa gli stessi parser,
  validator framebuffer e metriche provider su savestate corrente, foresta e
  casa di Link. Richiede inoltre firme draw, conteggi texture e screenshot
  distinti, impedendo che un errore di caricamento faccia passare tre volte la
  stessa scena. Il run corrente verifica 2.984 draw e 240 draw profilati per
  provider, con zero errori Vulkan/NRI.
- `oot3d_graphics_foundation_tests` copre ora 140 casi, inclusi round-trip
  completo dello schema Graphics, migrazione legacy, riparazione di input
  malformati, preservazione di versioni future, policy delle transazioni,
  sostituzione atomica del JSON e modello condiviso di selezione/assegnazione
  texture, comprese identita con hash uguale e dimensioni diverse e
  navigazione bidirezionale con wrap, encode/decode del peso ambientale,
  policy Low/Medium del normal input CACAO, trasformazione view-space
  identita, fallback legacy scalare e contratto RGB per-canale con validity
  bit, semantica dei canali della debug view reflection,
  encoding BMP top-down dei texel preview e compilazione della variante
  fragment/compute.
  Il frontend
  sintetico verifica inoltre che sia il rapporto ambient/direct scalare sia
  quello RGB per-canale vengano emessi prima del TEV senza modificare texture
  o colore. Il contratto CPU/GLSL condiviso seleziona RGB esatto quando la
  guida e valida e ricade deterministicamente sul normal-guide alpha per gli
  shader/runtime originali. L'attachment RGB single-sample/MSAA e ora parte
  del contratto condiviso a cinque MRT, e posseduto/inizializzato
  transazionalmente da NRI e pubblicato nel registry scena. Pipeline PICA,
  dynamic rendering, render pass legacy e grass usano lo stesso ordine
  tipizzato; il clear `{1,1,1,0}` rende il fallback deterministico. La
  variante fragment scrive RGB esatto con validity uno per il fragment
  lighting, oppure validity zero per shader originali/vertex-lit. Scene
  composite compute e scanout diretto consumano la stessa funzione RGB con
  fallback normal-alpha. Il gate giocabile Authentic/Advanced verifica
  validation pulita, parita di 594 draw per quattro frame, 300 draw
  ambient-guide attribuiti (tutti fallback nella scena vertex-lit corrente),
  sei composite CACAO e framebuffer non collassati; il test shader sintetico
  compila e qualifica il ramo RGB esatto.
  Il writer JSON atomico e
  isolato dal grafo Window/Context per mantenere puro il gate.
  Un smoke di otto frame sulla scena giocabile verifica inoltre
  avvio, migrazione reale a `Graphics.SchemaVersion=1`, framebuffer non
  collassato e uscita pulita.
- Mantenere test A/B fino alla parita su hardware aggiuntivo.
- Eliminare il legacy raw Vulkan soltanto dopo copertura multi-GPU e parita
  Authentic.

### Filoni paralleli obbligatori

Le funzioni gia previste non vengono rinviate a Release F:

- settings service e UI iniziano in Release A;
- FOV e view metadata sono parte del percorso critico di Release A;
- estrazione superfici, catalogo texture e bridge Link possono procedere in
  parallelo alla fondazione NRI, ma l'erba atterra come prima feature
  geometrica in Release B;
- finestra/borderless/fullscreen, risoluzione e 30/60/libero procedono sul
  backend swapchain attuale;
- FXAA/render scale sono entrati in Release A; SMAA/MSAA della Release C sono
  operativi;
- TAA e upscaler seguono necessariamente la fondazione temporale.

## Test e criteri di accettazione

### Parita e sicurezza

- `Authentic` deve riprodurre le baseline PICA con effetti e guide realmente
  esclusi dal frame.
- Nessun double-destroy o ownership ambiguity tra Vulkan, NRI e FidelityFX.
- Validation Vulkan e NRI senza errori, verificata automaticamente da
  `tools/Test-Oot3dRendererValidation.ps1` su Authentic,
  CACAO+Hi-Z+TAA e restore con cambio scena, e da
  `tools/Test-Oot3dFidelityFxSssr.ps1` sul provider
  FidelityFX con fallback Hi-Z per-view. `tools/Test-Oot3dSmaa1x.ps1`
  verifica inoltre SMAA su scena giocabile con CACAO, parita draw,
  ownership NRI di lookup/output/compute/barrier/upload e modifica
  misurabile del framebuffer.
- `tools/Test-Oot3dToon.ps1` esegue i test contrattuali del generatore PICA e
  del composite, richiede che nessun percorso tocchi sampling o albedo delle
  texture e qualifica visivamente preset, controlli stile e outline con
  framebuffer non collassati, draw invariati e validation pulita.
- `tools/Test-Oot3dNisUpscaler.ps1` qualifica NIS Quality per otto frame sulla
  scena giocabile: richiede scala 1,5x, dispatch per ogni frame renderizzato,
  output e barrier posseduti da NRI, draw PICA/NRI presenti, framebuffer non
  collassato e zero errori di validation Vulkan/NRI.
- `tools/Test-Oot3dFsrUpscaler.ps1` qualifica FSR Quality dopo il bootstrap
  temporale: richiede scala 1,5x, motion per ogni dispatch, almeno un reset
  history, view temporale valida, output/barrier NRI, scena giocabile non
  collassata e zero errori di validation Vulkan/NRI.
- `tools/Test-Oot3dDlssUpscaler.ps1` applica lo stesso contratto a NGX DLSR:
  su adapter NVIDIA richiede dispatch, motion, reset history, output/barrier
  NRI e validation pulita; su adapter non NVIDIA produce uno skip esplicito,
  senza trasformare l'assenza hardware in un falso fallimento del renderer.
- Shader/pipeline cache key include la variante attachment/guide, ma i parametri
  toon regolabili restano uniform per evitare permutation explosion.
- Resize, device/swapchain recreation e cambio render scale invalidano wrapper,
  descriptor e history in modo deterministico.

### Erba interattiva

- Una regola corrisponde soltanto alle mesh che usano hash/nome/mapper slot
  selezionati; HUD, actor, sky e texture omonime con hash diverso non ricevono
  anchor.
- `SubmitInteractiveWorldGeometry` e sempre tra ultimo world opaque/alpha
  test e primo translucent/HUD; se il marker manca, grass e off con causa
  diagnostica invece di essere disegnato tardi.
- Channel, invert, threshold, gamma, mip, wrap e UV sono confrontati con un
  sampler CPU di riferimento su texture note.
- Aggiunta o modifica di una regola dopo lo strip ri-decodifica in background
  la sola texture richiesta; gli RGBA vengono rilasciati dopo la mask e il
  render thread non attende I/O.
- Stessa scena/regola/seed produce lo stesso hash degli anchor dopo riavvio,
  su GPU diverse e con 30/60/libero.
- La densita per area resta entro una tolleranza dichiarata su due mesh
  geometricamente uguali ma ritessellate.
- Triangoli degenerati, slope esclusa, scale non uniformi, UV fuori 0..1 e
  texture animate seguono i fallback documentati.
- La modifica di un parametro di placement ricostruisce soltanto le cache
  interessate senza frame hitch o buffer parzialmente visibile.
- Il vento resta ancorato al mondo e ha uguale velocita apparente a ogni frame
  cap; pausa e savestate rispettano la policy del `VisualClock`.
- Link piega l'erba durante cammino, corsa e rotolata; il volume swept evita
  tunneling e il test verticale evita interazioni su ponti/piani sovrapposti.
- Recovery, max bend e impulso sono stabili; teleport, cambio room e savestate
  azzerano field e campione precedente.
- L'erba scrive depth/coverage soltanto sui texel alpha validi, riceve AO, non
  riceve SSR per default e non contamina HUD.
- Debug view mostra texture eligibility, mask campionata, anchor, LOD, collider
  corrente/precedente e interaction field.

### Effetti

- Depth linearization verificata con Z-buffer e W-buffer PICA.
- SSAO non oscura sky, emission, HUD o overlay.
- SSR non riflette HUD e non si abilita su materiali `Unknown`.
- Nessun ray feedback dalla texture che il pass sta scrivendo.
- PicaMaterialToon conserva texture animation, TEV, alpha test, fog, shadow e
  blend.
- Outline stabile durante rotazione camera, cambio FOV e render scale.
- Camera cut e savestate azzerano tutte le history interessate.

### Display, pacing e camera

- Windowed, borderless ed exclusive fullscreen sono ripetibili e rollback-safe.
- Output, internal, UI ed effect resolution restano indipendenti.
- HUD e ImGui sono nitidi e non attraversano TAA/upscaler/effect history.
- 30/60/libero non alterano clock guest, audio, input o determinismo savestate.
- Uncapped riporta quanti frame sono interpolati, duplicati o nativi.
- FOV 1.00-1.50 si applica a gameplay, cutscene, sottocamere e debug camera,
  mai alle proiezioni ortografiche.
- SSAO/SSR ricevono esattamente la proiezione modificata dal FOV.

### Prestazioni e compatibilita

- GPU timestamp separati per grass field/cull/draw, depth prep, CACAO, SSR,
  toon, AA e UI; telemetria separata per tempo CPU di placement e cache hit.
- Preset con budget separati per 30 e 60 FPS; nessun quality downgrade dinamico
  nascosto per default.
- Budget verificati per anchor per room, memoria instance buffer, field e
  rebuild queue, con culling/LOD forzabili nei test.
- Test su almeno una GPU NVIDIA, AMD e Intel.
- Fallback pulito quando sampling depth, subgroup, sample count, tearing,
  indirect draw, compute interaction, upscaler o un SDK non sono disponibili.

## Rischi principali e mitigazioni

| Rischio | Mitigazione |
|---|---|
| Fragment lighting PICA ancora scollegato | SSAO ed erba prima; PostToon/PicaMaterialToon gated dopo Release B |
| HUD nello stesso framebuffer | draw class, coverage guide e marker world/overlay |
| Depth PICA non lineare/W-buffer | debug view, view metadata e test numerici prima di CACAO/SSR |
| Match texture fragile | hash RGBA8 + nome/dimensioni/slot; mai indice o handle runtime |
| RGBA texture strippati dopo upload | catalogo dai metadati persistenti, re-decode on demand e sola mask scalare residente |
| Memoria duplicata dal sampling | reference count per regola e rilascio RGBA subito dopo mask/cache anchor |
| UV/materiale animato sposta il placement | authored UV statiche per default; effective-at-load opt-in; niente resampling per frame |
| Esplosione di anchor/draw | densita area-based, limite per room, instance buffer, culling/LOD e pochi bucket |
| Collisione incoerente con Link | actor position e collider di gameplay, sweep previous/current, test verticale |
| Field residuo dopo warp/savestate | stable id e reset esplicito su teleport, room, scene e restore |
| Alpha foliage aliasa depth/guide | alpha test/dither stabile e alpha-to-coverage con MSAA |
| Nessun confine world affidabile | hook esplicito e capability gate; mai grass injection dopo HUD |
| Nessuna roughness PBR originale | proxy da semantiche CMB/PICA, profili opt-in, unknown off |
| Colore PICA LDR/non documentato | target autentico invariato; policy sRGB testata, working color RGBA16F e scanout con conversione esplicita; vero HDR/tone mapping resta separato |
| SSR instabile senza motion | percorso deterministico/spaziale; SSSR solo dopo temporal foundation |
| Doppia gestione swapchain | ownership atomica NRI/Vulkan e worker abilitato soltanto nel fallback |
| Esplosione shader variant | due famiglie Authentic/Guided; parametri effetti via uniform |
| Dipendenze vendor pesanti | pin immutabile, build minima, notices e adapter isolati |

## Dipendenze e riferimenti da bloccare

- [NVIDIA Rendering Interface](https://github.com/NVIDIA-RTX/NRI), pin `v180`
  e commit indicato sopra.
- [SMAA](https://github.com/iryoku/smaa), shader e lookup ufficiali bloccati
  al commit `71c806a838bdd7d517df19192a20f0c61b3ca29d`, licenza MIT conservata in
  `THIRD_PARTY_NOTICES.md` e nel sorgente shader embedded.
- [GPU Gems, Rendering Countless Blades of Waving Grass](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-7-rendering-countless-blades-waving-grass),
  riferimento per quad incrociati, LOD e animazione per grass object; nessuna
  dipendenza runtime.
- [GPU Gems 3, Vegetation Procedural Animation and Shading in Crysis](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-16-vegetation-procedural-animation-and-shading-crysis),
  riferimento per main/detail bending, stiffness e sorgenti vento locali.
- [Vulkan Fragment Operations](https://docs.vulkan.org/spec/latest/chapters/fragops.html),
  contratto normativo per sample mask e alpha-to-coverage.
- [NVIDIA Turf Effects](https://developer.nvidia.com/turfeffects), solo
  riferimento comparativo; non selezionato perche legacy e DX11.
- [FidelityFX CACAO](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/combined-adaptive-compute-ambient-occlusion/),
  implementazione primaria SSAO; bloccare un tag/commit SDK dopo lo spike
  Vulkan e includere i notice richiesti.
- [FidelityFX SSSR](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/stochastic-screen-space-reflections/),
  candidato Ultra dopo la fondazione temporale.
- [Godot BoTW Toon Shader](https://github.com/nekotogd/Godot_BoTW_Toon_Shader),
  riferimento visivo CC0, non dipendenza runtime.
- [XeGTAO](https://github.com/GameTechDev/XeGTAO), riferimento alternativo non
  selezionato per la prima integrazione.

Ogni dipendenza va acquisita da tag/commit immutabile, con hash, licenza e
notice registrati. Non copiare shader da repository terzi senza mantenere
provenienza e condizioni di licenza.
