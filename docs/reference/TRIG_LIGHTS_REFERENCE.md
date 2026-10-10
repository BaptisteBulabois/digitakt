# Référence des lumières TRIG et PAGE — Digitakt II OS 1.17

Audit du 10 octobre 2026 pour la révision du retour visuel du séquenceur.
Sources effectivement lues : [manuel officiel OS 1.17](https://www.elektron.se/wp-content/uploads/2026/10/Digitakt-2-User-Manual_ENG_OS1.17_260930.pdf)
et [notes officielles](https://www.elektron.se/release-notes/digitakt-ii-os-release-notes),
copies locales et empreintes décrites dans [SOURCES.md](SOURCES.md).
Les pages citées sont les pages imprimées. Ce document distingue le contrat
documentaire du comportement livré ; il ne certifie pas une conformité complète.

## Règles documentées

| Contexte | Retour visuel et action | Source |
| --- | --- | --- |
| GRID, pas vide | Touche TRIG éteinte. Un appui court ajoute un note trig. | §10.2.1 p.41 ; §10.2.2 p.42 |
| GRID, note trig | Touche rouge. Un appui court retire le trig ; un maintien prépare son édition. | §10.2.1 p.41 ; §10.2.2 p.42 |
| GRID, lock trig | Touche jaune ; FUNC + TRIG l'ajoute. Le lock trig applique des paramètres sans déclencher de note. | §10.2.1 p.41 ; §10.2.2 p.42 |
| Trig avec parameter locks | Clignotement rouge pour un note trig, jaune pour un lock trig. | §3.1 p.13 ; §10.8.1 p.47 |
| Lecture du pattern / LIVE | Une lumière parcourt les seize touches à la vitesse du séquenceur, sur les pages successives, jusqu'à huit. La course n'est pas limitée au mode GRID. | §3.1 p.13 |
| Sélection de piste TRK + TRIG | La piste sélectionnée pour édition est indiquée en rouge. Une seule piste active à la fois. | §8.3 p.25 ; §10.2.2 p.42 |
| PAGE en GRID | La LED entièrement allumée indique la page d'édition ; PAGE change cette page si le pattern dépasse seize pas. | §10.2.2 p.42 |
| PAGE en lecture | Les LED indiquent le nombre de pages du pattern et la page active ; la LED de la page jouée clignote. | §3.1 p.13 ; §10.1.2 p.40 |
| MUTE GLOBAL | FUNC + TRK ouvre/ferme le mode. TRIG bascule le mute de sa piste ; allumée verte = non muette, éteinte = muette. Une piste muette avec des trigs clignote verte pendant la lecture. | §8.5.3 p.26 |
| MUTE PATTERN | FUNC + double TRK ouvre ce mode. Même principe en magenta, mais mute propre au pattern. Une piste muette aux deux niveaux clignote magenta dans les deux modes. | §8.5.3 p.27 |
| Mutes préparés | Dans MUTE, maintenir FUNC et appuyer les TRIG prépare les changements ; ils sont appliqués au relâchement de FUNC. Rouge = à rendre muette, orange = à réactiver. | §8.5.3 p.27 ; notes 1.00 → 1.01, Improvements |
| Sélection de pattern | Positions contenant des données blanches, pattern actif rouge, positions vides éteintes. Le nom du prochain pattern clignote à l'écran jusqu'au changement en fin du précédent. | §10.1.1 p.40 |
| PAGE maintenue, GRID | TRIG 1–8 choisissent la page d'édition ; TRIG 9–16 choisissent les pages à boucler en vert. Rangée haute : page de lecture maître clignotante ; rangée basse : page de boucle jouée clignotante. PAGE + NO rétablit toutes les pages. | §10.2.2 p.42 ; notes 1.01A → 1.02, Improvements |

Hors enregistrement, les actions des TRIG dépendent aussi du réglage
TRK SELECT : NORMAL sélectionne et joue, SILENT sélectionne, MANUAL joue,
INVERTED sélectionne et réserve TRK + TRIG au jeu. NORMAL, SILENT et MANUAL
sélectionnent silencieusement via TRK + TRIG (§14.7.9 p.82).

## Limites de la preuve et adaptation au VST

Le manuel ne précise ni la teinte exacte de la lumière qui parcourt les pas,
ni les fréquences/durées des clignotements, ni la luminosité relative des
différentes LED PAGE. Un curseur blanc dans le numéro, des couleurs rouges,
jaunes et vertes lisibles, et une animation liée à l'horloge sont des choix
visuels du plugin. Une vidéo nette peut préciser ces détails ; elle n'est pas
nécessaire pour appliquer les règles ci-dessus. Le manuel ne tranche pas
explicitement le suivi automatique de la page d'édition hors GRID : conserver
sa sélection en GRID et suivre la page jouée pour le curseur hors GRID est une
adaptation raisonnable, à ne pas annoncer comme mesurée sur la machine.

À l'entrée de cette révision (0.3.2), `StepPad` dessine une barre ambre sous
le numéro joué, `refreshSteps()` n'allume le curseur qu'en GRID, les locks sont
affichés par un texte « LOCK » fixe, et FUNC + TRK ouvre les outils logiciels.
Ces écarts motivent la révision ; les fonctions MUTE PATTERN, mutes préparés
et boucles de pages exigent un état musical dédié, pas seulement de nouvelles
couleurs. La présence d'une LED ne doit pas faire croire que ces fonctions
sont déjà implémentées.

## Révision 0.3.3

Les numéros portent désormais le retour lumineux : rouge pour note trig,
jaune pour lock trig, blanc pour le curseur, sombre pour un pas vide. Les
locks de pitch/cutoff/slice clignotent ; le curseur court également hors GRID.
La page d'édition reste fixe tandis que la LED de lecture alterne à chaque
pas de la piste. TRK affiche la piste active en rouge. Les auditions et notes
MIDI signalent leur activité réelle par un flash rouge de 100 ms, avec un
compteur conservant les événements plus courts qu'un rafraîchissement UI.

FUNC + TRK ouvre un panneau de mutes via les seize numéros, vert = non muet.
Ce geste utilise le mute de piste existant, sauvegardé avec le pattern ; il
ne crée pas les deux couches GLOBAL/PATTERN de la machine. FUNC + pad hors
GRID permet un quick mute. Les mutes préparés et boucles de pages ne sont
pas ajoutés par cette révision. Le clignotement des locks toutes les 180 ms
et le curseur blanc restent explicitement des choix visuels du VST.

Captures du plugin : [séquence en lecture](../images/takt-ii-0.3.3-sequencer.png)
et [mode MUTE](../images/takt-ii-0.3.3-mute.png).
