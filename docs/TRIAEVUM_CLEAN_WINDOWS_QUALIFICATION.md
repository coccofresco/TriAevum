# Qualificazione Windows pulito

Eseguire su una macchina/VM Windows senza Visual Studio, Build Tools o SDK
installati. Non basta ripulire le variabili d'ambiente del PC di sviluppo.
La macchina deve supportare Vulkan per la parte di avvio grafico; una VM senza
Vulkan puo' verificare l'importazione, non sostituire la prova del runtime.

## Input e comando

- Candidate immutabile con catalogo e moduli precompilati (serie 0.6.0 e successive).
- ROM `.cci`/`.3ds` decriptata dell'utente, in sola lettura.
- Script `tools/triaevum_release/qualify_clean_windows.ps1`, copiato nella VM.
- Cartella output nuova, esterna agli input, con spazio per dati estratti.

Esempio nella macchina di prova (adattare le sole directory):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\qualify_clean_windows.ps1 -Package D:\input\TriAevum -Rom D:\input\oot3d.cci -Output C:\TriAevumQualification
```

Lo script copia gli input modificabili nella nuova installazione, invoca il
worker **frozen** realmente usato dalla GUI, attende estrazione e attivazione
e poi avvia il runtime per 30 secondi. Timeout importazione 180 secondi, avvio 90;
nessun processo rimane volontariamente attivo al termine. Non installa componenti
di Windows e non richiede Python o toolchain dell'host.
Usare il processo `powershell.exe -File` mostrato, non dot-sourcing: il worker
osserva quel PID e termina se il processo di verifica esce.

## Evidenze

`qualification.json` registra hash ROM/manifesto, ambiente osservato, tempi ed
exit code. Log separati per Forge/runtime, `runtime.json` e framebuffer BMP.
Controlla l'assenza di SDK/cache compilatore, zero oggetti compilati, l'evento
finale di installazione e TopScreen/x2 effettivi. La presenza
del BMP non basta a certificare contenuto corretto: va ispezionato.

I risultati sono **privati**: contengono ROM estratta, DLL generata, percorsi e
catture. Non aggiungerli al pacchetto pubblico o al repository.

L'inventario degli strumenti non prova da solo l'assenza di tool di sviluppo:
documentare origine/creazione della VM e configurazione. Il risultato positivo
rimane `import_and_boot_passed_pending_visual_audio_and_environment_review` fino
alla verifica dell'ambiente, del framebuffer e dell'audio. F1/input/persistenza
restano una verifica interattiva distinta.

Il modello precompilato elimina del tutto download/acquisizione SDK dal percorso
utente. La compilazione resta esclusivamente nel workflow degli sviluppatori.

## Stato corrente

Parser PowerShell e test reali di preflight superati: nessuna sovrascrittura di
output esistente e nessuna copia ricorsiva dentro gli input. Il nuovo percorso
ROM-only ha completato importazione e boot della candidate 0.6.0-r1 in una
cartella vuota sul PC di sviluppo, senza SDK/cache copiati. Tempi e prove in
`TRIAEVUM_PRECOMPILED_RELEASE.md`. Non e' una prova su Windows pulito:
WindowsSandbox.exe non e' disponibile e non e' stata creata implicitamente una VM.
