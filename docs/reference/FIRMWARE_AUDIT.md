# Audit des évolutions firmware du Digitakt II

Analyse documentaire du 8 octobre 2026, pour l'interface et le workflow musical
du VST3 Windows x64 dans Ableton Live. Ce document décrit la cible ; il ne
certifie pas que ces fonctions sont déjà implémentées.

## Sources effectivement lues

- [Manuel officiel OS 1.17](https://www.elektron.se/wp-content/uploads/2026/10/Digitakt-2-User-Manual_ENG_OS1.17_260930.pdf),
  dont le texte indique une mise à jour le 24 septembre 2026. Sections utilisées :
  6.2.2, 8.7, 9.2–9.8, 10.2–10.8, 13.3/13.6, 14.7 et annexe A.2.5.
- [Notes officielles des versions](https://www.elektron.se/release-notes/digitakt-ii-os-release-notes),
  page mise à jour le 1er octobre 2026, transitions de 1.00 vers 1.17.
  Les références de version ci-dessous renvoient aux rubriques « List of changes »
  de cette page. Elle ne fournit pas de dates individuelles pour ces versions.

Les copies consultées sont `.reference/elektron/manual-os1.17.pdf`, son extraction
texte et `release-notes.html`/`release-notes.txt`. L'archive fournie contient des
liens, un index et un script de téléchargement ; elle ne contient pas les documents
officiels eux-mêmes. Ses résumés ne remplacent pas les sources ci-dessus.

## Cible retenue

**OS 1.17** est la référence documentaire actuelle. Le cœur musical inclut les
ajouts de 1.10, la machine Slice de 1.15 et les corrections ultérieures.
Les ajouts de 1.16/1.17 portent surtout sur Outbox 8 et n'imposent pas de menu
Outbox dans le plugin. La correction MIDI à très bas tempo de 1.16 reste pertinente
si un usage MIDI dans le DAW est retenu.

## Chronologie utile au produit

| Version arrivée | Évolution documentée et conséquence pour le VST |
| --- | --- |
| 1.00 | Point de départ des notes ; aucune rubrique autonome ne décrit exhaustivement le lancement. Les fonctions de base doivent être établies avec le manuel actuel. |
| 1.01 | Boucle de pages en GRID RECORDING, chaînage rapide dans une banque, menu PERSONALIZE, PAGE AUTOCOPY désactivable, U/D KEY MODE, menu SETUP regroupé, états visuels des mutes préparés et améliorations de navigation/préécoute. À intégrer aux interactions musicales. |
| 1.01A | Correction du canal audio droit sur certaines unités : problème matériel, aucune interaction à reproduire. |
| 1.02 | NOTE PARAM choisit la relation entre édition chromatique et gamme ; le mixeur affiche le nom de page. La boucle de pages distingue position maître et page jouée. Corrections importantes des retrigs enregistrés, de la navigation, des préécoutes et du rappel MIDI. |
| 1.03 | Support Overbridge 2.13 : hors périmètre. |
| 1.03A | Correction du sampling pouvant interrompre l'audio ; à considérer seulement si un enregistreur DAW est ajouté. L'autre correction concerne Overbridge. |
| 1.10 | Mise à jour majeure : Comb+, routage distorsion/filtre Base-width configurable, KEY TRACKING à quatre destinations, nouvelle courbe VELOCITY TO VOL ; chargement direct d'un sample sur une piste, nom de son éditable, Track Swap, overdub, locks en masse, TRACK SELECT et options d'enregistrement des locks. Navigation MOD, recherche et persistance améliorées. L'ajout de sampling mono ne signifie pas que les fichiers mono étaient auparavant interdits. |
| 1.10A | Corrections de sauvegarde des kits, locks en masse sur pistes MIDI, retrait de ces locks, mutes SONG lors de Track Swap, sélection/préécoute des samples et noms de presets. Corriger ces résultats, sans reproduire les anomalies. |
| 1.10B | Les locks RTRG doivent survivre à la conversion lock trig → note trig ; l'allongement d'un pattern ne doit pas bloquer l'interface. Correction d'un gel sur le canal MIDI FX. |
| 1.15 | Ajout de la machine **Slice**. La vérification/maintenance du filesystem exécutée au redémarrage ne relève pas du plugin. |
| 1.15A | Locks de slices en STEP RECORDING, modulation PLAY MODE et manipulation d'un éditeur sans slices corrigés. Corrections de sauvegarde/navigation ; migration de presets DT1 et contrôle physique du filesystem à traiter séparément du workflow VST. |
| 1.15B / 1.15C | Support de changements de production : aucun nouveau comportement musical documenté. |
| 1.16 | Configuration Outbox 8 exclue ; correction d'émission MIDI à très bas BPM à conserver si le MIDI externe est implémenté. |
| 1.17 | Routage Main vers Outbox 8 et navigation entre ses canaux : hors périmètre. |

## Exigences de workflow issues de 1.10 et du manuel 1.17

- **Locks en masse** : maintenir un trig existant puis PAGE affecte les trigs
  actifs de la page ; utiliser TRK étend l'édition aux trigs actifs de la piste.
  Ne pas créer des trigs sur les pas vides. La sélection d'un sample pendant un
  lock ne doit pas modifier le sample de base de la piste. Sources : ajout 1.10,
  corrections 1.10A et 1.01, manuel §10.8.1, p.47.
- **TRK SELECT** : hors mode d'enregistrement, NORMAL sélectionne et joue ;
  SILENT sélectionne sans jouer ; MANUAL joue sans sélectionner. INVERTED
  sélectionne sans jouer et TRK+TRIG joue sans sélectionner. Dans les trois
  premiers modes, TRK+TRIG sélectionne sans jouer. Source : ajout 1.10,
  manuel §14.7.9, p.82.
- **Control All** : agit sur les pistes audio autorisées dans KIT > CONTROL ALL
  CONFIG et toujours sur la piste active ; les pistes MIDI sont exclues.
  NO avant de relâcher TRK annule la modification. La configuration appartient
  au kit et doit être rappelée. Les notes 1.10 corrigent sa persistance et la
  cohérence de positions ; elles ne présentent pas sa création comme un ajout.
  Sources : manuel §§6.2.2 et 9.8.1, p.19/37 ; corrections 1.10.
- **Track Swap** : échange réglages, presets et données du séquenceur entre deux
  pistes ; les mutes SONG suivent l'échange. Source : ajout 1.10,
  correction 1.10A, manuel §9.8.1, p.37.
- **Live Recording** : distinguer LIVE REC OVERDUB, qui conserve les notes/locks
  déjà placés, et PARAM LIVE REC, qui limite éventuellement les locks aux pas
  contenant déjà un trig. Sources : ajout 1.10, manuel §§14.7.7–8, p.82.
- **Browser** : chargement direct depuis le +Drive sur une piste, préécoute
  manuelle même avec auto-preview, retour au dossier racine puis fermeture,
  mémorisation du dernier dossier et affichage du filtre de recherche actif.
  Les banques et dossiers doivent rester navigables pendant un sample lock.
  Sources : ajouts/corrections 1.01, 1.02, 1.10 et 1.10A ; manuel §§9.2–9.3,
  13.3 et 13.6. Adapter +Drive à une bibliothèque locale, sans simuler Transfer.

## Slice : apport musical de 1.15

Le manuel §A.2.5, p.97–100, définit une machine distincte de Grid : points de
début, fin et boucle éditables, grille initiale avec détection optionnelle de
transitoires, puis locks linéaires ou aléatoires sur les note trigs existants.
Les limites adjacentes sont liées par défaut et peuvent être dissociées ; division
et suppression d'une slice ne doivent pas supprimer l'audio. Les points sont
enregistrés avec le preset. Le mode SLICES expose les slices par groupes de 16
avec PAGE ; SLICE=NOTE permet leur sélection par clavier/MIDI, à partir de C1
et en revenant au début après la dernière slice (§8.7 et A.2.5).

La correction 1.15A exige que les locks Slice fonctionnent aussi en STEP
RECORDING et que l'éditeur reste utilisable sans slices. Elle ne justifie pas
d'émuler un crash ni de bloquer l'édition. La fidélité demandée concerne ces
gestes et états ; l'algorithme sonore ou la détection de transitoires peut être
une implémentation indépendante.

## Corrections à convertir en scénarios de validation

| Action | Résultat attendu et référence firmware |
| --- | --- |
| Combiner PRE/NEI avec PROB/FILL ; appliquer du swing à une séquence euclidienne | Conditions et swing compatibles, selon le manuel actuel ; corrections 1.01. |
| Boucler une page puis enregistrer ; ouvrir/fermer KEYBOARD SETUP en STEP RECORDING | Aucun pas d'une autre page ni pas actif effacé implicitement ; corrections 1.02. |
| Geler une séquence euclidienne MIDI ; convertir un lock trig en note trig | Conserver les locks de notes supplémentaires et RTRG ; corrections 1.10 et 1.10B. |
| Supprimer puis replacer un trig ; préécouter un trig avec retrigs | Locks RTRG supprimés avec le trig ; préécoute sans note infinie ; corrections 1.10/1.10A. |
| Changer de piste ou de machine avec une liste MOD ouverte | Liste fermée ou rafraîchie selon l'action, sans commande reportée par erreur sur la sous-page ; corrections 1.10. |
| Sauver/rappeler kit, preset, pattern et réglages de performance | Noms, références de samples, destinations LFO et configuration Control All conservés ; corrections 1.01, 1.02, 1.10/1.10A. Préserver aussi les anciens états Live du plugin. |
| Démarrer une chaîne ou un SONG pendant la lecture | Première entrée jouée ; ne pas sauter le premier pattern/la première ligne ; correction 1.10. |

Ces scénarios sont des exigences documentaires, pas des tests déjà exécutés.
Les bugs historiques servent à préciser le résultat corrigé. En cas d'ambiguïté,
utiliser la règle du manuel 1.17 et signaler l'adaptation souris/clavier séparément.

## Exclusions et décisions encore nécessaires

Outbox 8, Overbridge, USB audio vers le matériel, installation/downgrade d'OS,
processus de production, Transfer et maintenance du filesystem/+Drive sont
hors périmètre. Les corrections de clics, bruit ou courbes DSP ne constituent
pas une exigence de reproduction sonore exacte.

Le sampling et l'émission MIDI restent des fonctionnalités musicales possibles
uniquement si leur usage dans Ableton est défini. L'import de samples locaux,
la bibliothèque, les presets et le rappel Live restent nécessaires ; exclure
Transfer ne les exclut pas. Les limites physiques de stockage et l'import des
projets/presets DT1 ne deviennent pas automatiquement des contraintes du VST.
