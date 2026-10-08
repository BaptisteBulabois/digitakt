# Audit de l’interface et du workflow — Digitakt II OS 1.17

Analyse documentaire du 8 octobre 2026. Cible : instrument VST3 Windows x64
dans Ableton Live, seize pistes audio, samples importés par l’utilisateur.
La précision du DSP matériel n’est pas requise. Overbridge, Outbox, USB,
transfert vers une machine et maintenance de firmware sont hors périmètre.

Ce document distingue **documenté** (manuel ou notes officielles), **actuel**
(code et capture du prototype) et **adaptation proposée** (choix logiciel à
implémenter). Il ne certifie pas la fidélité de l’interface actuelle et ne
modifie aucun comportement du plugin.

## Références et méthode

- Manuel officiel *Digitakt II User Manual*, version OS 1.17, 118 pages :
  §3.1 p.12–13 ; §6 p.18–19 ; §8 p.25–28 ; §9 p.29–38 ; §11 p.53–60.
  Compléments : §12.1 p.61, §13.4/13.6 p.70–71, §14.7 p.82 et annexe A
  p.93–108. Les pages citées sont les numéros imprimés, également les numéros
  PDF dans cette édition. Les grilles ci-dessous ont été lues dans les
  figures du PDF ; le texte extrait ne contient pas leurs étiquettes.
- Notes officielles *Digitakt II OS Release Notes*, mises à jour le
  1er octobre 2026, jusqu’à OS 1.17. Les références sont les transitions de
  version, car cette source HTML n’a pas de pagination.
- Versions, liens et empreintes : [SOURCES.md](SOURCES.md).
- Base examinée : HEAD `87a5e84`, fichiers
  [PluginEditor.h](../../src/PluginEditor.h),
  [PluginEditor.cpp](../../src/PluginEditor.cpp) et état sérialisé dans
  [PluginProcessor.cpp](../../src/PluginProcessor.cpp).
  La capture locale `artifacts/editor-final.png` confirme cette présentation.
  La base d’installation à préserver demeure `751ebcb`.

## 1. Organisation de l’écran et des pages

**Documenté.** Six familles de paramètres se succèdent dans cet ordre sur
le panneau : **TRIG → SRC → FLTR → AMP → FX → MOD**. Une seule famille et une
seule piste sont actives. Les huit paramètres affichés correspondent aux
encodeurs A–H : A–D en haut, E–H en bas. Le niveau général de piste
LEVEL/DATA est une commande distincte (§3.1 p.12–13 ; §6 p.18 ; §8.3 p.25).

L’écran principal contient banque/pattern, nom du pattern, mode TRIG,
tempo, type et numéro de piste, niveau de piste, paramètres et indicateur
de sous-page (§6 p.18). La page du séquenceur et la sous-page de paramètres
sont deux notions différentes. Les LEDs distinguent page éditée et page
en cours de lecture (§3.1 p.13).

| Famille | Sous-pages audio documentées | Organisation / accès | Référence |
|---|---|---|---|
| TRIG | 1 : notes/conditions ; 2 : retrig/portamento | Touche TRIG ; nouvel appui ou ↑/↓ pour changer de sous-page | §11.1–11.3, p.53–55 |
| SRC | 1 : paramètres ; 2 : mêmes paramètres avec waveform | Contenu dépendant de la machine ; le menu Slice est un écran distinct | §11.4 p.55 ; annexe A p.93, p.98–101 |
| FLTR | 1 : machine filtre/EQ ; 2 : filtre base-width et options d’enveloppe | Machine choisie pour la page 1 ; page 2 commune aux pistes audio | §11.5–11.6, p.55–56 |
| AMP | Une page, dont les paramètres d’enveloppe dépendent d’AHD/ADSR | Enveloppe, panoramique et volume AMP | §11.7, p.56–57 |
| FX | Une page par piste audio | Réduction de bits, overdrive, réduction de fréquence, routages et trois sends | §11.8, p.57–58 |
| MOD | Trois pages : LFO 1, 2, 3 | Même grille pour chacun des trois LFO audio | §11.9–11.11, p.58–60 |

Les SEND FX globaux du pattern s’ouvrent par FUNC+FX et le MIXER par
FUNC+MOD ; ils constituent d’autres contextes, pas des sous-pages du FX
individuel (§12.1 p.61). REMEMBER SUBPAGE permet de revenir à la dernière
sous-page d’une famille ; U/D KEY MODE peut affecter ↑/↓ aux octaves du
clavier au lieu de la navigation (§14.7.3–14.7.4 p.82).

**Actuel.** Le prototype affiche simultanément transport, seize sélecteurs
de piste, sample, treize paramètres de son, seize pas, huit réglages de pas
et quatre réglages d’effets. Il n’a ni familles de pages, ni écran de huit
paramètres contextuels, ni commandes YES/NO/FUNC. Il possède huit pages
de seize pas ; ce fait ne reproduit pas les sous-pages de paramètres.

### Grille vérifiée des paramètres communs

`—` indique une case graphiquement vide. Les noms sont des identifiants
courts de paramètres, pas une reproduction du texte du manuel.

| Page / vue | A · B · C · D | E · F · G · H | Référence |
|---|---|---|---|
| TRIG 1 | NOTE · VEL · LEN · PROB | LFO.T · FLT.T · FILL · COND | Figure §11.2 p.53 ; descriptions p.53–54 |
| TRIG 2 | RTRG · VFAD · LEN · RATE | — · — · PTIM · PORT | Figure §11.3 p.54 ; descriptions p.54–55 |
| FLTR 2 | DEL · — · — · KEY.T | BASE · WIDTH · BW.RT · RSET | §11.6 p.55–56 |
| AMP, figure en mode ADSR | ATK · DEC · SUS · REL | RSET · MODE · PAN · VOL | Figure §11.7 p.56 ; descriptions p.56–57 |
| FX piste | BR · OVER · SRR · ROUT | DEL · REV · CHR · OD.RT | Figure §11.8 p.57 ; descriptions p.58 |
| MOD 1, 2 ou 3 | SPD · MULT · FADE · DEST | WAVE · SPH · MODE · DEP | Figure §11.9 p.58 ; descriptions p.58–59 |

En AHD, HOLD devient disponible, SUS/REL ne le sont plus (§11.7 p.56).
La figure AMP citée montre ADSR ; elle ne suffit pas à certifier les
positions exactes de HOLD et DEC dans la variante AHD. Avec une waveform
LFO RND, SPH devient SLEW (§11.9 p.59). FILL et COND apparaissent comme
cadenas dans la figure TRIG 1 : aucune valeur neutre par défaut n’y est
lisible. OFF pour FILL signifie « jouer hors Fill », pas « ignorer Fill ».

### Grille des machines SRC et FLTR

Les six machines SRC audio disposent des deux vues précitées. Les positions
restent identiques entre vue paramètres et vue waveform. REPITCH n’expose
pas TUNE ; GRID et SLICE ne partagent pas les mêmes paramètres.

| Machine SRC audio | A · B · C · D | E · F · G · H | Référence |
|---|---|---|---|
| ONESHOT | TUNE · PLAY · — · SAMP | STRT · LEN · LOOP · LEV | A.2.1, p.93–94 |
| WERP | TUNE · PLAY · — · SAMP | SEG · MODE · BARS · LEV | A.2.2, p.95–96 |
| STRETCH | TUNE · PLAY · — · SAMP | STRT · LEN · BARS · LEV | A.2.3, p.96 |
| REPITCH | — · PLAY · — · SAMP | STRT · LEN · BARS · LEV | A.2.4, p.96–97 |
| SLICE | TUNE · PLAY · — · SAMP | SLICE · LEN · — · LEV | A.2.5, p.97–98 |
| GRID | TUNE · PLAY · — · SAMP | SLICE · LEN · GRID · LEV | A.2.6, p.100–101 |

| Machine FLTR, page 1 | A · B · C · D | E · F · G · H | Référence |
|---|---|---|---|
| MULTI-MODE | ATK · DEC · SUS · REL | FREQ · RESO · TYPE · ENV | A.3.1, p.104–105 |
| LOWPASS 4 | ATK · DEC · SUS · REL | FREQ · RESO · — · ENV | A.3.2, p.105–106 |
| EQ | ATK · DEC · SUS · REL | FREQ · GAIN · Q · ENV | A.3.3, p.106 |
| COMB− | ATK · DEC · SUS · REL | FREQ · FDBK · LPF · ENV | A.3.4, p.106–107 |
| COMB+ | ATK · DEC · SUS · REL | FREQ · FDBK · LPF · ENV | A.3.5, p.107–108 |
| LEGACY | ATK · DEC · SUS · REL | FREQ · RESO · TYPE · ENV | A.3.5, p.108, numéro réutilisé dans le manuel |

Le texte MULTI-MODE nomme F « RESO/GAIN », tandis que sa figure affiche
RESO. Cette divergence ne justifie pas d’inventer une sélection EQ dans
MULTI-MODE. Les pages MIDI sont différentes ; elles ne sont pas nécessaires
au premier jalon des seize pistes audio et leur sortie vers un appareil
externe n’est pas un prérequis du VST.

## 2. Gestes physiques et équivalents logiciels

La colonne adaptation est une proposition, pas une description du plugin
livré. Les raccourcis doivent fonctionner uniquement quand l’éditeur a le
focus et être neutralisés dans les champs de texte pour respecter Live.

| Action documentée | Règle / référence | Adaptation souris-clavier proposée |
|---|---|---|
| Choisir une piste | TRK+TRIG, sans déclenchement dans les modes NORMAL/SILENT/MANUAL ; sélection exclusive (§3.1 p.13 ; §8.3 p.25 ; §14.7.9 p.82) | Sélecteur explicite de piste ; commande TRK maintenue avec clic sur un pad pour la variante panneau |
| Choisir famille et sous-page | Famille exclusive ; même touche répétée ou ↑/↓ ; maintien pour afficher les valeurs (§6.2 p.18 ; §11.1 p.53) | Six boutons de famille ; clic répété / commandes précédent-suivant ; indicateur 1/2 ou 1/3 ; maintien souris reproduisant l’affichage de valeurs |
| Tourner / pousser un encodeur | Réglage relatif sans butée physique ; pousser accélère (§6.2 p.18 ; §8.2 p.25) | Glisser ou molette ; geste accéléré explicitement indiqué ; saisie numérique conservée |
| Valeurs repères | FUNC pendant certains réglages fait sauter entre repères musicaux (§6.2.1 p.19) | Modificateur FUNC dédié ; repères dépendant du paramètre, pas un facteur de vitesse uniforme |
| Valeur par défaut | Encodeur pressé + NO (§6.2 p.18) | Menu « Valeur par défaut » ou geste visible équivalent ; distinguer du rappel sauvegardé |
| Page : défaut / rappel / aléatoire | Famille+PLAY : défauts ; famille+NO : dernier état sauvegardé ; famille+YES : randomisation audio (§6.2 p.18 ; §11.1 p.53) | Trois actions distinctes sur la page active, avec portée lisible et undo ; ne pas appeler les trois « Reset » |
| Naviguer dans un menu | Flèches ou LEVEL/DATA ; YES valide/entre/coche ; NO remonte (§6.1 p.18 ; §6.3 p.19) | Flèches/molette, Entrée, Échap ; pile de menus et restauration du focus ; clic sur une ligne ne charge pas nécessairement son contenu |
| Modifier un trig | Maintenir un ou plusieurs TRIG puis ajuster les paramètres (§11 p.53 ; §11.3 p.54) | Maintien ou sélection persistante clairement marquée « pas » ; multisélection ; même grille de page, portée des locks explicite |
| CONTROL ALL | TRK maintenu affecte les pistes audio autorisées et toujours la piste active ; NO avant relâchement annule (§6.2.2 p.19 ; §9.8.1 p.37) | Bouton maintenable + masque des pistes ; transaction restaurable ; annulation pendant le geste, sans confondre avec automation du host |
| Copier / coller / effacer | FUNC+RECORD / STOP / PLAY ; un seul objet dans le presse-papiers ; répéter coller/effacer annule (§6.4 p.19) | Actions contextuelles nommées ; presse-papiers typé partagé ; undo de la dernière action dans son contexte |
| Sauvegarde/rappel temporaire | FUNC+YES / FUNC+NO pour le pattern (§3.1 p.12) | Deux commandes distinctes du fichier de projet Live et de la sauvegarde permanente du preset |
| Lire / pause / stop | PLAY démarre puis suspend ; STOP arrête (§3.1 p.13) | Trois états visibles ; en Host Sync, priorité au transport du DAW et comportement local explicitement adapté |

**Actuel.** Le clic sélectionne une piste ; le clic droit la mute. Un clic
sur un pas le bascule et le sélectionne ; clic droit ou modificateur le
sélectionne sans bascule. Les réglages de pas vivent dans un panneau séparé
et seuls pitch/cutoff ont des locks. Il n’existe pas de multisélection,
CONTROL ALL, presse-papiers, rappel de page ou sauvegarde temporaire.
Les attachments JUCE donnent déjà automation et double-clic de retour
par défaut pour les paramètres attachés : cela ne fournit pas un rappel
du dernier preset sauvegardé.

## 3. Modes et états de navigation à distinguer

Les seize TRIG forment une grille physique de deux rangées de huit. Leur
rôle dépend du contexte ; ce ne sont pas seize boutons de pas permanents
(§3.1 p.12–13 ; §8.1/8.5 p.25–28). L’état de lecture, le mode
d’enregistrement, le clavier et les menus doivent être modélisés
séparément : par exemple clavier et enregistrement live se combinent.

| Contexte | Rôle des pads et état à conserver | État du prototype / référence |
|---|---|---|
| TRIG TRACKS | Déclenche piste ; sélectionner avec ou sans audition dépend de TRK SELECT | Sélection silencieuse séparée d’AUDITION ; pas de réglage TRK SELECT. §8.5.4 p.27 ; §14.7.9 p.82 |
| KEYBOARD | Jeu chromatique ou gamme/root/fold ; octave visible conservée indépendamment du pattern, réglages de gamme dans le pattern | Absent. §8.5.1–8.5.2 p.25–26 |
| MUTE GLOBAL / PATTERN | Deux couches distinctes ; global dans projet, pattern dans pattern ; dernier mode mémorisé | Un seul booléen de mute par piste. §8.5.3 p.26–27 |
| Mutes préparés | Modifications accumulées pendant FUNC, appliquées à son relâchement ; affichage de la préparation | Absent. §8.5.3 p.27 ; notes 1.00→1.01 pour couleurs rouge/orange |
| TRIG VELOCITIES | Les pads jouent la piste active avec seize vélocités croissantes | Absent. §8.5.4 p.27 |
| TRIG RETRIGS | Les pads sélectionnent des taux de retrig musicaux | Absent ; nombre 1–8 par pas ne représente pas ce mode. §8.5.4 p.28 |
| TRIG SLICES / PRESET POOL | Pads = slices ou slots de preset ; PAGE peut changer le groupe de seize | Absents. §8.5.4 p.28 |
| GRID / LIVE / STEP RECORDING | Même grille avec des actions d’entrée différentes ; clavier utilisable en live | Le prototype correspond à une édition grid simplifiée toujours accessible, sans modes RECORD. §3.1 p.13 ; §10.2 p.41–43 |
| Menus de machines / setup / bibliothèque | Menus distincts de l’écran de paramètres ; YES/NO et retour au contexte d’origine | Absents sauf sélecteur de fichier OS. §3.1 p.12–13 ; §9.2–9.8 p.30–38 |

Les retours visuels ont une signification : trigs de notes rouges,
locks signalés par clignotement rouge/jaune, progression de lecture et
famille active visibles (§3.1 p.12–13) ; mutes globaux verts et mutes de
pattern magenta (§8.5.3 p.26–27). Les notes 1.00→1.01 précisent rouge pour
une préparation de mute et orange pour une préparation de démute. La
palette orange uniforme actuelle ne code pas ces contextes. Leur
transposition graphique peut ajouter icônes et textes pour rester lisible
sans dépendre uniquement de la couleur.

**Adaptation proposée.** Maintenir un état UI explicite : famille active,
sous-page mémorisée par famille, piste active, page de séquenceur éditée,
ensemble de trigs ciblés, mode TRIG, clavier, mode de mute, mode RECORD et
pile de menus. La page actuellement lue a un indicateur distinct. Le
prototype utilise seulement `selectedTrack`, `selectedStep`, `selectedPage`.
Définir l’arbitrage des gestes combinés avant de les coder ; la liste de
modes ci-dessus ne documente pas à elle seule toutes les priorités.

Pour une bibliothèque ouverte, conserver séparément élément surligné,
éléments cochés et contenu réellement affecté. Quitter une préécoute avec
NO ne doit pas remplacer le sample. Les notes 1.03A→1.10 exigent également
que le choix de destination MOD se referme au changement de piste et que
les noms longs ou la recherche ne masquent pas les informations utiles.

## 4. Bibliothèque, pool et objets musicaux

**Documenté.** Les modifications d’un preset chargé affectent sa copie dans
le pattern, pas l’original de bibliothèque (§9 p.29). Les paramètres TRIG
appartiennent au pattern, contrairement aux paramètres de preset (§11
p.53). Le niveau de piste LEVEL et le volume AMP VOL sont distincts ; VOL
peut être verrouillé sur un pas, LEVEL ne le peut pas (§11.7 p.57).

| Objet | Contenu / portée | Référence |
|---|---|---|
| Projet | Huit banques de seize patterns, pool de samples/presets, mutes globaux | §9/9.1 p.29 ; §8.5.3 p.27 |
| Pattern | Kit, données des seize séquences, défauts TRIG, BPM, longueur, swing, signature | §9 p.29 |
| Kit | Seize presets et paramètres communs : niveaux, sends, compression/distorsion/routages, masque CONTROL ALL | §9 p.29 |
| Preset | Sample référencé et pages SRC/FLTR/AMP/FX/MOD ; copie indépendante après chargement | §9 p.29 |
| Pools | 128 presets pour les preset locks ; 1016 slots de samples du projet pour leur affectation/locks | §9.1 p.29 ; §9.7 p.36 |

L’identité d’un sample matériel repose sur son contenu, pas sur son chemin
ou son nom ; déplacement/renommage ne rompent pas l’utilisation (§9 p.29).
Les formats/capacités de stockage du matériel ne nécessitent pas de
simuler une connexion physique ou de limiter artificiellement le stockage
PC. Une bibliothèque de fichiers utilisateur et des identifiants de
contenu constituent une **adaptation logicielle**, à définir séparément.

Le navigateur de presets propose tri par nom/slot, banques, tags et recherche,
préécoute, affectation à la piste, gestion, sauvegarde nommée/taggée et
protection d’écriture (§9.2–9.3 p.30–35). Les écrans de samples distinguent
bibliothèque et RAM projet, dossiers et sélection multiple (§13.6 p.70–71).
La procédure quick assign part de SAMP sur SRC, permet une préécoute via
la voix de piste, puis charge le choix dans la piste et le pool (§9.7 p.36).
La préécoute documentée dure approximativement dix secondes, sans exiger
d’affectation préalable. Le chargement doit conserver son caractère de
copie, et la fermeture du navigateur doit restituer la navigation antérieure.

**Actuel.** IMPORT SAMPLE ou dépôt de fichier affecte immédiatement un
fichier à la piste active. AUDITION joue le sample déjà affecté. Il n’y a
ni bibliothèque musicale, ni pool, ni choix temporaire préécoutable, ni
sauvegarde de preset/kit, ni collection de patterns. La sauvegarde Live
embarque bien paramètres, séquences et samples : cette qualité doit être
préservée et ne remplace pas ces opérations internes distinctes.

SLICE ajoute un menu puis un éditeur, pas une troisième page SRC : édition
de limites liées/non liées, création de grille, locks linéaires/aléatoires,
zoom, position de vue, sélection/scission/suppression et préécoute
(A.2.5 p.98–100). GRID possède son propre menu de création de locks
(A.2.6 p.101). Ce sont des fonctions musicales pertinentes au VST.

## 5. Écarts majeurs et priorités

| Priorité | Écart actuel | Contrat attendu / source |
|---|---|---|
| P0 | Pas de navigation par familles, huit commandes ou sous-pages | Écran contextuel et état de navigation prévisible ; §§3/6/11 précités |
| P0 | Sélection/jeu/édition/mute sont des widgets permanents sans modes | Définir les contextes et gestes, puis implémenter leur arbitrage ; §8 p.25–28 |
| P0 | Une refonte pourrait remapper les valeurs sauvegardées | Conserver identité VST3, IDs d’automation et lecture des états `TAKTII_STATE_1` de `751ebcb` |
| P1 | LEVEL existant ne distingue pas SRC LEV, AMP VOL et niveau de piste | Trois portées distinctes ; annexe A p.93 ; §11.7 p.57 ; aucune alias automatique entre elles |
| P1 | START/END et reverse/loop simplifient ONESHOT | STRT+LEN définit la fin ; LOOP est un point séparé ; PLAY a quatre modes ; §13.4 p.70 |
| P1 | Locks limités à pitch/cutoff et panneau dédié | Ciblage de plusieurs trigs et paramètres des pages, sans perdre leurs valeurs de base ; §11 p.53 ; notes 1.03A→1.10 |
| P1 | Pas de copy/paste/undo, rappel, sauvegarde temporaire ou CONTROL ALL | Opérations contextuelles et transactions distinctes ; §6.2–6.4 p.18–19 ; §11.1 p.53 |
| P1 | Pas de bibliothèque/pool/préécoute avant affectation | Séparation choix/écoute/validation, preset/kit comme copies ; §9 p.29–38 |
| P2 | Pas de machines SRC/FLTR, enveloppes complètes, trois LFO, chorus ou routages | Fonctionnalités musicales à ajouter ; l’approximation DSP ne dispense pas du contrat des commandes ; §11 et annexe A |

L’écart des bits est mesurable : le prototype expose 4–24 bits, alors que
BR documenté va de 16 à 1 bit (§11.8 p.58). Il faut un adaptateur explicite
si les valeurs/labels changent ; renommer le contrôle ne rend pas sa plage
fidèle. De même, les valeurs affichées en secondes/Hz ou pourcentage ne
doivent pas être présentées comme les valeurs natives matérielles sans
une table de conversion validée.

## 6. Premier jalon d’implémentation proposé

**Jalon UI-1 : navigation compacte avec état musical préservé.** La cible
est une zone centrale de huit commandes en deux rangées, six familles,
indicateurs de piste/sous-page/page jouée et aide de gestes stable. Le
sélecteur explicite de piste, l’import utilisateur et HOST SYNC peuvent
rester des commodités du VST. Leur statut d’adaptation doit être clair.

Le jalon doit commencer par un contrôleur de navigation séparé du moteur :
changer piste, famille, sous-page ou écran ne modifie aucun paramètre audio,
trig, sample ou transport. Réutiliser les attachments et IDs actuels ; ne
pas renommer `tN_gain`, `tN_pitch`, `tN_start`, `tN_end`, etc. Les libellés
physiques doivent passer par une table de présentation, sans confondre
`pitch` de lecture/TUNE avec NOTE TRIG, ni END avec LEN, ni gain avec AMP VOL.

Dans ce premier jalon, repositionner les commandes déjà opérationnelles
et identifier les cases/capacités encore absentes. Ne pas dessiner de
commande active qui semble accomplir un traitement inexistant. L’ajout
effectif des objets de bibliothèque, états de modes et paramètres manquants
relève des jalons suivants. L’état UI peut d’abord rester local à l’éditeur ;
sa persistance éventuelle doit être additive avec des valeurs par défaut
pour les anciens projets. Le lecteur d’état existant ne doit pas être
remplacé par un format qui rejette `TAKTII_STATE_1`.

Critères d’acceptation à tester lors de cette future implémentation :

1. Ouvrir un projet Live de `751ebcb` : identité, automation, trigs,
   longueurs et audio embarqué sont restaurés ; refermer/réouvrir conserve
   les mêmes valeurs musicales.
2. Parcourir toutes les familles, sous-pages et pistes sans éditer : aucun
   changement musical et aucun déclenchement involontaire.
3. Vérifier les positions A–H contre les tables ci-dessus, y compris les
   cases vides ; tout écart de plage/unité est explicitement recensé.
4. Modifier un contrôle opérationnel, automatiser son ID depuis Live et
   changer de piste : le bon paramètre suit, les autres restent inchangés.
5. Distinguer clairement niveau piste, portée piste et portée pas ; sélectionner
   un trig sans le basculer ; afficher page jouée même si une autre est éditée.
6. Fermer un menu ou une préécoute avec NO/Échap : retour exact au contexte
   précédent ; texte saisi non intercepté par les raccourcis musicaux.
7. Comparer capture et état attendu aux transitions définies, plutôt que
   considérer une seule capture statique comme preuve de fidélité.

Ce document seul ne nécessite pas de rebuild. Ces critères ne sont pas
annoncés comme validés par les tests de la première version.

## 7. Points à résoudre sans supposition silencieuse

- La figure AMP disponible est en ADSR ; compléter la grille AHD avant de
  prétendre en reproduire toutes les positions. HOLD/NOTE et retrigger/reset
  ne se déduisent pas d’un simple contrôle de decay.
- Le manuel affiche des cadenas pour FILL/COND sans valeur neutre lisible ;
  ne pas assimiler FILL OFF à absence de condition. Le contrat détaillé du
  séquenceur est traité dans [SEQUENCER_WORKFLOW_AUDIT.md](SEQUENCER_WORKFLOW_AUDIT.md).
- §3.1 p.13 contient une phrase manifestement mêlée au paragraphe PAGE
  (opérations sur patterns insérées au milieu). Utiliser §10 pour les
  règles de PAGE ; ne pas transformer cette erreur de mise en page en action.
- Les notes 1.03A→1.10 et §14.7.9 p.82 documentent quatre comportements
  TRK SELECT : NORMAL, SILENT, MANUAL, INVERTED. Le choix par défaut du VST
  est une décision d’adaptation à annoncer, pas une propriété déduite du
  simple sélecteur actuel.
- Les notes 1.10B→1.15 introduisent SLICE ; 1.15→1.15A corrigent notamment
  ses locks en STEP RECORDING et PLAY MODE. Les ajouts Outbox 1.16/1.17
  ne créent aucun manque dans ce produit. Voir [FIRMWARE_AUDIT.md](FIRMWARE_AUDIT.md).
