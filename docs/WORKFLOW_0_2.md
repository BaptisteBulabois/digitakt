# Utiliser Takt II 0.2.0 dans Ableton Live

Cette version réorganise le sampler/séquenceur à 16 pistes autour de pages et
d'opérations d'édition. Elle conserve les samples et paramètres de la première
version. Pour installer ou remplacer le bundle Windows, suivre
[INSTALL_WINDOWS.md](INSTALL_WINDOWS.md).

## Jouer et importer ses samples

Charger Takt II sur une piste MIDI. Avec **HOST SYNC** actif, lancer le transport
de Live ; le tempo vient du host. Avec HOST SYNC désactivé, **PLAY** démarre le
séquenceur au tempo interne, puis permet pause/reprise. **STOP** suspend cette
horloge sans revenir au début ni couper les samples/effets en cours : c'est une
adaptation de ce lot, pas encore le comportement complet du STOP matériel.
Les notes MIDI 36–51 déclenchent les pistes 1–16.

Choisir la piste à modifier avec le sélecteur de piste, puis utiliser
**IMPORT SAMPLE** ou déposer un fichier audio sur la forme d'onde. **AUDITION**
écoute la piste sélectionnée. Les samples importés sont conservés dans l'état
du plugin enregistré par Live.

## Pages et pas

Les six familles **TRIG**, **SRC**, **FLTR**, **AMP**, **FX** et **MOD** organisent
l'édition. Les positions **A–H** correspondent aux paramètres de la page
courante ; **LEVEL / DATA** reste une commande séparée du **MAIN VOLUME**.
Cliquer à nouveau sur une famille, ou utiliser les flèches haut/bas, change sa
sous-page. La deuxième vue SRC affiche la forme d'onde. Les emplacements sans
fonction implémentée sont désactivés ; un astérisque signale un contrôle adapté
dont le détail est expliqué par son infobulle.

Cliquer **TRK**, puis un pad, sélectionne une piste sans la jouer et désactive
TRK. La barre VST offre aussi seize sélecteurs silencieux ; un clic droit dessus
change le mute. **REC** actif : les pads éditent les pas ; un clic droit sélectionne
un pas sans le basculer. REC désactivé : les pads jouent les pistes ; un clic
droit sélectionne la piste silencieusement.

Les huit pages de séquenceur affichent chacune 16 pas. La sélection de page
concerne l'affichage et l'édition ; la longueur du pattern détermine quels pas
sont joués.

**VST TOOLS** ouvre les réglages de tempo interne, swing, longueur, portée du
presse-papiers et les commandes complémentaires. **STEP TOOLS** expose les
anciens locks pitch/cutoff, conditions de cycle, nombre de répétitions et
microtiming. **SEND FX** expose les effets globaux disponibles. Ces outils
préservent le fonctionnement du premier VST et ne remplacent pas les fonctions
complètes du Digitakt II. **NO** revient à la page de paramètres.

## Copier, coller et effacer

Un seul presse-papiers est partagé entre les portées. Une nouvelle copie remplace
la précédente. Copier et coller dans une portée correspondante.

Choisir la portée dans VST TOOLS. Les actions sont accessibles dans ce tiroir
ou avec **FUNC** : cliquer FUNC, puis la seconde touche ; FUNC se désactive
après la commande.

| Touches | Action |
| --- | --- |
| FUNC puis REC | COPY |
| FUNC puis STOP | PASTE |
| FUNC puis PLAY | CLEAR selon la portée |
| FUNC puis YES | TEMP SAVE |
| FUNC puis NO | TEMP RELOAD |
| FUNC puis FX | Ouvrir SEND FX |

| Portée | COPY / PASTE | CLEAR |
| --- | --- | --- |
| Pas | Pas complet et ses locks | Retire les locks de pitch et de filtre, remet le pitch du pas à zéro ; conserve son déclenchement et ses autres valeurs |
| Page | Les 16 pas de la page, avec leurs locks | Remet les 16 pas à leur état vide ; conserve la longueur de piste |
| Séquence de piste | Les 128 pas, leurs locks et la longueur de piste | Vide les pas ; conserve la longueur de piste |

La copie de séquence ne copie ni le sample ni les paramètres sonores de la piste.
Ainsi, une séquence collée sur une autre piste joue le sample déjà chargé sur
cette piste.

Répéter **PASTE** ou **CLEAR** sur la même cible annule cette opération. Une
nouvelle édition ou copie remplace ce point d'annulation. Ce mécanisme est
limité à la dernière opération concernée ; il ne constitue pas un historique
général des gestes.

## Sauvegarde temporaire et restauration

**TEMP SAVE** mémorise les pas, les longueurs, les samples, les paramètres de
pistes, le tempo interne, le swing et les effets. **TEMP RELOAD** restaure ce
point. Le volume MASTER, PLAY, HOST SYNC et la navigation restent indépendants.

Sans TEMP SAVE, TEMP RELOAD revient à l'état initial de l'instance, ou au dernier
état valide chargé depuis Live. Sauvegarder le projet Live ne déplace pas ce point
de repli. Utiliser TEMP SAVE pour choisir explicitement l'état à retrouver pendant
une session.

Le presse-papiers, le point d'annulation et la copie temporaire sont propres à
la session. Pour conserver le résultat musical après fermeture, sauvegarder
le projet Live ; ces outils d'édition ne sont pas enregistrés avec lui.

## Clavier et aide

Quand l'éditeur a le focus : `Ctrl+C`, `Ctrl+V`, `Ctrl+Z` copient, collent et
annulent ; `Suppr` efface la portée courante. `1–8` et `Q W E R T Y U I`
actionnent les seize pads ; `Maj` permet une sélection silencieuse. Les flèches
gauche/droite changent la page de séquenceur, haut/bas la sous-page de paramètres.
`Espace` commande la lecture interne ; `Échap` revient en arrière. Les raccourcis
sont suspendus pendant une saisie de texte. Le bouton **?** affiche l'aide.

## Références et limites du lot

L'organisation des commandes s'appuie sur le manuel Digitakt II OS 1.17,
§§3.1/6.2/8.3 et chapitre 11. Les opérations d'édition proviennent des
§§6.4 et 10.8.5 ; la sauvegarde temporaire du §10.8.6. Les boutons, sélecteurs
et raccourcis logiciels transposent les gestes physiques à la souris ; le point
de repli temporaire et sa durée de vie décrits ci-dessus sont propres au VST.
Les [sources](reference/SOURCES.md) et la [spécification](WORKFLOW_SPEC.md)
détaillent la cible documentaire.

Les machines SRC avancées, Slice/Grid, les LFO, les modes LIVE/STEP RECORDING,
les banques de patterns, Song Mode et les règles complètes des conditions,
retrigs et longueurs restent à développer. Les contrôles existants conservent
leur comportement de prototype ; cette nouvelle navigation ne les transforme
pas en implémentation complète du manuel.

Overbridge, Outbox, Transfer et la maintenance d'un appareil Elektron sont
hors périmètre. Le moteur audio utilise des traitements indépendants et les
samples de l'utilisateur.
