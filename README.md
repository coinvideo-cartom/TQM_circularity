# TQM Misura Circonferenza

Applicazione desktop Qt 6 / C++17 e OpenCV 4.11. Acquisizione dei punti da immagini; nessun driver camera richiesto o integrato.

## Uso

1. Aprire l'immagine del blocchetto Johansson. Inserire la distanza certificata tra le due facce in mm.
2. Selezionare i due lati: quattro click, due estremi per lato. Evitare gli spigoli. Gli estrattori vengono distribuiti lungo ciascun segmento, con asse di scansione perpendicolare. La normale segue l'ordine dei due click; il selettore Direzione permette di invertirla. In calibrazione la polarita Entrambe facilita la misura di lati opposti.
3. Premere Esegui. Controllare frecce, punti verdi, ROI rosse (edge mancante o ROI fuori immagine), RMS delle rette e parallelismo. Servono almeno 6 punti per lato, almeno l'80% degli estrattori riusciti e un angolo tra rette non superiore a 2 gradi.
4. Salvare la calibrazione JSON se desiderato. Aprire l'immagine del pezzo conservando ottica, distanza di lavoro, piano di misura e risoluzione. La scala viene azzerata automaticamente se cambia la risoluzione; gli altri cambiamenti richiedono nuova calibrazione manuale.
5. Selezionare circonferenza: tre click su punti distinti del bordo, ben distanziati e non allineati. Il cerchio passante per i tre punti appare in anteprima. Impostare numero e dimensioni degli estrattori, sigma, soglia, polarita, edge piu forte/primo/ultimo e direzione radiale esterna/interna. Premere Esegui.
6. Esportare CSV con punti e residui radiali. Coordinate immagine: origine in alto a sinistra, x a destra, y in basso. Le coordinate mm mantengono questa origine.

La rotella esegue lo zoom; Adatta immagine ripristina la vista. Per correggere i click premere nuovamente il pulsante di selezione. Dopo modifiche ai parametri rieseguire la misura.

## Algoritmi e significato dei risultati

L'estrattore campiona una ROI rettangolare con interpolazione bilineare, media trasversalmente, filtra il profilo con una Gaussiana, calcola la derivata centrale e interpola parabolicamente i massimi del modulo del gradiente. La soglia e in livelli di grigio per pixel su immagini convertite a 8 bit. I margini di filtraggio sono esclusi dalla ricerca; una ROI non interamente campionabile viene rifiutata.

Riferimenti consultati:
- [HALCON gen_measure_rectangle2](https://www.mvtec.com/doc/halcon/2511/en/gen_measure_rectangle2.html): geometria e interpolazione della ROI.
- [HALCON measure_pos](https://www.mvtec.com/doc/halcon/2411/en/measure_pos.html): profilo medio, derivata gaussiana, polarita e selezione degli edge.

Implementazione ispirata al principio di HALCON, senza dipendenza o equivalenza numerica con HALCON. In particolare soglia e ampiezza non riproducono la normalizzazione HALCON.

La calibrazione stima due rette, controlla il parallelismo e misura la separazione lungo la normale comune: px/mm = distanza_px / lunghezza_mm. La scala e isotropa; non corregge distorsione, prospettiva o inclinazione del piano. Il solo blocchetto non permette una calibrazione completa della camera.

Il cerchio e inizializzato con un fit algebrico e raffinato con minimi quadrati geometrici. P-V = massimo raggio meno minimo raggio rispetto al centro di questo cerchio. RMS = radice della media dei residui radiali al quadrato. Il diametro LSC e due volte il raggio medio del cerchio raffinato. La zona minima viene calcolata separatamente come descritto sotto. Nessun punto viene eliminato come outlier: difetti reali e falsi edge possono entrambi influenzare P-V. La copertura minima richiesta e 80%; il risultato riguarda soltanto i punti trovati, non garantisce il rilevamento di difetti tra estrattori o nei settori mancanti.

## Misure del cerchio

- Diametro LSC in mm con calibrazione, altrimenti px.
- Picco-valle e RMS dei residui LSC in micrometri con calibrazione, altrimenti px.
- Rotondita MZC numerica: differenza tra raggio esterno e interno dell'anello concentrico ottimizzato, nelle stesse unita degli errori LSC. I due cerchi arancioni racchiudono i punti; il cerchio magenta e il riferimento LSC.
- Edge validi/totali e percentuale. Il massimo intervallo angolare e la distanza tra due estrattori validi consecutivi, compresa la chiusura 360/0 gradi, riferita al centro iniziale di acquisizione. Con 360 estrattori tutti validi vale 1 grado; nessun edge valido produce 360 gradi. Il valore include il passo di campionamento, non solo gli estrattori mancanti.
- Grafico dei residui LSC rispetto all'angolo misurato dal centro LSC: zero a destra e senso orario nelle coordinate immagine. Ogni punto e visualizzato separatamente, senza interpolare i settori mancanti.

La zona minima usa Nelder-Mead con nove inizializzazioni e coordinate normalizzate; tolleranza geometrica del simplesso 1e-10 volte il raggio LSC, massimo 1000 iterazioni per inizializzazione. Il risultato e una stima numerica del minimo, senza certificazione del minimo globale. Se nessuna inizializzazione converge, la misura viene dichiarata non valida. L'anello LSC e mantenuto come limite superiore della larghezza. Nessun outlier e escluso. Il filtro spline del profilo radiale e attivabile come descritto sotto.

Il numero iniziale di estrattori e 360. Precisione visualizzata e tolleranza dell'ottimizzatore non sono l'incertezza della misura ottica.

Il CSV contiene una riga per edge valido, con indice originale dell'estrattore (base zero), angolo LSC, coordinate, residuo LSC, raggio rispetto al centro MZC e tutte le misure riepilogative ripetute su ciascuna riga. Sono inclusi centro e raggi MZC in pixel e copertura. Con calibrazione si aggiungono coordinate/diametro in mm ed errori in micrometri; i suffissi delle colonne indicano le unita. I punti mancanti sono riconoscibili dagli indici saltati.

## Compilazione e verifica

Aprire `TQM_MisuraCirconferenza.pro` con il kit Qt 6.2.4 MSVC 64 bit. `OPENCV_DIR` e configurabile da qmake; predefinito `D:/librerie/opencv_4_11_0/build`, come negli altri progetti locali. DLL OpenCV copiata dopo il link. L'interfaccia e costruita in C++; il vecchio mainwindow.ui non viene usato.

Nell'ambiente locale: `build/compile.bat` compila Release; `build/test.bat` verifica subpixel, polarita, verso, bordi immagine, immagini uniformi, calibrazione inclinata, parallelismo, estrazione radiale, fit circolare, profilo trilobato e degenerazione collineare. `build/test-ui.bat` verifica il flusso completo su un profilo trilobato sintetico, unita calibrate/pixel, copertura insufficiente e invalidazione; salva `build/measurement-ui.png`. I test numerici includono zona minima nota, differenza da LSC, invarianza per traslazione e scala e intervalli angolari attraverso 360/0 gradi. `--smoke-test` avvia la finestra, salva ui-smoke.png e termina.

Validare precisione e ripetibilita su immagini reali e campioni noti prima dell'uso produttivo.



## Cerchio inscritto, circoscritto e differenza radiale

La misura aggiunge diametro MIC (massimo inscritto), diametro MCC (minimo circoscritto) e `Differenza radiale MCC-MIC = R_MCC - R_MIC = (D_MCC - D_MIC)/2`. I due centri sono indipendenti: questa differenza e un indicatore aggiuntivo e non viene presentata come rotondita MZC. Diametri in mm e differenza in micrometri quando calibrato, altrimenti pixel.

Il MCC usa OpenCV minEnclosingCircle su coordinate normalizzate; il raggio viene ricalcolato in doppia precisione sui punti originali per assicurare il contenimento. E un risultato numerico, con centro calcolato in precisione float da OpenCV.

Il MIC usa il poligono chiuso ottenuto collegando in ordine gli edge acquisiti, inclusa la chiusura ultimo-primo, con segmenti rettilinei. Il raggio e la distanza minima ai segmenti, non soltanto ai vertici. La ricerca branch-and-bound sulla distanza con segno termina quando il limite superiore residuo differisce dal miglior raggio di non oltre 0.001 px. Questa e la tolleranza di calcolo sul poligono, non l'incertezza ottica. Un massimo di 250000 celle impedisce calcoli illimitati; al superamento MIC e differenza risultano non disponibili. Sono rifiutati contorni degeneri, duplicati e auto-intersezioni.

MIC e differenza non sono disponibili se l'intervallo angolare massimo tra estrattori validi supera 5 gradi. Questo controllo comprende anche il passo nominale: con meno di 72 estrattori il MIC non e disponibile. MCC e le misure precedenti restano consultabili quando valide. Anche con copertura completa, l'interpolazione per corde influenza il MIC: per un cerchio campionato regolarmente il raggio inscritto nel poligono e leggermente inferiore al raggio reale. Aumentare la densita dei punti e verificare la stabilita del risultato.

Sull'immagine MIC e ciano e MCC rosso, tratteggiati, con croci sui rispettivi centri. Il CSV aggiunge diametri, centri in pixel e differenza radiale; le colonne MIC e differenza sono vuote quando non disponibili, mai impostate a zero. I test includono forme note, contorno concavo, inversione dell'ordine, traslazione, gap angolari e CSV con/senza calibrazione e MIC.

## Filtro spline periodico del profilo radiale

Attivare "Spline periodica ISO 16610-22" e premere Esegui. Il filtro e inizialmente disattivato. I valori iniziali scelti per l'applicazione sono taglio 50 UPR, tensione beta 0 e massimo intervallo tra punti acquisiti 5 gradi. La tensione e regolabile da 0 a 1. La calibrazione del blocchetto usa sempre i punti acquisiti.

Le equazioni implementate sono quelle periodiche di ISO 16610-22:2015, paragrafo 4.3.3, verificate sull'anteprima normativa. La conformita completa all'edizione 2026 resta da verificare sul testo integrale.
Fonte consultata https://cdn.standards.iteh.ai/samples/61377/e40635664509456c80df5997024957f8/ISO-16610-22-2015.pdf

Il centro LSC acquisito definisce il profilo polare. Un'interpolazione lineare periodica dei raggi produce N campioni equiangolari, con N uguale al numero di estrattori e angolo iniziale zero verso destra. Il filtro risolve mediante DFT il sistema ciclico della spline. Per la frequenza k UPR, posto q = sin(pi*k/N)/sin(pi*fc/N), la trasmissione e H = 1/(1 + beta*q^2 + (1-beta)*q^4). La componente costante e conservata e al taglio fc l'ampiezza e dimezzata.

Il ricampionamento e una scelta applicativa. Sono verificati ordine radiale, raggi positivi e massima distanza angolare fra punti, inclusa la chiusura. Il taglio deve essere inferiore sia a N/2 sia a 180/gap_max in gradi. Quest'ultimo e un controllo applicativo sul campionamento acquisito. Le interpolazioni possono influenzare il risultato nei settori mancanti.

Il contorno filtrato viene ricostruito e sottoposto nuovamente ai calcoli LSC, MZC, MCC e MIC. Il MIC filtrato richiede anche un intervallo acquisito massimo di 5 gradi. I risultati sono etichettati per profilo. Nel grafico entrambi i profili usano come riferimento il centro e il raggio LSC acquisiti, per un confronto diretto. I valori RMS e picco-valle filtrati sono invece riferiti al nuovo fit LSC filtrato. Sull'immagine il contorno filtrato e viola e i cerchi di riferimento mostrati appartengono al profilo acquisito.

Con filtro attivo il CSV contiene prima le righe acquired, poi le righe filtered, identificate da profile_type. Ogni gruppo contiene coordinate, residui e misure calcolati sul rispettivo profilo. probe_index e vuoto per i campioni ricampionati; sample_index indica l'indice nel gruppo. resampled_input_radius_px contiene il raggio prima del filtro sulla griglia uniforme. Le colonne filter_* registrano formulazione, taglio, tensione, limite e gap osservato, numero di campioni e centro del ricampionamento. pixels_per_mm registra la scala se disponibile. La copertura riportata in entrambi i gruppi riguarda gli edge realmente acquisiti.

I test verificano il sistema ciclico, il taglio al 50%, la fase, il raccordo periodico, i limiti di interpolazione, i parametri non validi e il flusso completo con 7200 estrattori e CSV calibrato/in pixel.