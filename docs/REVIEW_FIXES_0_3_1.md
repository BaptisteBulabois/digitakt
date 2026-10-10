# Corrections de la revue — développement 0.3.1

Ce lot applique les problèmes confirmés de la revue à la branche
`development/0.3-machines-modulation`. `main` reste au commit
`185ad20abe5d249894417df6b9785222da0b2a73`. Le 10 octobre 2026,
l'utilisateur a retiré l'exigence de compatibilité avec les anciennes versions.
Les nouvelles fonctionnalités restantes sont dans [WORKFLOW_0_3.md](WORKFLOW_0_3.md).

## Sauvegardes et concurrence

Un callback host pendant TEMP RELOAD pouvait sauvegarder gain restauré,
pitch ancien et séquence restaurée. Les opérations groupées publient maintenant
une cible musicale complète pendant la livraison des paramètres. La sauvegarde
utilise cette cible sans attendre la fin du callback. Le rappel d'un projet
inclut aussi play, hostSync et master ; TEMP conserve leurs valeurs courantes.

Le premier rendu après un rappel initialise l'arrangement sans réappliquer
le snapshot chargé sur l'état courant. Les changements de paramètres, trigs,
longueurs, slices ou samples reçus après le rappel sont ainsi conservés dès
ce rendu. L'initialisation attend la fin de la publication sans bloquer le
thread audio ; les sélections musicales suivantes gardent leur comportement.
Un Select plus ancien encore en file ne laisse plus ses samples ou sa séquence
dans le projet rappelé. En Song, les lignes sans override suivent le swing
automatisé courant ; un swing explicite de ligne reste prioritaire.

La compression GZIP appartient au processeur, associée aux samples immuables.
Les imports et la démo la préparent une fois ; les blobs d'un état chargé sont
réutilisés. Les sauvegardes suivantes réutilisent le cache, et les entrées
expirées sont purgées hors du rendu. Compression et copie des blobs se font
hors du verrou du cache. Construire le ValueTree et copier l'état restent des
travaux proportionnels à la taille du projet : la sauvegarde n'est pas instantanée.

## Traitement MIDI et audio

Les messages ignorés sont filtrés dans leur vue brute avant de créer un
`MidiMessage` propriétaire. Cela supprime l'allocation/libération constatée
pour un SysEx de 1 024 octets. Les événements simultanés conservent leur ordre.
Le moteur utilise un curseur pour les événements triés et garde une voie
sans allocation pour les appels avec événements non triés.

La FIFO et son stockage étaient déjà alignés, 128/128. Leur capacité n'est
pas modifiée. Le tableau agrégé reste borné à 512 événements ; les abandons
sont comptés. En saturation, une release prend la place d'une note-on, ou
d'une release redondante si nécessaire, pour éviter une voix tenue. Les compteurs
sont consultables dans l'infobulle VST TOOLS.

Le DSP cache les constantes pitch/filtre, pan, drive, bitcrusher, EQ, comb,
BASE/WIDTH, SRR et chorus, puis les actualise quand leurs contrôles changent.
Les paramètres identiques ne sont plus réappliqués systématiquement à chaque
bloc. Les enveloppes AHD/ADSR évitent le calcul Legacy inutilisé ; le triangle
LFO utilise sa formule directe.

Le master utilise une approximation rationnelle avec clamp avant multiplication,
bornée à ±1. Son erreur absolue face à `tanh` reste sous 0,024 dans le test
numérique. Ce changement modifie légèrement le rendu sonore ; la parité
avec les anciennes versions n'est plus un critère.

## Import et interface

Décodage et compression des imports WAV/AIFF/FLAC se font sur un worker.
La destination pattern/piste est mémorisée ; une demande plus récente sur la
même piste remplace l'ancienne. Annuler empêche immédiatement la publication,
sans interrompre une lecture ou compression déjà commencée. La destruction
du processeur attend la fin du worker hors du traitement audio.

Les callbacks de l'éditeur utilisent un SafePointer. Un import continue si
l'éditeur est fermé ; un processeur détruit ne reçoit plus sa publication.
Le bouton IMPORT devient CANCEL pendant le travail. Une lecture cohérente
par tick regroupe les données de l'interface ; seuls les pads, l'OLED,
l'horloge, la page jouée, le niveau et la waveform modifiés sont invalidés.

Control All et la prévisualisation de destination LFO sont rattachés à leur
pattern/kit d'origine. Après une transition automatique, leur annulation
n'écrit plus les valeurs de l'ancien kit dans le nouveau.

## Benchmark du moteur

Comparaison avec la 0.3 précédente, moteur isolé, même corpus synthétique :
16 pistes stéréo, 48 kHz, blocs de 256, 300 blocs après un bloc de chauffe,
soit 76 800 frames et 1,6 seconde de son. Médiane de cinq essais ; GCC 14.2,
`-O3 -DNDEBUG -std=c++17`, sans fast-math ni activation explicite de FTZ/DAZ.
Les deux variantes réappliquent les paramètres des seize pistes à chaque bloc.

| Charge | Avant | Après |
| --- | ---: | ---: |
| Drive + BR Legacy | 89,7 ms | 75,8 ms |
| ADSR + EQ + BASE/WIDTH + SRR | 382,5 ms | 150,9 ms |
| Même configuration avec 48 LFO | 491,4 ms | 280,1 ms |
| MIDI trié, 512 événements/bloc | 394,3 ms | 148,2 ms |

Ce sont des temps de calcul cloud du moteur, pas des mesures de CPU dans Live.
Le corpus reproductible est [engine_benchmark.cpp](../tests/engine_benchmark.cpp).
Pour mesurer la version courante sous Linux :

```bash
g++ -O3 -DNDEBUG -std=c++17 -Isrc/engine tests/engine_benchmark.cpp \
  src/engine/Engine.cpp src/engine/AmpFilter.cpp \
  src/engine/SequencerRules.cpp src/engine/PatternChain.cpp -o /tmp/takt-benchmark
/tmp/takt-benchmark
```

La compilation, les tests de régression et le bundle réel sont détaillés dans
[VALIDATION.md](VALIDATION.md). Ils ne constituent pas un essai dans Ableton Live.
