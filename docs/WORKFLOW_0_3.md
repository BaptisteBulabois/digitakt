# Takt II 0.3.2 — branche de développement

Cette branche conserve `main` et la version stable 0.2.0. Elle étend le panneau
fourni par l'utilisateur et les workflows décrits dans le manuel Digitakt II
OS 1.17. La validation de cette branche est consignée dans [VALIDATION.md](VALIDATION.md).
Les algorithmes audio restent des implémentations indépendantes.

L'interface 0.3.2 suit le panneau officiel : noms et valeurs dans l'écran,
lettres A–H près des codeurs et voyants de page en deux rangées au-dessus
de PAGE. Glisser un codeur pour régler sa valeur ; celle-ci apparaît aussi
au survol. Les outils conservent leurs champs numériques. Les panneaux
d'outils et d'arrangement masquent les commandes qu'ils recouvrent ; NO
les ferme. Les détails sont dans [UI_REWORK_0_3_2.md](UI_REWORK_0_3_2.md).

## Préserver l'installation stable

Fermer Live et conserver une copie du dossier complet `Takt II.vst3` 0.2.0
avant d'essayer le nouveau bundle. Le nouveau plugin garde la même identité et
les mêmes 265 premiers paramètres : il remplace l'instance existante.
Les nouveaux paramètres sont ajoutés après ceux de la première version.

Le 10 octobre 2026, l'utilisateur a retiré l'exigence de compatibilité avec
les anciennes versions. Les validations portent sur la sauvegarde et le
rappel de la version actuelle. Les lecteurs historiques restent présents,
mais la parité audio avec un ancien binaire n'est plus imposée. Le format
`TAKTII_STATE_2` contient machines, points Slice, modulation, enveloppes,
effets et arrangements.

## Machines et samples

Sélectionner une piste, importer un sample, puis utiliser **FUNC → SRC** pour
choisir la machine. SRC dispose aussi d'une vue avec la forme d'onde.

Les imports WAV/AIFF/FLAC et le glisser-déposer décodent et préparent les
samples en arrière-plan. Les commandes restent accessibles ; **CANCEL IMPORT**
annule la publication du fichier en cours pour la piste. Le pattern et la
piste de destination sont mémorisés à l'ouverture du sélecteur, même si l'on
navigue ensuite. Un nouvel import sur la même piste remplace la demande
précédente. Fermer l'éditeur laisse l'import continuer dans le processeur.

| Machine | Contrôles et comportement |
| --- | --- |
| Legacy | Lecture historique, START/END absolus, inversion et boucle ; conservée pour les anciens projets |
| Oneshot | Début et longueur relative, point de boucle indépendant, lecture avant/arrière avec ou sans boucle |
| Werp | Fragments synchronisés au tempo, taille SEG, direction des fragments et durée BARS |
| Stretch | Lecture granulaire dont BARS règle la durée musicale et TUNE la hauteur |
| Repitch | Vitesse et hauteur suivent le tempo ; pas de TUNE indépendant sur cette page |
| Grid | Tranches égales, nombre GRID, première SLICE et nombre de tranches LEN |
| Slice | Points de début, fin et boucle éditables, sélection de tranche et LEN |

La durée BARS décrit le sample entier, en mesures de quatre noires. La lecture
granulaire et les fragments ne reproduisent pas les algorithmes Elektron.
Les positions sont affichées en valeurs normalisées et plusieurs niveaux en
pourcentages ; les anciens paramètres gardent leurs plages d'automation.

Avec Slice ou Grid, **SRC puis YES** ouvre les opérations sur les tranches.
Les allocations de locks linéaires ou aléatoires visent les note trigs existants
et ne créent pas de nouvelles notes.

L'éditeur Slice utilise **A** pour le lien entre points adjacents, **D** pour la
boucle, **E/H** pour début/fin, **F** pour le zoom et **G** pour la position.
FUNC permet l'accrochage aux passages par zéro, le zoom vertical ou le
déplacement simultané de début/fin. Les flèches choisissent une tranche.
Les points appartiennent à la piste du pattern, pas au fichier audio partagé.
La convention logicielle pour SLICE=NOTE commence à la note MIDI 24 ; elle
est distincte des notes MIDI 36–51 qui déclenchent les seize pistes du VST.

## Modulation, amplitude et filtres

**MOD** comporte trois pages, une par LFO, avec SPD, MULT, FADE, DEST, WAVE,
SPH/SLEW, MODE et DEP. MULT peut suivre le tempo ou un tempo fixe de 120 BPM.
DEP à zéro désactive la modulation. FADE positif atténue progressivement ;
FADE négatif fait apparaître la modulation. YES confirme une destination
préécoutée ; NO revient à la destination précédente.

Les modes FRE, TRG, HLD, ONE et HLF correspondent respectivement à une phase
continue, un redémarrage au trig, une valeur tenue, un cycle et un demi-cycle.
Les destinations proposées sont celles réellement traitées par le moteur.
Les courbes de fade et de slew sont indépendantes ; les destinations de LFO
vers d'autres LFO et le key tracking à quatre destinations restent à compléter.

**AMP** distingue l'enveloppe historique, AHD et ADSR. HOLD fixe et HOLD=NOTE
ne traitent pas les note-off de la même façon. VOL est distinct du LEVEL de la
piste et du volume principal. Les durées utilisent les unités du VST ; elles
ne prétendent pas reproduire une courbe de temps propriétaire à valeurs 0–127.
Avec les nouvelles enveloppes, AUDITION et la préécoute d'une tranche utilisent
une porte de 0,25 noire. Les notes MIDI transmettent leurs note-off aux
enveloppes ADSR ou HOLD=NOTE.

**FUNC → FLTR** choisit le filtre. Les machines proposées sont Prototype,
Multimode, Lowpass 4, EQ, Comb−, Comb+ et Legacy. Leurs contrôles changent avec
la machine. La seconde page contient délai d'enveloppe, key tracking,
base/width, routage et reset. FLT.T règle le déclenchement de l'enveloppe au trig.

**FX** ajoute réduction de fréquence d'échantillonnage, routages pré/post,
réduction de résolution et envoi chorus. **FUNC → FX** ou le bouton SEND FX
donne accès aux pages delay, reverb et chorus. Les contrôles non implémentés
restent désactivés.

## Trigs, conditions et Control All

REC active l'édition GRID. Un clic ajoute ou retire une note ; un clic droit
sélectionne sans basculer. FUNC avec un pad permet un lock trig, qui modifie
les locks disponibles sans déclencher un nouveau sample. Retirer puis recréer
une note efface ses anciens locks.

TRIG expose note, vélocité, longueur, PROB, LFO.T, FLT.T, FILL et COND.
Les conditions comprennent A:B, PRE, NEI, 1ST, LST et leurs inverses. NEI
lit la piste précédente. LST dépend d'un changement de pattern annoncé.
Le bouton FILL adapte le geste matériel sous forme d'un état verrouillé.
PROB et la condition de probabilité utilisent actuellement la même valeur ;
deux probabilités indépendantes restent à compléter.

La sous-page retrig expose RTRG, VFAD, LEN et RATE. LEN règle ici la durée
de la courbe de vélocité ; la longueur de note détermine la durée du train de
retriggers. Un nouveau note trig accepté remplace le train précédent sur cette
piste monophonique. Les longueurs finies sont bornées à 512 noires ; INF reste
à développer. Les sept vitesses de piste sont disponibles dans VST TOOLS.

Les anciens pas conservent leur probabilité déterministe et leurs répétitions
réparties dans un pas. STEP TOOLS permet leur édition. L'activation des nouvelles
règles est explicite ; la probabilité avancée est évaluée par activation et
renouvelée au redémarrage du transport.

TRK avec un encodeur ouvre un geste Control All. Les valeurs évoluent à partir
de celles mémorisées au début du geste ; NO annule, recliquer sur TRK valide. La
piste active est toujours incluse. Le choix d'un déplacement relatif est une
adaptation logicielle : le manuel ne précise pas sa formule pour des pistes
dont les valeurs initiales diffèrent.
Le processeur prend en charge un masque d'inclusion ; son panneau de
configuration n'est pas encore exposé dans l'interface.

## Patterns et songs

Le projet dispose de huit banques A–H de seize patterns. PTN ouvre la sélection
et les pads choisissent un emplacement. Une sélection pendant la lecture est
différée à une frontière de pattern ; le numéro en attente doit rester visible.
Les chaînes peuvent contenir jusqu'à 64 entrées.
La mémoire TEMP concerne le pattern actif et est réinitialisée lorsqu'on
change de pattern ; une mémoire TEMP indépendante pour chaque emplacement
reste à développer.

SONG permet de sélectionner une des seize songs. Chaque song contient jusqu'à
99 lignes, avec pattern, répétitions, longueur, tempo, swing et mutes. END
détermine LOOP ou STOP. HOST SYNC conserve le tempo et le transport de Live :
une fin de song arrête l'arrangement du plugin, pas le DAW.
Choisir une song pour l'éditer puis utiliser PLAY SONG constitue l'adaptation
à la souris de ce lot ; les opérations de presse-papiers sur lignes restent
à compléter.

PERFORM KIT conserve les sons et leurs modifications lors des changements
de pattern. La séquence change indépendamment du kit. Les commandes de
sauvegarde/rechargement du kit sont explicites. Les samples partagés entre
patterns sont conservés dans le projet du VST.

## Ressources et travail restant

La documentation disponible suffit pour développer sans machine physique.
Des boucles avec tempo et nombre de mesures connus, un breakbeat et une note
tenue seraient utiles aux essais, ainsi qu'un projet Live avec automation et
sauvegarde/réouverture. Le compte rendu de ton essai dans Live est prioritaire.

Les modes d'enregistrement LIVE/STEP, les locks généralisés et le choix de
sample par pas, le browser de samples/presets/kits, les destinations de
modulation supplémentaires et les règles complètes CHANGE/RESET restent à
compléter, ainsi que le compresseur, les contrôles manquants des pages SEND FX
et le presse-papiers des arrangements. Sampling dans le DAW, sorties séparées et MIDI externe sont des
extensions dont l'usage doit être défini. Overbridge, Outbox et la maintenance
d'un appareil réel restent hors périmètre.

Les références et les ambiguïtés sont dans
[NEXT_LOT_RESEARCH.md](reference/NEXT_LOT_RESEARCH.md). YouTube a refusé l'accès
dans cet environnement ; seuls les titres et liens des vidéos intégrées à la
page officielle ont été retrouvés, sans visionnage revendiqué.
