# Périmètre de la réimplémentation

La demande porte sur le Digitakt II, utilisé comme instrument VST3 sous Windows dans Ableton Live. Ce dépôt reconstruit un workflow de sampler/séquenceur à 16 pistes. Il n'extrait pas le firmware de la machine, et aucune machine physique n'était accessible pendant le développement.

La direction actuelle est définie dans [PRODUCT_SCOPE.md](PRODUCT_SCOPE.md) : fidélité de l'interface et du workflow, sans exigence d'identité sonore. Les mesures matérielles ne sont pas un prérequis. Overbridge et la gestion d'un appareil réel sont exclus du périmètre.

## Sources et niveau de certitude

- Sources principales : manuel officiel **OS 1.17** et notes des versions jusqu'à **1.17**, téléchargés et consultés le 8 octobre 2026. Les refus HTTP 403 rencontrés pendant le premier développement ne bloquent plus leur consultation. Les URL, dates et empreintes sont dans [reference/SOURCES.md](reference/SOURCES.md).
- Recherche indépendante d'architecture : https://github.com/lalzart/digitakt-ii-firmware-research-public. Les documents publics distinguent les observations d'architecture des hypothèses sur le DSP. Ils ne démontrent pas l'équivalence sonore d'une réimplémentation. Aucun code de firmware n'a été repris.
- Mapping de contrôleur indépendant : https://github.com/techronmic/Digitakt-II_NLCXL3. Il corrobore les pistes 1–16 et les familles de commandes sample/filtre/amplitude/effets ; ce n'est pas une spécification complète du produit.

Les valeurs et algorithmes ci-dessous sont des décisions de ce plugin, et non des constantes extraites d'Elektron.

Les audits de [l'interface](reference/UI_WORKFLOW_AUDIT.md), du
[séquenceur](reference/SEQUENCER_WORKFLOW_AUDIT.md) et des
[évolutions firmware](reference/FIRMWARE_AUDIT.md) comparent cette base aux
références officielles. Les priorités de correction sont dans
[WORKFLOW_SPEC.md](WORKFLOW_SPEC.md).

## Moteur fourni

Le moteur C++ ne dépend pas de JUCE. Les buffers d'effets sont alloués dans `prepare`, pas pendant le rendu. Chaque piste a une voix stéréo monophonique. Le chemin Legacy conserve le lecteur à interpolation linéaire, l'enveloppe attack/decay et les traitements des anciens projets. La branche 0.3 ajoute les machines de lecture, trois LFO par piste, les enveloppes AHD/ADSR, les filtres et le chorus décrits dans [WORKFLOW_0_3.md](WORKFLOW_0_3.md). Les algorithmes granulaires, les filtres et les courbes temporelles sont indépendants de ceux du matériel.

À vitesse normale, un pas correspond à une double croche (0,25 noire). Les anciens pas conservent leur probabilité déterministe et leurs retriggers simplifiés. Les nouveaux trigs utilisent des conditions A:B, PRE, NEI, FIRST, LAST et FILL, une probabilité par activation et un train de retriggers à débit musical. Les locks disponibles portent sur la hauteur, le filtre et la tranche ; les locks généralisés restent à compléter.

Le séquenceur calcule ses événements à partir de la position musicale du bloc. Il supporte des tailles de blocs différentes et les repositionnements du host. Les notes MIDI 36–51 sont un choix de routage du plugin, pas une revendication de compatibilité avec le mapping MIDI matériel.

Les contrôles de pattern et les références de samples sont transférés au moteur avec une acquisition non bloquante du verrou de contrôle ; une mise à jour peut être différée d'un bloc pendant une édition. Les anciens samples sont conservés puis libérés hors du rendu par l'interface. L'import et l'encodage de l'état se font hors du traitement audio. L'intégration n'est pas une garantie de temps réel dur et nécessite encore du profilage dans plusieurs DAW.

## Écarts restant à traiter

| Domaine | État |
| --- | --- |
| Machines de lecture, slicing, time stretch/Werp | Implémentées sur la branche 0.3 avec DSP indépendant ; formats et unités adaptés au VST |
| LFO et enveloppe de filtre | Trois LFO et enveloppe implémentés ; destinations entre LFO et key tracking à quatre destinations à compléter |
| Familles de filtres | Multimode, Lowpass 4, EQ, Comb−/+, Legacy et Prototype ; aucune équivalence sonore revendiquée |
| Locks de tous les paramètres et choix de sample par pas | Pitch/cutoff/slice disponibles ; généralisation et sample locks à développer |
| Conditions Fill/First/Previous/Neighbor | Implémentées en mode avancé ; règles CHANGE/RESET complètes à développer |
| Pistes MIDI externes, CC, MIDI output | Non implémentés ; entrée MIDI pour jouer les samples |
| Banques, chaînes de patterns, song mode | 128 patterns, chaînes de 64 entrées et 16 songs de 99 lignes ; presse-papiers des arrangements à compléter |
| Enregistrement LIVE/STEP et clavier chromatique | À développer ; GRID et déclenchement MIDI disponibles |
| Browser samples/presets/kits | Import de samples disponible ; browser et gestion complète des bibliothèques à développer |
| Effets | Delay/reverb historiques, chorus et routages ; compresseur et contrôles SEND FX restants à développer |
| Sampling direct dans le DAW | Non implémenté ; usage à définir |
| Sorties séparées et sidechain | Sortie stéréo principale uniquement |
| Transfert matériel, Overbridge, maintenance du firmware | Hors périmètre |
| Formats de projets Elektron et SysEx | Non pris en charge ; hors périmètre pour la connexion au matériel |
| Interface et précision des commandes | Panneau fondé sur le SVG fourni ; pages contextuelles et adaptations à la souris décrites dans le guide 0.3 |

## Validation de l'interface et du workflow

Le manuel OS 1.17 et les notes de mises à jour fixent désormais la cible documentaire. Les audits relient les pages, commandes et enchaînements d'actions aux sections de référence. Leur implémentation devra être vérifiée au niveau de l'état du séquenceur et de l'interface ; l'analyse documentaire ne constitue pas une validation du comportement du plugin.

Les gestes physiques seront adaptés à la souris et au clavier en conservant leur effet musical. Les tests devront vérifier notamment les locks, conditions, opérations sur les patterns et le rappel des projets Live. Une comparaison sonore avec une machine réelle reste facultative.
