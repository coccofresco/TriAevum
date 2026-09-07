# Renderer3DS NRI PICA Execution Handoff

## Stato congelato

La meccanica di creazione pipeline NRI, binding draw, descriptor, upload sparsi
e trasformazione dei sampler GLSL e stata spostata da `fast/oot3d` a
`fast/renderer3ds`. Il modulo comune non contiene token o policy di Zelda.

Il backend esistente conserva la stessa API tramite
`fast/oot3d/nri_pica_pipeline_bridge.*`, ora ridotto a un adapter che:

- espone al modulo comune solo i servizi Vulkan/NRI necessari;
- conserva le variabili ambiente `OOT3D_GRAPHICS_NRI_PICA_DRAWS` e
  `OOT3D_GRAPHICS_NRI_PICA_UPLOADS`;
- mantiene invariati tipi e chiamanti del backend OOT3D.

Il nuovo target isolato e
`ThreeDsRecomp::Renderer3dsNriPicaExecution`. Il core portabile
`Renderer3dsPicaCore` resta privo di dipendenze Vulkan/NRI.

## Verifiche completate

- gate cross-title: 38 file title-neutral;
- build del target `renderer_3ds_nri_pica_execution`;
- build dell'adapter OOT3D;
- build di `gfx_vulkan.cpp` e `gfx_vulkan_pica.cpp`;
- 15/15 test mirati PICA NRI, attachment e upload;
- link completo di `oot3d_native_game` e seconda build senza compilazione o
  relink;
- run Kokiri di 60 presentazioni: 9.233 draw, zero mismatch di composizione,
  draw rifiutati, errori/warning Vulkan o NRI;
- framebuffer finale byte-identico alla baseline, SHA-256
  `4F368098A185480A3FB94EAA0F1505460E4F7E24D47FC4D6A21DAD7F4B86EB74`.

## Non verificato in questa tranche

- uso del nuovo executor da un frontend di un secondo titolo;
- estrazione di texture decode, render-target ownership e display transfer,
  che restano tranche indipendenti successive.

## Ripresa consigliata

1. Aggiungere un test dell'executor con un finto `NriPicaInterop` oppure un
   harness Vulkan headless.
2. Scegliere una singola tranche successiva tra texture
   upload/decode e display transfer; non spostarle insieme.

Build directory usata: `I:/oot3dre_work/whole-aot-product-consumer`.
Evidenza runtime:
`I:/oot3dre_work/visual-parity/portable-3ds-nri-executor-20260826`.
La cattura accettata e `frame-final.png`; `frame.png` e intenzionalmente
respinta perche, senza `ScreenshotStartFrame=59`, fotografa il frame iniziale
nero prima della prima presentazione utile.
