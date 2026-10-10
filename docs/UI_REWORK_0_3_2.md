# Interface 0.3.2 : panneau rapproché du Digitakt II

Cette révision corrige les débordements de la version 0.3.1 et rapproche
l'organisation visuelle du panneau du Digitakt II. Elle conserve les 16 pistes
audio et les fonctions musicales déjà présentes. Le développement reste sur
`development/0.3-machines-modulation` ; `main` ne fait pas partie de ce lot.

## Références et limites

La disposition s'appuie sur le **manuel officiel Digitakt II OS 1.17** :

- **§3.1, pages 12–13** : dessin du panneau, positions des commandes, touches
  de transport, fonctions secondaires et indicateurs de page.
- **§6.2, page 18** : les paramètres affichés sur l'écran correspondent aux
  positions physiques des codeurs DATA ENTRY.
- **§18, page 91** : écran OLED 128 × 64 et dimensions 215 × 176 mm du boîtier.

Les documents et leur provenance sont décrits dans
[reference/SOURCES.md](reference/SOURCES.md). Le SVG groupé fourni par
l'utilisateur complète cette référence ; ses proportions et ses annotations
restent approximatives. Le dessin officiel prime lorsqu'ils divergent.

L'interface garde l'identité **Takt II**. Il s'agit d'une adaptation à la souris,
au clavier et au VST3, sans certification de reproduction exacte du matériel.
La révision visuelle ne transforme pas les fonctions encore absentes en
fonctions implémentées. Par exemple, la présence d'une touche KEYBOARD ou
d'une inscription PRESET POOL ne signifie pas que ces menus sont disponibles.
Les fonctions de connexion matérielle restent hors périmètre.

## Changements réalisés

| Zone | Comportement de la version 0.3.2 |
| --- | --- |
| Boîtier | Panneau de 836 × 684 unités, rapport 1,222 proche des 215 × 176 mm documentés ; couleur sombre et contraste des bordures réduits. |
| Codeurs A–H | Deux rangées de quatre, avec uniquement leurs lettres sur le panneau. Les noms et valeurs des paramètres sont affichés dans l'OLED ; une valeur apparaît également au survol ou pendant le réglage. |
| Volume / LEVEL | Boutons rotatifs sans champs numériques permanents ; valeur disponible au survol. |
| Écran | Zone OLED de 256 × 128 unités, rapport 2:1 ; typographie à chasse fixe, paramètres en deux rangées correspondant aux codeurs. Le nom du sample reste dans l'écran. |
| Import SRC | Bouton IMPORT/CANCEL situé sous l'écran, à l'intérieur de son cadre ; il ne dépasse plus à droite des codeurs. |
| Transport | Symboles cercle, triangle et carré pour REC, PLAY et STOP, avec COPY/CLEAR/PASTE comme inscriptions secondaires. |
| Menus | Pictogrammes pour preset/kit, réglages, sampling, tempo et clavier, avec légendes secondaires sous les touches ; les fonctions absentes restent désactivées. |
| Pages du séquenceur | Huit indicateurs ronds en deux rangées de quatre au-dessus de PAGE. Ils restent cliquables pour sélectionner directement une page ; un contour indique la page jouée. |
| Pages de paramètres | TRIG, SRC, FLTR, AMP, FX et MOD avec état actif rouge/orange ; fonctions secondaires regroupées sous les touches. |
| Outils VST | Largeur des champs numériques adaptée à la largeur réelle de leur composant, au lieu d'un champ fixe trop large. |
| Panneaux superposés | Les commandes recouvertes sont masquées pour éviter les interactions à travers le panneau. NO reste visible et accessible, y compris dans l'aide. |
| Bande logicielle | Sélecteurs de piste, import, audition, synchronisation host, outils et aide conservés sous le boîtier, dans une bande plus compacte. |

La suppression des champs numériques autour des codeurs répond directement
au défaut de la version précédente : les codeurs de droite et le bouton
d'import empiétaient sur la bordure du boîtier. Les noms et valeurs sont
désormais regroupés dans l'écran, conformément à la hiérarchie de la machine.

Les grands groupes conservent l'ordre documenté : volume et LEVEL à gauche,
écran au-dessus des menus, matrice A–H à droite, navigation au centre, puis
16 touches TRIG en deux rangées. La colonne gauche suit FUNC, KEYBOARD, TRK,
PTN et SONG.

## Vérification

La lecture des coordonnées confirme que les commandes du panneau restent
dans les limites du boîtier et que l'OLED conserve son rapport 2:1. Les
coordonnées des centres de volume, LEVEL et A–H suivent les proportions du
dessin officiel ; elles ne constituent pas des mesures physiques certifiées.

La validation d'exécution, les captures aux différentes tailles, le chargement
du VST3 et le build Windows sont consignés séparément dans
[VALIDATION.md](VALIDATION.md). Cette note décrit les changements du code ;
elle n'affirme pas à elle seule que ces contrôles ont réussi ni qu'un essai
dans Ableton Live a été réalisé pour la version 0.3.2.
