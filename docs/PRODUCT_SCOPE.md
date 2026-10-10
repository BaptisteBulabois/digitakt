# Direction du projet

Décisions confirmées par l'utilisateur le 8 octobre 2026.

## Objectif

Reproduire avec précision l'interface et le workflow musical du **Digitakt II**
dans un instrument **VST3 Windows x64 pour Ableton Live**. Les interactions,
la navigation, les commandes et les règles du séquenceur sont les critères
principaux de fidélité.

La précision sonore par rapport au matériel n'est pas une exigence. Le plugin
doit permettre d'importer les samples de l'utilisateur ; les traitements audio
peuvent rester des implémentations indépendantes.

## Priorités

- Organisation des pages et paramètres, sélection des pistes et des pas,
  retours visuels et manipulation des commandes.
- Édition du séquenceur : trigs, parameter locks, conditions, microtiming,
  retriggers, swing, longueur et échelle des pistes/patterns.
- Gestion musicale des samples, sons, patterns et projets selon les fonctions
  décrites dans la documentation de référence.
- Opérations de copie, collage, effacement, sauvegarde et restauration lorsque
  ces comportements sont documentés.
- Adaptation des gestes physiques à la souris et au clavier, avec des actions
  équivalentes et une aide explicite.
- Intégration DAW : transport/tempo du host, automation, entrée MIDI,
  sauvegarde des samples et rappel des projets Live.

## Hors périmètre

Overbridge, connexion USB à une machine Elektron, transfert vers le matériel,
mise à jour de son firmware et autres fonctions de gestion d'un appareil réel.
Ces éléments ne sont pas des fonctionnalités manquantes à implémenter.

Les fonctions purement musicales doivent être évaluées séparément : un LFO,
le slicing ou l'édition de patterns restent pertinents même sans appareil réel.
Le sampling ou le MIDI externe ne seront ajoutés que si un usage dans le DAW
est identifié ; ils ne sont pas requis pour relier une machine physique.

## Base à préserver

Le commit publié `751ebcb` représente la première version. L'utilisateur a
validé son installation comme VST3 dans Ableton Live, sans encore confirmer
tous les comportements musicaux.

Le 10 octobre 2026, l'utilisateur a retiré l'exigence de compatibilité avec
les versions précédentes. Le développement vérifie la sauvegarde, la
réouverture et le rendu de la version actuelle, sans imposer le rappel ni
la parité audio des anciennes versions. L'identité du plugin reste inchangée
dans ce lot. Les corrections sont réalisées sur la branche de développement
0.3 ; `main` reste intact.

## Références reçues et cible

Le pack `Digitakt_II_Codex_Pack.zip` fourni par l'utilisateur contient des liens
et un script de récupération. Les documents officiels ont été téléchargés et
lus le 8 octobre 2026 : manuel **OS 1.17**, 118 pages, et notes des versions
jusqu'à **1.17**. Leurs versions, dates et empreintes sont consignées dans
[reference/SOURCES.md](reference/SOURCES.md).

La cible documentaire est **OS 1.17**, y compris les interactions ajoutées
en 1.10 et la machine Slice introduite en 1.15. Les ajouts Outbox des versions
1.16/1.17 restent hors périmètre. Les consignes de maintenance contenues dans
les sources sont des informations sur la machine, pas des actions demandées
pour cet environnement.

Les audits distinguent comportement documenté, état réel du prototype et
adaptation proposée au VST. Le développement suit la
[spécification de workflow](WORKFLOW_SPEC.md) ; une exigence documentée ne doit
pas être présentée comme une fonctionnalité déjà livrée.

## Validation

Construire une correspondance entre les fonctions documentées et leur
implémentation. Vérifier les enchaînements d'actions, les valeurs et l'état
obtenu après chaque opération. Les comparaisons sonores avec une machine
réelle sont facultatives et ne conditionnent pas l'avancement du projet.
