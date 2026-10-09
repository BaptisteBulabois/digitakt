# Références du lot suivant après 0.2.0

Recherche effectuée le 9 octobre 2026. Ce document précise les règles utiles
au développement ; il ne constitue pas une liste de fonctionnalités livrées.
La cible reste le manuel **Digitakt II OS 1.17**, 118 pages, et les notes
officielles jusqu'à 1.17. Versions, URL et empreintes sont consignées dans
[SOURCES.md](SOURCES.md). Les numéros ci-dessous sont ceux imprimés du manuel.

## Contrôle collectif

Les sections 6.2.2, p. 19, et 9.8.1, p. 37, décrivent le geste suivant :
maintenir TRK en modifiant un paramètre applique cette modification aux pistes
audio autorisées par CONTROL ALL CONFIG. La piste active est toujours incluse,
même si elle est exclue de cette sélection. Les pistes MIDI sont exclues.
NO avant de relâcher TRK annule les modifications du geste.

Le texte ne définit pas précisément le traitement des valeurs initiales
différentes : même valeur absolue ou même déplacement relatif. Une adaptation
doit être explicite et testée, sans attribuer une formule non documentée au
matériel. Le masque d'inclusion et l'annulation restent des règles confirmées.

## Modes d'enregistrement et types de trigs

Source : sections 10.2–10.2.4, pp. 41–44, 10.8.1, p. 47, et préférences
14.7.7–14.7.8, p. 82.

- Un **note trig rouge** déclenche une note ; un **lock trig jaune** déclenche
  les valeurs verrouillées sans créer de note. Un lock trig ne doit pas devenir
  une note silencieuse dont le volume serait simplement zéro.
- GRID : REC active/désactive le mode ; pad ajoute une note, FUNC + pad ajoute
  un lock trig. Une pression brève sur un trig existant l'enlève ; le maintien
  prépare l'édition. Retirer puis remettre un trig efface ses locks. Le maintien
  d'un trig + YES préécoute ce trig avec ses locks.
- LIVE : REC + PLAY active le mode et lance le séquenceur. PLAY quitte LIVE
  en gardant la lecture ; REC passe en GRID ; STOP arrête lecture et
  enregistrement. Les notes jouées, leur vélocité et leur durée peuvent être
  enregistrées ; les mouvements des encodeurs deviennent des locks.
- LIVE sans quantification conserve le décalage temporel. PARAM LIVE REC permet
  de choisir entre tous les pas et uniquement les pas avec trig existant.
  LIVE REC OVERDUB permet l'ajout aux trigs existants au lieu du remplacement.
  Ces préférences empêchent de présenter une seule politique d'écrasement
  comme universelle.
- STEP : REC + STOP entre dans le mode ; REC + deux pressions STOP bascule
  STANDARD/JUMP. Un pad choisit le pas actif ; FUNC + pad de piste écrit une
  note puis avance. Le clavier chromatique et l'entrée MIDI constituent les
  autres chemins d'entrée. NO supprime/pose un silence puis avance.
- STANDARD avance d'un pas. JUMP avance selon TRIG LEN et verrouille LEN :
  1/16 = un pas, 1/8 = deux pas, 1/4 = quatre pas. Une nouvelle note sur un
  note trig existant conserve ses locks.
- STEP réaffecte FUNC + YES/NO à l'enregistrement : ces gestes ne doivent pas
  provoquer une sauvegarde/restauration temporaire du pattern.

Les mappings souris/clavier, la quantification et le chemin MIDI du VST doivent
être expliqués comme adaptations. Aucun raccordement à une machine n'est
nécessaire pour implémenter ces fonctions musicales.

## Conditions et FILL

Source : pp. 48–49, intitulé imprimé « 10.7.3 » dans le corps du manuel.
Les inversions graphiques des noms sont parfois perdues par `pdftotext` ; un
second PRE/NEI/1ST/LST/A:B dans l'extraction désigne leur version inversée.

PRE/NOT PRE prennent le dernier résultat conditionnel pertinent de la même
piste. Ces conditions PRE sont elles-mêmes ignorées dans cette mémoire.
NEI prend le résultat pertinent de la **piste précédente**, pas la suivante ;
sans trig conditionnel sur ce voisin, NEI est faux. A:B compte les répétitions
de piste si la piste est plus courte que le pattern : 2:4 joue aux passages
2, 6, 10, etc. Le cycle continue jusqu'à l'arrêt du séquenceur. 1ST n'est vrai
que pendant le premier passage. LST dépend d'un changement de pattern annoncé,
donc ne peut être fidèle sans mécanisme de changement de pattern.

FILL a son propre paramètre sur TRIG, distinct de COND. YES + PAGE prépare un
cycle FILL au prochain bouclage ; PAGE maintenu active FILL immédiatement hors
GRID ; un geste PAGE + YES avec relâchement de PAGE en premier le verrouille.
Un bouton FILL VST explicite peut adapter ces gestes.

## Trois LFO audio

Source : sections 11.9–11.11, pp. 58–60, et annexe C, p. 114. Le manuel
décrit trois LFO audio, deux seulement pour une piste MIDI. Les huit réglages
sont SPD, MULT, FADE, DEST, WAVE, SPH/SLEW, MODE et DEP.

| Réglage | Règle confirmée |
| --- | --- |
| SPD | Bipolaire ; valeurs négatives parcourent la forme à l'envers |
| MULT | Multiplication liée au tempo ou indépendante, basée sur 120 BPM |
| FADE | -64 à 63 ; positif = extinction, négatif = apparition, zéro = aucune |
| WAVE | TRI, SINE, SQR, SAW, RND bipolaires ; EXPO et RAMP unipolaires |
| SPH | 0 = début du cycle, 64 = milieu ; RND remplace SPH par SLEW |
| MODE | FRE continu ; TRG redémarre ; HLD phase continue mais valeur tenue au trig ; ONE s'arrête à la fin ; HLF au milieu |
| DEP | Bipolaire, zéro = pas de modulation |

La table p. 60 donne un cycle de `2048 / (abs(SPD) × MULT)` pas de
séquenceur. À quatre pas par noire, cela correspond à une fréquence signée
`SPD × MULT × BPM / 30720` Hz ; la variante indépendante remplace BPM par 120.
Cette formule est déduite de la table, pas extraite du DSP Elektron. SPD = 0
est adapté à une phase immobile. Les multiplicateurs listés vont de 1 à 2048.
Le texte ne précise pas ici les bornes complètes de SPD/DEP/SPH ni la courbe
temporelle exacte de FADE ou SLEW : leurs choix DSP doivent rester identifiés
comme approximations indépendantes.

L'annexe C autorise aussi des destinations FX et des modulations de LFO :
LFO 2/3 vers LFO 1 et LFO 3 vers LFO 2, sans retour inverse ni automodulation.
Le résumé SRC/FLTR/AMP p. 58 ne constitue donc pas la liste complète.
TRIG LFO.T p. 53 contrôle le déclenchement des LFO à la note.

**KEY TRACKING** est une source distincte, décrite p. 39 : jusqu'à quatre
destinations SRC/FLTR/AMP/FX/MOD, avec des offsets issus de NOTE ou du MIDI
entrant. Ce n'est pas un sixième mode de déclenchement du LFO.

## Machines SRC, Grid et Slice

Source : annexe A.2, pp. 93–101, et clavier p. 26.

- Oneshot : STRT + LEN détermine la fin de la portion du sample, LOOP le point
  de retour. PLAY offre forward/reverse, chacun avec ou sans boucle. Les boucles
  restent contraintes par TRIG LEN et l'enveloppe AMP HLD/DEC.
- Werp : fragments temporels alignés sur le tempo ; SEG règle leur taille et
  MODE leur sens/boucle individuellement. BARS décrit la durée musicale.
- Stretch : grains avec fondus ; TUNE change la hauteur, BARS décrit la durée.
- Repitch : la vitesse suit le tempo et modifie aussi la hauteur ; la page
  n'offre pas de TUNE indépendant. Garder un TUNE actif sous ce nom serait
  trompeur pour cette machine.
- Grid : parts égales sur le sample entier ; GRID règle leur nombre. SLICE
  choisit la première part, LEN le nombre de parts jouées consécutivement.
- Slice : points éditables de début, fin et boucle ; SLICE = 2 et LEN = 3
  signifie début de tranche 2 jusqu'à fin de tranche 4. Les points sont
  sauvegardés avec le preset. Les fins/débuts adjacents sont liés par défaut.
- Slice/GRID en SLICE = NOTE jouent les tranches depuis C1, avec retour au
  début après la dernière tranche. Le manuel ne donne pas dans ce passage la
  valeur MIDI entière correspondant à son C1. L'affichage d'octaves varie entre
  DAW : documenter la valeur choisie et préserver les anciennes notes du VST.
- SRC + YES ouvre l'allocation Grid ou le menu Slice. LINEAR LOCKS assigne les
  tranches aux note trigs existants à partir de la première ; RANDOM LOCKS
  remélange à chaque validation. L'allocation ne crée pas de nouvelles notes.

Dans l'éditeur Slice, E = début, H = fin, D = boucle, A = lien ; FUNC + E/H/D
accroche les passages par zéro ; FUNC + G déplace début/fin ensemble ; F zoome
horizontalement, FUNC + F verticalement ; G déplace la vue. Gauche/droite
choisit une tranche ; FUNC + droite la divise, FUNC + gauche supprime son aire,
sans supprimer les données audio ni modifier les autres tranches. FUNC + YES
préécoute. Les menus peuvent créer une grille et proposer une détection des
transitoires, mais son algorithme propriétaire n'est pas spécifié.

## Recherche de vidéos : accès vérifié

Le 9 octobre 2026, une requête publique à la recherche YouTube a échoué avec
`Tunnel connection failed: 403 Forbidden`. Aucun visionnage ni transcript de
vidéo n'a été obtenu. La [page officielle Digitakt II](https://www.elektron.se/explore/digitakt-ii)
a répondu HTTP 200 ; son HTML expose les titres et URL des vidéos intégrées :

- [Digitakt II — At A Glance](https://www.youtube.com/watch?v=QuBtS8EeSHY).
- [Slice on Digitakt II — OS 1.15 Upgrade](https://www.youtube.com/watch?v=DjnZyjZngXo).

Ces titres et liens sont les seules observations faites sur ces vidéos. Aucun
comportement du plugin n'est validé par leur contenu. Ils serviront de points
de comparaison si un accès vidéo ou des captures deviennent disponibles.

## Ressources facultatives à demander à l'utilisateur

La documentation disponible suffit pour avancer sans machine réelle. Les
ressources les plus utiles seraient deux ou trois samples qu'il utilise : une
boucle de batterie avec tempo et nombre de mesures connus, un breakbeat avec
transitoires irréguliers et une note tenue pour tester les LFO. Un petit projet
Live avec automation et sauvegarde/réouverture aiderait à vérifier le rappel.
Des captures ou timestamps précis de tutoriels restent facultatifs pour les
détails visuels ambigus. Aucun firmware, connexion USB ou enregistrement d'une
machine Digitakt réelle n'est requis.
