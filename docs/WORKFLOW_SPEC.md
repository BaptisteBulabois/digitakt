# Spécification de workflow — cible Digitakt II OS 1.17

Analyse du 8 octobre 2026, fondée sur les [sources effectivement lues](reference/SOURCES.md).
Ce document décrit le travail à réaliser, pas une nouvelle version du VST3.
La base installée dans Live reste le commit `751ebcb` ; les audits comparent
le code disponible au commit `87a5e84`.

## Contrat du produit

Instrument Windows x64 VST3, 16 pistes audio, destiné à Ableton Live. Priorité
aux interactions, à l'organisation des données et aux règles musicales de la
machine. Les samples viennent de l'utilisateur et le DSP peut être indépendant.
Overbridge, Outbox et la maintenance matérielle sont exclus.

Les sections/pages citées ci-dessous désignent le manuel OS 1.17. Les précisions
et les écarts par rapport au code sont dans les trois audits :
[interface](reference/UI_WORKFLOW_AUDIT.md),
[séquenceur](reference/SEQUENCER_WORKFLOW_AUDIT.md) et
[firmware](reference/FIRMWARE_AUDIT.md).

## Modèle de données à établir

| Objet documenté | Contrat pour la réimplémentation | Référence |
| --- | --- | --- |
| Projet | Contient 8 banques de 16 patterns, pools de presets/samples et songs ; distinguer ces pools de la bibliothèque locale partagée qui transpose +Drive. Le projet Live conserve les données et samples nécessaires au rappel | §§5.1–5.2, p.16–17 ; §9.1 ; adaptation DAW |
| Pattern | Données du séquenceur, TRIG, kit, tempo et réglages de longueur/swing appartiennent au pattern | §5.2, p.16–17 |
| Kit | 16 presets de piste et réglages du kit ; charger une référence crée une copie dans le pattern, éditable indépendamment | §5.2, p.16–17 ; §9.8 |
| Preset | Sample et paramètres SRC/FLTR/AMP/FX/MOD ; les réglages TRIG appartiennent au pattern | §5.2 ; introduction du chapitre 11, p.53 |
| Trig | Distinguer note trig, lock trig et pas vide ; les locks ne doivent pas modifier le preset de base | §10.2.1, p.41 ; §10.8.1, p.47 |
| Sample | Audio référencé par les presets et les locks, y compris après déplacement du fichier original ; les points Slice appartiennent au preset, pas au fichier partagé | Chapitres 9 et 13 ; §A.2.5, p.99 ; adaptation du rappel Live |

Les limites physiques de stockage ne sont pas des obligations du plugin. La
compatibilité avec les fichiers propriétaires Elektron n'est pas déduite de ce
modèle et ne fait pas partie de cette étape.

## Ordre d'implémentation

### 1. Navigation et surface de contrôle

Remplacer la présentation simultanée des paramètres par les familles
**TRIG → SRC → FLTR → AMP → FX → MOD**, un écran avec huit positions de
paramètres **A–H**, et une commande **LEVEL** distincte. La page, la piste,
le pattern et le mode doivent toujours être identifiables. YES valide et NO
revient au niveau précédent (§3.1, p.12–13 ; §6, p.18–21 ; §8.3, p.25).

Établir un état de navigation explicite : famille/sous-page, piste, page de
pas, mode des touches TRIG et mode d'enregistrement. Les boutons d'une même
zone ont des actions différentes en lecture, GRID, clavier ou slicing ; leur
état et leur aide doivent expliquer l'action courante. Les pages qui demandent
un comportement absent du moteur doivent être implémentées avec ce comportement
avant d'être proposées comme utilisables.

**Adaptation proposée** : FUNC, TRK et PAGE accessibles à la souris avec état
visuel ; raccourcis documentés pour les maintenir ; clic sur une position A–H
pour éditer, action contextuelle pour réinitialiser. La disposition et les effets
des gestes viennent du manuel ; le choix exact des raccourcis vient du VST.

Validation attendue : sélectionner la piste 9, passer de SRC à AMP puis revenir,
changer de sous-page, revenir d'un menu avec NO ; conserver la bonne piste et
les valeurs. Vérifier que le mapping A–H affiché correspond à la machine choisie.

### 2. Édition et réversibilité

Introduire un presse-papiers partagé avec type de contenu ; copier un nouvel
objet remplace la copie précédente. Implémenter d'abord les pas, pages et pistes,
puis les patterns une fois le modèle de banques prêt. Le contexte décide de
l'objet copié/collé/effacé ; répéter une opération de collage ou d'effacement
permet son annulation (§6.4, p.19 ; §10.8.5, p.49).

Ajouter sauvegarde temporaire et restauration de pattern avec repli vers l'état
sauvegardé en l'absence de copie temporaire. Distinguer cette opération du rappel
permanent du projet Live (§10.8.6, p.49–50). Control All applique les changements
aux pistes audio autorisées et à la piste active ; NO avant relâchement annule
le geste (§6.2.2, p.19 ; §9.8.1, p.37).

Validation attendue : copier une page avec plusieurs locks, coller ailleurs,
annuler ; vérifier contenu et espacements. Modifier plusieurs pistes avec
Control All puis annuler et retrouver les valeurs initiales. Sauver temporairement,
modifier puis restaurer sans changer le fichier Live.

### 3. Séquenceur et modes d'enregistrement

Implémenter GRID, LIVE et STEP comme des états distincts. Établir les règles de
placement/remplacement des trigs, overdub et enregistrement des paramètres
(§10.2, p.41–44 ; §14.7.7–8, p.82). Retirer puis replacer un note trig en GRID
efface ses locks ; remplacer une note en STEP a des règles différentes : ne pas
appliquer un simple basculement universel.

Étendre les locks aux paramètres musicaux concernés et au choix de sample,
avec note trigs et lock trigs. Une édition en masse vise les trigs actifs de la
page ou de la piste et ne crée pas de note sur les pas vides (§10.8.1, p.47).

Remplacer les simplifications actuelles par des définitions documentées :

- PROB évalué à chaque activation effective du trig, conditions A:B, PRE/NEI,
  1ST/LST et inverses, FILL distinct (rubrique « TRIG CONDITIONS », imprimée
  §10.7.3, p.48 ; §10.8.4, p.49 ; §11.2, p.53).
- Retrig avec RTRG/RATE/LEN/VFAD, plutôt qu'un nombre de répétitions réparties
  dans un seul pas (§11.3, p.54).
- Swing exprimé en ratio : 50 % neutre jusqu'à 80 % ; le contrôle actuel 0–75 %
  n'a pas cette sémantique (§7.3.1, p.23).
- Longueur PER PATTERN/PER TRACK, vitesses, CHANGE/RESET et copie de pages
  lors de l'allongement (§10.7, p.45–47).

Validation attendue : lock sans note, retour aux valeurs de base au trig suivant,
probabilité sur plusieurs boucles du host, conditions voisines et FILL, retrig
aux différents tempos, changement de pattern avec pistes de longueurs différentes.
Les tests existants du prototype doivent être complétés ou corrigés lorsqu'ils
figent une règle différente du manuel. Les limites de microtiming qui ne sont
pas établies par l'extraction texte restent à confirmer visuellement.

### 4. Samples, presets et machines

Construire un browser local navigable avec préécoute, recherche et rappel du
dossier, puis le chargement indépendant de samples, presets et kits (chapitres
9 et 13). Transposer +Drive en bibliothèque locale ; conserver les mêmes
effets musicaux sans simuler les fonctions Transfer.

Commencer par Oneshot : distinguer début, longueur et point de boucle, et les
quatre modes de lecture. Le contrôle actuel « end » est une position absolue,
pas le paramètre LEN documenté (annexe A.2.1, p.93–94). Ajouter ensuite les autres
machines et leurs pages ; **Slice** est distinct de Grid et stocke des points
éditables avec le preset (annexe A.2.5, p.97–100 ; notes 1.15/1.15A).

Compléter l'organisation AMP, les filtres, LFO et pages FX avec leurs états,
destinations et plages (§11 ; annexe A). La correspondance des commandes est
nécessaire ; une identité d'algorithme audio avec la machine ne l'est pas.

Validation attendue : importer un sample, éditer boucle/slices, sauvegarder puis
rappeler ; un sample lock ne remplace pas le sample de base. Une slice vide et
une bibliothèque vide restent éditables. Déplacer le fichier d'origine ne casse
pas le rappel Live.

### 5. Banques et performance

Une fois le modèle de patterns opérationnel, ajouter les 128 patterns, chaînes,
Song Mode, échange de pistes et mutes de performance. Le rappel doit conserver
les kits propres aux patterns et les états de song (§5.2 ; §10.1.3 ; §10.9 ;
§9.8.1 et notes 1.10/1.10A).

Respecter l'exception **PERFORM KIT** : ce mode conserve le kit courant et ses
modifications lors des changements de pattern et suspend l'autosauvegarde
concernée. Changer de pattern ne doit donc pas systématiquement charger son
kit dans tous les modes (§10.10, p.52).

**Adaptation DAW** : lorsque Host est actif, Live fournit transport et tempo.
Documenter comment les commandes locales de démarrage/changement de pattern
s'appliquent à cette position ; ne pas modifier silencieusement le tempo du DAW
pour émuler le mode autonome. Conserver le mode interne déjà utilisable.

Validation attendue : changement de pattern pendant la lecture, chaîne démarrée
en cours de transport et première ligne de song jouée ; sauvegarde et rappel
d'un projet avec plusieurs patterns et samples partagés.

## Compatibilité et critères de livraison

- Préserver identifiant VST3 et identifiants des paramètres publiés. Les huit
  encodeurs sont une présentation des paramètres de la piste courante, pas
  huit nouveaux paramètres remplaçant toute l'automation existante.
- Versionner le format d'état et lire `TAKTII_STATE_1`. Les anciens projets,
  samples, locks et réglages doivent rester récupérables. Toute modification
  de la sémantique d'un paramètre, notamment swing ou end/LEN, exige une
  stratégie explicite de migration et de compatibilité d'automation.
- Séparer les états d'édition des snapshots consommés par l'audio ; ne pas
  allouer ni charger un fichier sur le thread de rendu pour naviguer dans les
  pages, changer de pattern ou préécouter un sample.
- Vérifier les changements de code avec `bash scripts/build.sh`, les tests
  existants pertinents et les scénarios ci-dessus ; produire ensuite le VST3
  Windows via CI. L'installation validée par l'utilisateur ne vaut pas
  validation de toutes les interactions musicales.
- Pour chaque livraison, préciser ce qui est utilisable, ce qui est adapté
  au DAW et ce qui reste à implémenter. Les audits sont de la documentation,
  pas des tests déjà passés ni une preuve de fidélité complète.

Le premier lot de développement est la navigation de la section 1, avec les
paramètres déjà fonctionnels et les fondations nécessaires aux modes. Les
sections suivantes dépendent de ce socle ; elles ne constituent pas une promesse
de reproduction complète en une seule livraison.

## État du lot 0.2.0

La navigation à huit commandes, les sous-pages et le panneau inspiré du SVG
fourni sont implémentés. REC distingue l'édition GRID du jeu des pistes ; TRK
sélectionne silencieusement. Les commandes FUNC et les actions explicites
permettent copie/collage/effacement de pas, page et séquence de piste, annulation
et sauvegarde/restauration temporaire.

Les banques/patterns multiples, les modes LIVE/STEP, les locks généralisés,
les machines avancées et les règles complètes du séquenceur restent à développer.
La présentation des paramètres ne modifie pas leurs plages ou leur DSP legacy.
Le STOP local conserve sa fonction de pause, avec les queues audio ; il n'offre
pas encore la coupure/double STOP du matériel. Le
[guide 0.2](WORKFLOW_0_2.md) décrit les gestes disponibles et les adaptations.
