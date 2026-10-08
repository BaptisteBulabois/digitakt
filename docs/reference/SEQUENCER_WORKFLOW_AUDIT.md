# Audit du séquenceur et du workflow — Digitakt II OS 1.17

Audit documentaire du 8 octobre 2026, sur le code `87a5e84`. La base VST3
installée dans Live est `751ebcb`. Aucune modification de source, de format
d'état ou d'identité du plugin n'est proposée comme déjà réalisée ici.

La cible est le comportement musical et les interactions définis dans
[`PRODUCT_SCOPE.md`](../PRODUCT_SCOPE.md). L'identité sonore du matériel n'est
pas un critère. Les entrées MIDI utiles au jeu et à l'enregistrement dans Live
restent pertinentes ; Overbridge, Outbox et la connexion à un appareil réel
sont hors périmètre.

## Sources effectivement lues

- **[Digitakt II User Manual, OS 1.17](https://www.elektron.se/wp-content/uploads/2026/10/Digitakt-2-User-Manual_ENG_OS1.17_260930.pdf)**,
  mis à jour le **24 septembre 2026**, PDF de 118 pages : copies locales
  `.reference/elektron/manual-os1.17.pdf` et `manual-os1.17.txt`.
  Les citations ci-dessous désignent les **pages imprimées**, identiques aux
  numéros de pages PDF pour les sections étudiées. Les figures des pages 45
  et 48 ont aussi été consultées : l'extraction perd les barres d'inversion
  des conditions.
- **[Digitakt II OS Release Notes](https://www.elektron.se/release-notes/digitakt-ii-os-release-notes)**,
  page officielle portant la date **1er octobre 2026**, changements jusqu'à
  **1.17**, copie locale `.reference/elektron/release-notes.txt`.
  Ce document HTML n'a pas de pagination ; ses citations indiquent les versions
  de départ et d'arrivée et la rubrique concernée.
- Versions et empreintes : [SOURCES.md](SOURCES.md). Les copies officielles
  sont exclues de Git ; les liens ci-dessus permettent leur consultation.
- Code relu : [`Engine.h`](../../src/engine/Engine.h),
  [`Engine.cpp`](../../src/engine/Engine.cpp),
  [`PluginProcessor.h`](../../src/PluginProcessor.h),
  [`PluginProcessor.cpp`](../../src/PluginProcessor.cpp), interactions de
  [`PluginEditor.cpp`](../../src/PluginEditor.cpp),
  [`engine_tests.cpp`](../../tests/engine_tests.cpp) et
  [`plugin_tests.cpp`](../../tests/plugin_tests.cpp).

**Partiel** signifie qu'une partie du comportement existe ; **absent** qu'aucun
modèle/action correspondant n'a été trouvé ; **divergent** qu'un comportement
actuel concret contredit la référence. Un test de stabilité du prototype ne
prouve pas sa conformité au manuel.

## 1. Transport, patterns et longueur

| ID | Contrat documenté et référence | État actuel / preuve dans le code |
|---|---|---|
| SQ-01 | Un projet possède 8 banques A–H de 16 patterns. Un pattern regroupe trigs, locks, réglages TRIG, paramètres de pistes/effets, quantize, longueur, vitesse et swing. **§10, p.40 ; §5.2.3, p.16.** | **Absent** pour banques/patterns. `PluginProcessor::steps/lengths/samples` et son unique nœud `SEQUENCER` décrivent un seul état de pattern. Le chargement de démo remplace cet état. |
| SQ-02 | Sélectionner un autre pattern pendant la lecture le met en attente ; son départ intervient à la fin du pattern courant. Les messages Program Change peuvent sélectionner/mettre en attente un pattern. **§10.1.1, p.40.** | **Absent** : pas de pattern actif/en attente ni de traitement Program Change dans `processBlock`. |
| SQ-03 | PLAY lance puis permet pause/reprise. STOP coupe les pistes, avec les retours d'effets encore audibles ; un double STOP réduit aussi les effets par un court fondu. **§10.1.2, p.40.** | **Divergent** pour STOP : `Transport::playing=false` bloque les prochains trigs, mais `Engine::process` continue de rendre les voix actives jusqu'à leur fin naturelle. Un seul booléen `play` ne distingue pas pause/stop/double stop. `reset()` efface immédiatement voix et effets. |
| SQ-04 | Jusqu'à 128 pas sur 8 pages de 16. En GRID, page d'édition et page jouée sont distinguées ; une sélection de pages peut être jouée en boucle, avec retour au parcours complet. **§10.2.2, p.42 ; §10.7, p.45.** | **Partiel** : 128 pas et 8 boutons de page existent. Ceux-ci changent seulement `selectedPage/selectedStep` ; aucun masque de pages jouées ni parcours de boucle de pages. |
| SQ-05 | PER PATTERN partage longueur/vitesse entre les pistes ; PER TRACK permet des valeurs indépendantes. SPEED propose **1/8×, 1/4×, 1/2×, 3/4×, 1×, 3/2×, 2×**. **§10.7.1–10.7.2, p.46.** | **Partiel** : longueur indépendante 1–128 via `setTrackLength`. La durée de tous les pas reste `stepBeats=0.25` ; aucun mode commun ni SPEED par piste/pattern. |
| SQ-06 | CHANGE fixe la limite de changement vers un pattern en attente ; RESET fixe quand toutes les pistes repartent ensemble, avec **INF** possible. Un CHANGE inférieur prévaut sur RESET. **§10.7.2, p.46–47.** | **Absent** : `index % lengths_[track]` boucle chaque piste indépendamment depuis le PPQ absolu. Pas de compteur maître, RESET, CHANGE ou INF. |
| SQ-07 | Étendre la longueur recopie les trigs vers les nouvelles pages si **PAGE AUTOCOPY** est actif ; l'option permet de désactiver cette copie. **§10.7.1, p.46 ; §14.7.5, p.82.** | **Absent** : `setTrackLength` ne modifie que l'entier de longueur. Les pas déjà stockés au-delà de la longueur peuvent réapparaître ; ce comportement ne remplace pas l'autocopie. |
| SQ-08 | Le swing est un **ratio**, 50 % étant neutre, réglable de 51 à 80 %. Il appartient au pattern ; le tempo peut être commun au projet ou propre au pattern. **§7.3.1, p.22–23 ; §10, p.40.** | **Divergent** : `stepTime` retarde les pas impairs de `swing*0.125` battement avec `swing∈[0,0.75]`. Sur une paire de pas, le ratio réel vaut `50% + 25%*swing`, soit **50–68.75 %**, alors que l'UI affiche 0–75 %. Tempo/swing sont actuellement globaux à l'instance. |

L'horloge actuelle constitue une base utile : synchronisation BPM/PPQ du host,
tempo interne, positions d'événements à l'échantillon près et indépendance des
tailles de blocs sont testés. Elle ne fournit pas encore les compteurs de
**visites musicales** nécessaires aux patterns, aux boucles et aux conditions.

## 2. Modes de saisie et manipulation des trigs

| ID | Contrat documenté et référence | État actuel / preuve dans le code |
|---|---|---|
| SQ-09 | Deux types : **note trig** déclenche son/note ; **lock trig** applique des locks sans déclencher de note. Rouge et jaune les distinguent. **§10.2.1, p.41.** | **Absent** pour lock trig. `Step::enabled` est le seul type ; `schedule` ne produit que des appels à `trigger` qui réinitialisent une voix. Un pas désactivé muni de locks n'effectue aucune action. |
| SQ-10 | GRID : appui court ajoute/retire ; maintien prépare l'édition ; FUNC+trig ajoute un lock trig. Sélection de plusieurs trigs, préécoute avec leurs locks et déplacement global d'un pas sont possibles. **§10.2.2, p.42.** | **Partiel** : clic bascule `enabled`, clic droit sélectionne le pas. Pas de mode GRID explicite, de maintien/multisélection, de préécoute propre au pas ou de décalage de la piste. `triggerTrack` préécoute la piste, sans les locks du pas sélectionné. |
| SQ-11 | LIVE : saisie de trigs/pitch/velocity/durée en temps réel ; commandes de paramètres enregistrées comme locks et création de lock trigs si nécessaire. Quantize automatique commutable. Sortir de LIVE peut laisser la lecture active. **§10.2.3, p.42–43 ; §10.8.1, p.47.** | **Absent** : entrée MIDI et bouton TRIGGER jouent seulement des voix. `processBlock` n'écrit jamais ces événements ni l'automation dans `steps`. Aucun état d'enregistrement, quantize de saisie ou mesure de durée Note On/Off. |
| SQ-12 | STEP STANDARD capture une note et avance d'un pas ; un silence/effacement avance aussi. STEP JUMP avance selon LEN et pose le lock de longueur correspondant. **§10.2.4, p.43.** | **Absent** : pas de curseur d'insertion, STANDARD/JUMP, longueur de trig ou capture MIDI chromatique de la piste active. |
| SQ-13 | Dans STEP, ajouter un note trig sur un note trig existant conserve ses locks. Entrée MIDI détermine la vélocité verrouillée ; FUNC peut fixer VEL ; YES peut mesurer/verrouiller LEN. Les raccourcis de sauvegarde/reload temporaire n'agissent pas dans ce contexte. **§10.2.4, p.44.** | **Absent** pour le contexte STEP. La conservation actuelle des locks lors d'un simple clic n'est donc pas un équivalent complet de cette règle. |
| SQ-14 | **LIVE REC OVERDUB** permet de superposer notes/locks aux trigs existants. **PARAM LIVE REC** choisit entre tous les pas et les seuls trigs existants. **§14.7.7–14.7.8, p.82.** | **Absent** : aucune préférence ni logique d'enregistrement correspondant. |

Le mapping MIDI actuel est une adaptation de boîte à rythmes : Note On **36–51**
joue les pistes 1–16, sans distinction de canal. Note Off, Program Change et
contrôleurs sont ignorés, puis le buffer MIDI est effacé. Les tests
`testMidiAndBuses` valident explicitement ce mapping et ces événements ignorés.
Ils ne valident pas la saisie GRID/LIVE/STEP d'une piste active décrite aux
**§10.2.2–10.2.4, p.42–44**.

Les accords de quatre notes cités dans ces sections concernent les **pistes
MIDI**. Ils n'imposent pas une polyphonie à quatre voix sur chaque piste audio.
Une éventuelle sortie de notes vers une autre piste de Live est un choix de
périmètre musical à traiter séparément : elle n'exige aucun appareil physique.

## 3. Locks, conditions et probabilité

| ID | Contrat documenté et référence | État actuel / preuve dans le code |
|---|---|---|
| SQ-15 | Les paramètres des pages peuvent être verrouillés sur les différents types de trigs/pistes ; le pattern admet jusqu'à **80 paramètres distincts verrouillés**, indépendamment du nombre de pas utilisant chacun. Maintenir trig puis modifier un encodeur pose le lock ; presser cet encodeur l'enlève. **§10.8.1, p.47.** | **Partiel** : seuls `lockPitch` et `lockCutoff` existent. Velocity/probability/microtiming sont des valeurs fixes par pas, sans notion de valeur TRIG de base et de lock optionnel. AMP, sends, sample, retrig, LFO et les autres paramètres n'ont pas de locks génériques. |
| SQ-16 | Enlever puis remettre un note trig efface ses locks. L'effacement ciblé de locks, et la pose sur tous les trigs d'une page/piste, sont possibles. **§10.8.1, p.47.** | **Divergent** : `Editor::selectStep` bascule seulement `enabled` ; désactiver puis réactiver conserve pitch/cutoff locks et les autres données. `clearTrack` efface tous les pas sans geste ciblé ni undo. À distinguer de SQ-13 et de la conversion lock trig→note trig. |
| SQ-17 | Un **preset lock** peut remplacer le preset de piste sur un note trig par un preset du pool. **§10.8.2, p.48.** | **Absent** : un seul `Sample` et un seul jeu de `TrackParams` par piste ; aucun preset/sample/pool référencé par le pas. |
| SQ-18 | **PROB** est une valeur générale TRIG, verrouillable par pas. Son résultat est réévalué chaque fois qu'un trig doit jouer. **§11.2, p.53.** | **Divergent sur les visites répétées du même PPQ** : `schedule` calcule `random01(index*37 + track*7919)`. Une boucle ou un seek de Live vers le même index absolu reproduit toujours le même tirage. Un déroulement continu varie entre index successifs, mais ce n'est pas un état aléatoire par occurrence. Pas de PROB général de piste. |
| SQ-19 | **A:B** : vrai à la visite A d'un cycle de B visites ; son inverse joue hors de cette visite. Le compte suit le pattern, ou la piste si elle est plus courte, et recommence après le cycle ; il dure jusqu'à l'arrêt du séquenceur. **Titre imprimé §10.7.3, p.48, situé sous §10.8.** | **Partiel** : `(index/trackLength)%conditionEvery == conditionOffset` représente le cas simple **A=offset+1, B=every** pour une piste en déroulement continu. Pas d'inverse, de longueur maître/pattern ou de remise à zéro des visites à STOP. Le PPQ du host fixe la phase au lieu d'un compteur musical. |
| SQ-20 | **PRE / inverse PRE** utilisent le dernier résultat de condition évalué sur la même piste, en ignorant PRE et inverse PRE comme nouvelles sources d'historique. **§10.7.3 imprimé, p.48.** | **Absent** : pas d'historique d'évaluation ni de type de condition. Une condition PRE doit pouvoir dépendre d'un trig qui a été évalué faux, pas uniquement du dernier son effectivement joué. |
| SQ-21 | **NEI / inverse NEI** consultent la piste précédente, en ignorant ses conditions PRE ; NEI est faux si cette voisine n'a aucun trig conditionnel. **§10.7.3 imprimé, p.48.** | **Absent** : pistes évaluées sans historique partagé. L'ordre chronologique des évaluations devra être défini, particulièrement pour deux pistes au même instant. |
| SQ-22 | **1ST / inverse 1ST** distinguent la première lecture du pattern ; **LST / inverse LST** distinguent sa dernière lecture avant un changement. **§10.7.3 imprimé, p.48.** | **Absent** : pas de contexte d'entrée/sortie du pattern ni de changement en attente. A:B ne peut pas représenter ces règles. |
| SQ-23 | **FILL** est séparé de COND : ON joue pendant FILL, OFF joue hors FILL. FILL peut être tenu, enclenché pour le prochain cycle entier ou verrouillé jusqu'à désactivation. **§11.2, p.54 ; §10.8.4, p.49.** | **Absent** : ni filtre FILL par trig ni état momentané/prochain cycle/verrouillé. Ne pas confondre la valeur **OFF** avec « aucune condition de Fill ». |

Le manuel imprime plusieurs fois les mêmes noms dans l'extraction texte : les
secondes versions portent une **barre d'inversion** dans le PDF p.48. Leur
omission supprimerait un comportement documenté. La numérotation **10.7.3** est
une anomalie du manuel lui-même ; les citations la conservent.

**PROB, FILL et COND doivent pouvoir coexister.** Les notes **1.00→1.01,
Bug fixes** corrigent précisément la combinaison PRE/NEI avec PROB/FILL. Le
texte ne spécifie toutefois pas exhaustivement l'ordre d'évaluation et le
résultat exact mémorisé pour chaque combinaison ; les points ouverts sont
énumérés plus bas.

Pour corriger SQ-18, une identité d'occurrence distincte du PPQ est nécessaire.
Un PRNG avec graine fixe pour les tests peut préserver l'indépendance des
tailles de blocs sans imposer la même décision à chaque retour de boucle.
Les recherches anticipées de pas dans `schedule` ne doivent pas effectuer de
nouveaux tirages à chaque bloc pour la même occurrence. Le manuel ne demande
pas que deux tirages successifs donnent obligatoirement deux résultats
différents.

## 4. Retrigs, microtiming et quantize

| ID | Contrat documenté et référence | État actuel / preuve dans le code |
|---|---|---|
| SQ-24 | Retrig s'applique aux pistes audio, pas aux pistes MIDI. Réglage global TRIG PAGE 2 et locks par trig, avec édition de plusieurs trigs. **§10.5, p.45 ; §11.3, p.54.** | **Partiel** : `Step::retrigs` existe sur toutes les pistes, aujourd'hui uniquement audio. Aucun réglage de base de piste, enable RTRG distinct ou enregistrement LIVE. Les événements externes appellent `trigger` une fois et ne créent aucune répétition. |
| SQ-25 | **RTRG**, **VFAD**, **LEN**, **RATE** sont distincts. VFAD va de −64 à +64 ; LEN définit l'étendue de la courbe de vélocité, jusqu'à INF. RATE est un intervalle musical, avec **1/1, 1/2, 1/3, 1/4, 1/5, 1/6, 1/8, 1/10, 1/12, 1/16, 1/20, 1/24, 1/32, 1/40, 1/48, 1/64, 1/80**. 1/16 = un trig par pas ; 1/32 = deux ; 1/12 ou 1/24 permettent des triolets. **§11.3, p.54.** | **Divergent** : un entier 1–8 répartit les événements dans la seule durée du pas courant, calculée aussi avec swing. Pas de durée indépendante, d'intervalle lent dépassant un pas, de VFAD ou de retrig illimité. Le compte inclut l'événement initial ; il ne doit pas être simplement renommé RATE. |
| SQ-26 | Microtiming avance/retarde les trigs sur pistes audio et MIDI ; édition individuelle, multiple ou de tous les trigs, stockage dans le pattern. La figure montre **+1/384**. **§10.4, p.45.** | **Partiel** : déplacement `microtiming*0.25` battement, sauvegardé par pas ; bornes −0.49…+0.49 pas et incrément UI 0.01 pas. L'affichage en pourcentage ne reprend pas l'unité de la figure. Les bornes et la résolution complètes ne sont pas données par le texte : ne pas les déclarer conformes sur cette seule base. |
| SQ-27 | Quantize agit en temps réel sur les trigs décalés ; réglages **TRACK** et **PATTERN** graduels. Il est distinct du quantize automatique de l'enregistrement LIVE. **§10.6, p.45 ; §10.2.3, p.42–43.** | **Absent** : pas de quantize track/pattern ni de réglage de saisie. Mettre manuellement microtiming à zéro n'offre pas les mêmes niveaux ni la même portée. |

LEN du retrig est explicitement décrit comme la durée de sa **courbe de
vélocité**. Le texte étudié ne suffit pas à affirmer qu'il est l'unique règle
d'arrêt de la répétition : interaction avec LEN du note trig, note relâchée et
trig suivant à préciser. La probabilité est tirée une seule fois pour le groupe
de répétitions dans le prototype ; le manuel ne tranche pas explicitement un
tirage par sous-répétition, donc ce point n'est pas annoncé comme un défaut
établi.

## 5. Copie, collage, effacement et restauration

| ID | Contrat documenté et référence | État actuel / preuve dans le code |
|---|---|---|
| SQ-28 | Copier/coller/effacer le pattern hors GRID ; les mêmes commandes portent sur la piste en GRID. Des patterns non actifs peuvent être copiés/collés/effacés sans quitter le pattern courant. **§10.1.1, p.40 ; §10.8.5, p.49.** | **Absent**, sauf effacement de piste par `clearTrack`. Pas de presse-papiers/contextes ni de collection de patterns. |
| SQ-29 | Copier/coller/effacer une page de piste touche les seuls 16 pas de cette page. Les pages de paramètres se copient entre pistes ou reviennent à leurs valeurs par défaut. **§10.8.5, p.49.** | **Absent** : sélection de page uniquement visuelle, aucun buffer/actions de page ou de paramètres. |
| SQ-30 | Copier des trigs inclut leurs locks ; une sélection multiple garde ses distances relatives au collage, depuis le pas de destination. L'action CLEAR sur trigs enlève les locks. **§10.8.5, p.49.** | **Absent** : pas de multisélection, copie/collage ou effacement ciblé des locks. |
| SQ-31 | Répéter les touches d'une opération COPY/PASTE/CLEAR permet de l'annuler. **§10.8.5, p.49.** | **Absent** : aucun historique ou snapshot d'undo. Un éventuel undo de Live ne constitue pas une implémentation de cette action musicale locale. |
| SQ-32 | TEMP SAVE crée un point de restauration du pattern actif sans sauvegarde permanente ; TEMP RELOAD y revient. Sans TEMP SAVE, le reload prend l'état sauvegardé permanent. Changer de projet perd cette mémoire temporaire. **§10.8.6, p.49–50.** | **Absent** : `getStateInformation/setStateInformation` sauvegardent/rappellent l'instance pour le DAW ; aucun état permanent versus travail versus snapshot temporaire par pattern. `loadDemoPattern` n'est pas un reload. |

## 6. Arrangement et performance

| ID | Contrat documenté et référence | État actuel / preuve dans le code |
|---|---|---|
| SQ-33 | Une chaîne peut contenir jusqu'à **64 occurrences de patterns** de A–H, doublons possibles ; création pendant la lecture, parcours en boucle. Nouvelle chaîne ou sélection normale d'un pattern/song la remplace ; elle n'est pas sauvegardée sur l'appareil. **§10.1.3, p.40–41.** | **Absent** : aucun objet chaîne ni liste ordonnée de patterns. Pour le VST, choisir explicitement la durée de vie de ce buffer dans le rappel DAW sans prétendre qu'une chaîne matérielle est sauvegardée. |
| SQ-34 | **16 songs**, jusqu'à **99 lignes** chacune. Chaque ligne définit pattern, répétitions, longueur **2–1024 pas**, tempo/swing et mutes ; END choisit LOOP/STOP. **§10.9–10.9.1, p.50.** | **Absent** : aucun song/ligne, longueur de ligne, override de tempo/swing ou mutes d'arrangement. L'automation de Live est utile mais ne fournit pas cet écran ni ces règles. |
| SQ-35 | Création de song à partir d'une chaîne ; insertion copie la ligne courante, copie/collage/reset/suppression de ligne ; déplacement de playhead, boucle de ligne et saut vers une prochaine ligne. STOP reprend à la position ; double STOP revient au début du song. **§10.9.2–10.9.3, p.51 ; persistance, p.52.** | **Absent** : pas de mode SONG ni de curseur/commandes d'arrangement. |
| SQ-36 | **PERFORM KIT** garde le kit modifié entre changements de pattern et évite son autosauvegardage. Reload/save du kit restent accessibles, ainsi que TEMP SAVE/RELOAD du pattern. **§10.10–10.10.1, p.52.** | **Absent** : aucun kit dissocié du pattern ni snapshot de performance. |
| SQ-37 | **CONTROL ALL** modifie un paramètre sur les pistes audio autorisées, inclut toujours la piste active, et permet d'annuler avant la fin du geste. TRACK SWAP échange réglages, preset et séquence. **§6.2.2, p.19 ; §9.8.1, p.37.** | **Absent** : `setParameter` ne vise qu'un paramètre identifié ; aucune portée collective/configuration, transaction de geste ou échange complet de pistes. |
| SQ-38 | Mutes **GLOBAL** et **PATTERN** distincts, persistants à leurs niveaux respectifs ; mutes préparés appliqués au relâchement de FUNC. **§8.5.3, p.26–27.** | **Partiel** : un booléen automatable `tN_mute`. Pas de couches global/pattern/row ni de préparation groupée. Le moteur bloque aussi les déclenchements manuels et fige la voix active pendant mute ; la portée exacte attendue pour audition/MIDI devra être vérifiée. |
| SQ-39 | Euclidean : deux générateurs de pulses, rotations individuelles/de piste, opérateurs OR/XOR/AND/SUB. Les trigs ordinaires restent conservés/inactifs, reparaissent à la sortie ; conversion en séquence ordinaire possible avec locks. **§10.3, p.44–45.** | **Absent** : aucun générateur, masque de trigs ou jeu de trigs ordinaires préservé. Cette fonctionnalité est musicale et reste dans le périmètre. |

## 7. Notes OS à convertir en régressions de workflow

| Notes officielles | Contrat à conserver dans la cible 1.17 |
|---|---|
| **1.00→1.01**, Improvements/Bug fixes | Option PAGE AUTOCOPY et boucles de pages ; combinaison PRE/NEI avec PROB/FILL ; aucun reset des tweaks PERFORM KIT lors du redémarrage ; swing applicable aux séquences euclidiennes. |
| **1.01A→1.02**, Bug fixes | RTRG/VFAD/LEN/RATE enregistrables en LIVE ; retrig en KEYBOARD ; boucles de pages correctes ; LIVE sur une page bouclée n'efface pas les trigs d'une autre page ; quitter KEYBOARD SETUP en STEP ne supprime pas le pas courant. |
| **1.03A→1.10**, Improvements | LIVE REC OVERDUB ; locks posables sur les trigs d'une page/piste ; PARAM LIVE REC ; TRK SELECT ; échange de pistes. |
| **1.03A→1.10**, Bug fixes | Premier pattern/première ligne conservés lors du lancement d'une chaîne/song déjà en lecture ; retrig en jeu MIDI externe ; réglages CONTROL ALL retenus ; aucun lock RTRG résiduel après suppression/réinsertion d'un trig. |
| **1.10→1.10A**, Bug fixes | Préécouter un trig à retrig ne doit pas laisser le son jouer indéfiniment ; locks applicables/enlevables collectivement sur des pistes MIDI ; mutes SONG suivent un échange de pistes. |
| **1.10A→1.10B**, Bug fixes | Le lock RTRG doit survivre à la conversion **lock trig → note trig**. Cette conversion diffère de l'effacement puis de la réinsertion d'un trig. |
| **1.15→1.15A**, Bug fixes | Slices verrouillables en STEP avec le trig mode SLICES. La machine Slice est introduite dans **1.10B→1.15**. |
| **1.15C→1.16 ; 1.16→1.17** | Aucun nouveau contrat de séquenceur dans ces ajouts Outbox. La correction des messages MIDI à très faible BPM en 1.16 concerne la sortie MIDI, si ce workflow DAW est retenu. |

## 8. Tests de conformité à ajouter après une décision d'implémentation

Les tests existants couvrent la précision de l'horloge, le cas A:B simple,
probabilité nulle, répétitions uniformes, pitch/cutoff locks, microtiming,
stéréo, sécurité audio et rappel de l'état courant. `testBlockInvariance`
valide le déterminisme actuel ; `testHostTransport` attend l'extinction naturelle
après STOP et ne vérifie pas la coupure immédiate documentée. Aucun de ces
tests ne couvre GRID/LIVE/STEP, lock trigs, FILL/PRE/NEI, undo, banques,
chaînes/songs ou snapshots de performance.

| Cas | Actions et état/audio attendu | Référence |
|---|---|---|
| T-01 | Déclencher un long son ; STOP coupe son direct mais garde les retours ; double STOP les atténue brièvement. Pause/reprise garde sa position. Tester séparément l'adaptation STOP du host. | SQ-03 |
| T-02 | Longueurs de pistes 3 et 5, SPEED 1× et 3/4× ; RESET explicite puis INF ; CHANGE vers un pattern en attente. Vérifier positions, resets collectifs et moment de changement. | SQ-05–06 |
| T-03 | Étendre 16→32 puis 32→64 avec autocopy ON/OFF ; réduire/rétendre. Vérifier pas/locks et choisir explicitement le traitement des données hors longueur. | SQ-07 |
| T-04 | Note trig puis lock trig sans note, dont un lock sur un son tenu ; aucun redéclenchement d'enveloppe/échantillon pour le lock trig. Enlever/recréer une note efface les locks ; remplacer une note en STEP les conserve ; conversion lock→note conserve RTRG. | SQ-09, SQ-13, SQ-16, notes 1.10B |
| T-05 | LIVE MIDI avec vélocité/durée, quantize ON/OFF, puis automate d'un paramètre ; vérifier trigs/locks et modes overdub/locks sur trigs existants. Répéter sur une page bouclée sans toucher les autres pages. | SQ-11, SQ-14, notes 1.02 |
| T-06 | STEP STANDARD note/silence avancent d'un pas ; JUMP LEN 1/8 avance de deux. Réinsertion et navigation ne perdent pas les locks ni le pas courant. | SQ-12–13 |
| T-07 | A:B 1:2, 2:4 et inverse ; première/dernière lecture ; PRE et inverse PRE n'écrasent pas leur source ; NEI dépend de la piste précédente et vaut faux sans condition voisine. Inclure PROB/FILL et notes conditionnelles silencieuses. | SQ-19–23 |
| T-08 | Rejouer une boucle Live sur le même PPQ avec une graine de test fixe : constater de nouveaux tirages par visite, jamais par bloc. Les mêmes visites découpées en blocs différents donnent les mêmes décisions. Ne pas exiger que chaque paire de tirages diffère. | SQ-18 |
| T-09 | FILL tenu immédiat, armé pour un prochain cycle, puis verrouillé ; noter l'interaction avec GRID et les trigs OFF. | SQ-23 |
| T-10 | RTRG OFF/ON ; RATE 1/12, 1/24 et 1/32 ; plusieurs LEN/VFAD dont signes opposés, effet de VEL ; jeu clavier/MIDI externe ; lock de retrig enregistré en LIVE. Tester la règle d'arrêt après sa clarification. | SQ-24–25 |
| T-11 | Microtiming positif/négatif sauvegardé ; exemple +1/384, soit 1/24 de pas à vitesse 1× ; TRACK quantize réduit les offsets sur la seule piste active, PATTERN agit sur toutes les pistes. Multisélection et décalage d'un pas conservent les locks. | SQ-10, SQ-26–27 |
| T-12 | Copie de deux trigs distants de trois pas, collage à une autre position ; préserver distance et locks. Copier/effacer une page laisse les autres intactes ; répéter CLEAR/PASTE restaure l'état antérieur. | SQ-28–31 |
| T-13 | TEMP SAVE, tweaks/trigs, TEMP RELOAD ; sans snapshot, retour permanent. Refaire en PERFORM KIT, changer de pattern et recharger le kit ; distinguer sauvegarde Live et snapshot de performance. | SQ-32, SQ-36 |
| T-14 | Chaîne avec doublons et patterns de banques différentes ; démarrer pendant lecture sans sauter le premier. Song créé depuis cette chaîne, ligne répétée/écourtée, mutes/swing, boucle de ligne, END STOP/LOOP. | SQ-33–35, notes 1.10 |
| T-15 | Charger un état réellement produit par `751ebcb` ; valeurs, samples, locks et résultat de lecture legacy conservés. Ajouter les nouveaux champs, enregistrer/rappeler, puis tester un état incomplet/corrompu sans application partielle. | Compatibilité ci-dessous |

## 9. Compatibilité et limites de la spécification

Le format actuel utilise l'en-tête **`TAKTII_STATE_1`**, un seul `SEQUENCER`,
16 nœuds `TRACK` et 128 `STEP` par piste. `setStateInformation` les exige, avec
leurs samples incorporés. Les paramètres existants utilisent notamment
`tN_*`, `play`, `hostSync`, `tempo`, `swing`. Toute extension devra :

- continuer à lire les états de `751ebcb` et préserver identité VST3 et IDs ;
- fournir des défauts explicites aux champs absents, sans écraser les anciennes
  données, et migrer cet état vers le pattern initial approprié ;
- préserver le sens des anciennes automations, pas seulement leurs noms :
  changer la plage de `swing` ou transformer `retrigs` en RATE change le rendu ;
- distinguer format legacy et nouveaux comportements lorsque leur conversion
  exacte est impossible, notamment répétitions par compte versus RATE/LEN ;
- conserver l'import/rappel des samples dans les projets Live et un chargement
  atomique en cas de données invalides ;
- séparer snapshots temporaires, état permanent musical, état de travail et
  sauvegarde DAW, avec un contrat de persistance adapté au VST clairement décrit.

Les points suivants restent **à clarifier**, et ne sont pas présentés comme
des exigences démontrées par cette référence seule :

1. ordre exact PROB/FILL/COND et résultat mémorisé pour PRE/NEI ; état initial
   PRE, cas de la piste 1 et voisinage, événements simultanés ;
2. valeur neutre/non verrouillée de FILL : OFF signifie « hors FILL », et la
   figure TRIG PAGE 1 ne définit pas son enum/default ;
3. bornes et résolution exhaustive du microtiming : la figure +1/384 ne
   suffit pas à établir toutes les valeurs possibles ;
4. arrêt des retrigs selon LEN du trig, LEN de la courbe, Note Off, prochain
   trig, INF ; partage d'un tirage PROB entre les répétitions ;
5. règle des valeurs verrouillées entre deux événements sans redéclenchement,
   et effet exact de mute sur le jeu manuel/audition ;
6. SQ-31 suit la formulation de §10.8.5, qui mentionne COPY/PASTE/CLEAR,
   alors que §6.4 p.19 ne confirme l'annulation répétée que pour PASTE/CLEAR :
   ces deux dernières actions sont établies, l'effet d'un second COPY reste à préciser ;
7. adaptation DAW des sauts PPQ, boucles et STOP versus pause, priorité du tempo
   de Live face aux tempos de song, durée de vie des chaînes/snapshots.

Ces décisions peuvent être spécifiées et testées indépendamment du DSP du
matériel. Les fonctions musicalement absentes ne doivent pas être classées
hors périmètre au seul motif qu'Overbridge et les connexions physiques le sont.
