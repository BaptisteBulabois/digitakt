# Sources de la reproduction du workflow

Documents récupérés et consultés le **8 octobre 2026**. La cible documentaire
est le **Digitakt II OS 1.17**. Les citations des audits utilisent les numéros
de page imprimés du manuel, pas les numéros de lignes de son extraction.

## Archive fournie

`Digitakt_II_Codex_Pack.zip` contient un README, deux raccourcis URL, un index
des firmwares, un script Python et une liste de sources. Elle contient des
liens vers les documents, **aucun PDF**. Ses résumés et ses instructions ne
remplacent ni les documents officiels ni les choix de produit de l'utilisateur.
Le script joint a été lu ; les téléchargements ont été réalisés séparément.

## Documents officiels

| Document | Version et date | Utilisation |
| --- | --- | --- |
| [Digitakt II User Manual](https://www.elektron.se/wp-content/uploads/2026/10/Digitakt-2-User-Manual_ENG_OS1.17_260930.pdf) | OS 1.17 ; mise à jour indiquée dans le manuel : 24 septembre 2026 ; 118 pages | Navigation, structure des données, modes, séquenceur, pages et machines |
| [Digitakt II OS Release Notes](https://www.elektron.se/release-notes/digitakt-ii-os-release-notes) | Versions 1.00–1.17 ; page mise à jour le 1er octobre 2026 | Ajouts musicaux et résultats corrigés à conserver |

Le suffixe `260930` de l'URL du PDF diffère de la date imprimée dans le manuel.
Il n'est pas utilisé comme date de son contenu. La page des notes est mutable :
les observations se rapportent à la copie consultée à la date ci-dessus.

Empreintes SHA-256 des fichiers téléchargés :

```text
manual-os1.17.pdf
a5c12b2b692311828f78fad18008dcda96bf26dd50e117bda5d735794e0bbd06

release-notes.html
63e87a6af3c836682653d0004e53d46b2b53c5b7f7a30872a138a774dae00472
```

## Copies de travail et restitution

Les copies PDF/HTML et leurs extractions texte sont conservées localement dans
`.reference/elektron/`, exclu de Git. Elles ne font pas partie du code MIT du
projet. Le dépôt publie nos synthèses et des liens vers les sources officielles.

L'extraction du PDF utilise `pdftotext -layout`. Les tableaux, valeurs encadrées
et pictogrammes peuvent nécessiter une vérification visuelle ; les audits doivent
signaler les informations qui ne peuvent pas être confirmées dans le texte.

## Analyses produites

- [Interface, navigation et données](UI_WORKFLOW_AUDIT.md).
- [Séquenceur et édition](SEQUENCER_WORKFLOW_AUDIT.md).
- [Historique firmware et régressions](FIRMWARE_AUDIT.md).
- [Spécification et ordre d'implémentation](../WORKFLOW_SPEC.md).

Les sources indépendantes utilisées pour le premier prototype restent décrites
dans [REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md). Le manuel et les notes
officielles priment désormais pour définir les comportements musicaux. Aucun
firmware ni banque sonore Elektron n'est extrait ou utilisé.
