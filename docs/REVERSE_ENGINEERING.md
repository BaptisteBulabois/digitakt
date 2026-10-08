# Périmètre de la réimplémentation

La demande porte sur le Digitakt II, utilisé comme instrument VST3 sous Windows dans Ableton Live. Ce dépôt reconstruit un workflow de sampler/séquenceur à 16 pistes. Il n'extrait pas le firmware de la machine, et aucune machine physique n'était accessible pendant le développement.

La direction actuelle est définie dans [PRODUCT_SCOPE.md](PRODUCT_SCOPE.md) : fidélité de l'interface et du workflow, sans exigence d'identité sonore. Les mesures matérielles ne sont pas un prérequis. Overbridge et la gestion d'un appareil réel sont exclus du périmètre.

## Sources et niveau de certitude

- Produit de référence : https://www.elektron.se/en/digitakt-ii. Le site officiel et son CDN ont renvoyé HTTP 403 depuis cet environnement ; le manuel officiel n'a donc pas pu être consulté ici.
- Recherche indépendante d'architecture : https://github.com/lalzart/digitakt-ii-firmware-research-public. Les documents publics distinguent les observations d'architecture des hypothèses sur le DSP. Ils ne démontrent pas l'équivalence sonore d'une réimplémentation. Aucun code de firmware n'a été repris.
- Mapping de contrôleur indépendant : https://github.com/techronmic/Digitakt-II_NLCXL3. Il corrobore les pistes 1–16 et les familles de commandes sample/filtre/amplitude/effets ; ce n'est pas une spécification complète du produit.

Les valeurs et algorithmes ci-dessous sont des décisions de ce plugin, et non des constantes extraites d'Elektron.

## Moteur fourni

Le moteur C++ ne dépend pas de JUCE. Les buffers d'effets sont alloués dans `prepare`, pas pendant le rendu. Chaque piste a une voix stéréo monophonique, un lecteur à interpolation linéaire, une enveloppe attack/decay exponentielle, un filtre passe-bas à variables d'état, une saturation et une réduction de résolution. Le mix final utilise une saturation douce pour borner la sortie.

Un pas correspond à une double croche (0,25 noire). La probabilité dépend d'un hash déterministe de la piste et de l'indice absolu du pas : le même passage au même PPQ donne les mêmes décisions. Les conditions « tous les N cycles » utilisent la longueur de chaque piste. Les locks de hauteur et de filtre s'appliquent au trig courant ; le trig suivant sans lock retrouve les valeurs de base.

Le séquenceur calcule ses événements à partir de la position musicale du bloc. Il supporte des tailles de blocs différentes et les repositionnements du host. Les notes MIDI 36–51 sont un choix de routage du plugin, pas une revendication de compatibilité avec le mapping MIDI matériel.

Les contrôles de pattern et les références de samples sont transférés au moteur avec une acquisition non bloquante du verrou de contrôle ; une mise à jour peut être différée d'un bloc pendant une édition. Les anciens samples sont conservés puis libérés hors du rendu par l'interface. L'import et l'encodage de l'état se font hors du traitement audio. L'intégration n'est pas une garantie de temps réel dur et nécessite encore du profilage dans plusieurs DAW.

## Écarts restant à traiter

| Domaine | État |
| --- | --- |
| Machines avancées de lecture, slicing, time stretch/Werp | Non implémentées ; comportement et commandes à préciser dans le manuel |
| LFO et enveloppe de filtre | Non implémentés |
| Familles complètes de filtres | Passe-bas indépendant uniquement ; réglages et interactions à documenter |
| Locks de tous les paramètres et choix de sample par pas | Locks de pitch/cutoff uniquement |
| Conditions Fill/First/Previous/Neighbor | Condition de cycle et probabilité uniquement |
| Pistes MIDI externes, CC, MIDI output | Non implémentés ; entrée MIDI pour jouer les samples |
| Banques/projets, chaînes de patterns, song mode | Un pattern à 16 pistes rappelé avec l'état du plugin |
| Sampling direct dans le DAW | Non implémenté ; usage à définir |
| Sorties séparées et sidechain | Sortie stéréo principale uniquement |
| Transfert matériel, Overbridge, maintenance du firmware | Hors périmètre |
| Formats de projets Elektron et SysEx | Non pris en charge ; hors périmètre pour la connexion au matériel |
| Interface et précision des commandes | À confronter au manuel et aux notes de mise à jour fournis |

## Validation de l'interface et du workflow

Le manuel et les notes de mises à jour permettront de fixer une version cible et de décrire les pages, commandes et enchaînements d'actions. Chaque fonction sera reliée à une section de référence, puis vérifiée au niveau de l'état du séquenceur et de l'interface.

Les gestes physiques seront adaptés à la souris et au clavier en conservant leur effet musical. Les tests devront vérifier notamment les locks, conditions, opérations sur les patterns et le rappel des projets Live. Une comparaison sonore avec une machine réelle reste facultative.
