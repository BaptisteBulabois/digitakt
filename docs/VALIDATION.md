# Validation

Validation réalisée dans l'environnement cloud Linux avec JUCE 8.0.6 :

- 12 suites DSP : lecture stéréo, rééchantillonnage, 128 pas, swing, microtiming, retriggers, conditions, locks, filtre, enveloppe, effets, automation et absence d'allocation pendant le rendu ;
- tests du processeur : les 16 pistes, notes MIDI 36–51, import WAV mono/stéréo, paramètres, transport host et tailles de bloc variables ;
- sauvegarde/rappel : 16 samples, 128 pas par piste, longueurs, paramètres, rejet d'états corrompus et valeurs non finies ;
- chargement du bundle VST3 réel dans un host JUCE, rendu MIDI stéréo, automation et rappel d'état ;
- rendu hors ligne de quatre mesures à 120 BPM, stéréo 48 kHz ;
- création et rendu hors écran de l'interface du plugin.

Commandes principales :

```bash
bash scripts/build.sh
ctest --test-dir build --output-on-failure
```

Le workflow GitHub Actions a compilé et testé la cible Windows avec Visual
Studio (run `37825987059`). L'utilisateur a confirmé l'installation du VST3
dans Ableton Live ; les scénarios musicaux détaillés restent à vérifier dans Live.

## Analyse documentaire OS 1.17

Les audits du 8 octobre 2026 comparent le code du prototype aux documents
officiels récupérés avec leurs empreintes, et fournissent des scénarios attendus.
Ils ne démontrent pas la conformité du prototype au matériel : certains tests
existants vérifient ses règles simplifiées, notamment la probabilité déterministe
et le retrigger par nombre de répétitions.

Ce lot modifie la documentation et l'exclusion Git des copies de référence.
La cohérence des liens locaux et `git diff --check` sont contrôlés ; aucune
recompilation ni nouvelle validation audio n'est nécessaire pour ces fichiers.
Les prochaines modifications de code devront valider les règles de la
[spécification de workflow](WORKFLOW_SPEC.md) en préservant le rappel Live.

## Lot 0.2.0 : panneau et édition

`bash scripts/build.sh` construit VST3 et application autonome Linux ; les deux
cibles CTest `engine` et `plugin_processor` passent. Les nouveaux scénarios
clipboard et TEMP sont effectivement appelés dans la suite par défaut.

La validation avec Xvfb et `TaktTests --gui … --host … --legacy … --render …`
vérifie également :

- Navigation entre six familles, sous-pages, seize pistes et huit pages de pas
  sans modification des paramètres, samples ou trigs ; les 265 identifiants
  d'automation restent présents et les encodeurs éditent la bonne piste.
- Gestes TRK/FUNC, presse-papiers typé, copie de locks avec effet audible,
  annulation et restauration temporaire des seize pistes. Les callbacks host
  peuvent relire l'état sans deadlock ; le rappel d'un état ne marque pas le
  projet comme modifié.
- Raccourcis Ctrl+C/V/Z avec caractère texte nul, comme sous Windows ; les
  événements provenant d'une saisie de texte sont laissés à cette saisie.
- Capture de l'éditeur, revue visuelle du panneau et chargement réel du nouveau
  bundle VST3 dans un host JUCE, avec automation et rappel.
- État produit par le bundle 0.1.0 préservé avant compilation : seize samples,
  128 pas, locks, longueurs et paramètres identiques après lecture par 0.2.0,
  avec comparaison du rendu audio puis nouveau cycle de sauvegarde/rappel.
- Rendu hors ligne de quatre mesures, stéréo 48 kHz. Les trois identifiants de
  classe du VST3 restent identiques, avec version portée de 0.1.0 à 0.2.0.

Le [run Windows 37888994853](https://github.com/BaptisteBulabois/digitakt/actions/runs/37888994853),
pour le commit `fb5741d91d587bf5d304a7c86064809fcd759916`, a réussi : compilation
x64 Release, CTest, scénarios GUI et chargement du VST3 Windows dans un host JUCE.
Le conditionnement et l'envoi de l'[archive installable 0.2.0](https://github.com/BaptisteBulabois/digitakt/actions/runs/37888994853/artifacts/11598145486)
ont également réussi. Elle contient le dossier complet `Takt II.vst3`, une capture
du panneau et les instructions d'installation.

L'installation et le comportement de 0.2.0 dans Live nécessitent le retour de
l'utilisateur ; les tests JUCE ne constituent pas un test automatisé d'Ableton
Live. Le test avec le véritable ancien module et la comparaison de rendu ont
été exécutés sous Linux, comme décrit ci-dessus.

## Branche de développement 0.3.0

Le 9 octobre 2026, `bash scripts/build.sh` construit VST3 et application
autonome Linux. Les cinq cibles CTest passent : `engine`, `pattern_chain`,
`sequencer_rules`, `amp_filter` et `plugin_processor`.

La suite moteur comporte 24 groupes couvrant notamment les machines SRC,
48 LFO actifs, conditions chronologiques PRE/NEI, retriggers entre blocs,
portes MIDI/séquenceur, enveloppes, filtres, SRR/BR et chorus. Le stress de
rendu des seize pistes vérifie zéro allocation et zéro libération sur le
thread de traitement.

Les tests du processeur vérifient les 1 377 paramètres, avec les 265 premiers
identifiants, indices et plages inchangés, ainsi que :

- Rappel des machines, trois LFO par piste, slices, règles avancées et DSP ;
  rejet d'états invalides et retour aux valeurs Legacy lors de la lecture
  de `TAKTII_STATE_1` dans une instance déjà modifiée.
- Audio effectif des commandes AMP/FLTR/FX, note-off ADSR, Control All,
  sauvegarde temporaire et presse-papiers typé.
- Banques A01/B02/H16, pool de samples partagé, seize songs, transitions aux
  frontières musicales, mutes, Perform Kit et édition avant rafraîchissement
  de l'interface.
- Rappel d'un kit Perform modifié distinct du kit stocké, rendu après
  synchronisation de l'interface et nouvelles sauvegardes/réouvertures.
- Migration STATE_1 vers STATE_2 puis nouvelle ouverture, conservant le
  placement absolu aux positions PPQ 1,75 et 2 du transport host.

La validation Xvfb avec `TaktTests --gui … --host … --legacy … --render …`
passe : navigation des six machines, trois LFO, AMP/EQ/FX/TRIG, Control All,
banques et édition/démarrage de songs ; gestes d'automation équilibrés et
raccourcis Windows sans caractère texte. Le test Song traite réellement
la commande audio et sa frontière de pattern avant de vérifier l'activation.

Le bundle VST3 0.3 est chargé et rendu dans un host JUCE avec automation et
rappel. Les véritables modules 0.1 et 0.2 préservés produisent chacun un état
rechargé par 0.3 : seize samples, 128 pas, locks, paramètres et rendu MIDI
identiques, puis comparaison audio des seeks host après migration et nouvelle
sauvegarde. Tous les modules sont arrêtés et préparés dans les mêmes conditions.
Les trois identifiants de classe VST3 sont conservés entre 0.1, 0.2 et 0.3.

La capture [du panneau 0.3](images/takt-ii-0.3.png) a été revue visuellement ;
le rendu hors ligne de quatre mesures, stéréo 48 kHz, passe également.
Le popup natif Slice Editor n'est pas automatisé dans la suite GUI ; les
points, grilles et leur rappel sont vérifiés au niveau du processeur.

Le [run Windows 37982391074](https://github.com/BaptisteBulabois/digitakt/actions/runs/37982391074),
pour le commit `661cb0266584f55e6fefc6b273d2d43c3a30c65d`, a réussi :
compilation x64 Release, les cinq cibles CTest, scénarios GUI et chargement
du véritable VST3 Windows dans un host JUCE avec rendu, automation et rappel.
Le conditionnement et la publication de
l'[archive installable 0.3.0 de développement](https://github.com/BaptisteBulabois/digitakt/actions/runs/37982391074/artifacts/11641331887)
ont également réussi. Le code et ce build sont sur la branche de développement ;
`main` reste au commit `185ad20abe5d249894417df6b9785222da0b2a73` (0.2.0).
L'archive publiée a été téléchargée et inspectée : bundle complet, binaire
PE AMD64, manifeste 0.3.0, capture Windows et LISEZ-MOI présents.

Ces résultats ne constituent pas un essai de 0.3 dans Ableton Live ni une
comparaison du DSP avec le matériel. Les limites de workflow restent dans le
[guide 0.3](WORKFLOW_0_3.md).

## Corrections de développement 0.3.1

Le 10 octobre 2026, la compilation Linux via `bash scripts/build.sh` réussit.
Les six cibles CTest passent : moteur (26 groupes), pattern chain, sequencer
rules, AmpFilter (5 groupes), processeur et contrôle temps réel du processeur.
Le dernier contrôle est spécifique Linux/GNU : il instrumente `new/delete`
et `malloc/free/calloc/realloc`, avec un SysEx propriétaire servant de témoin
positif pour vérifier que l'instrumentation détecte réellement des allocations.

Les callbacks testés rendent zéro allocation et zéro libération : premier
bloc après préparation, SysEx ignoré de 1 024 octets, rafale MIDI en saturation,
seize pistes avec 48 LFO et filtres/chorus, transition de banque et premier
bloc après rappel du projet courant. Cela couvre ces scénarios du processeur
entier, sans garantir le comportement d'un callback externe fourni par un host.

Les nouvelles régressions du processeur couvrent :

- sauvegarde réentrante au premier callback de paramètre de TEMP et du rappel
  d'un projet, avec réouverture d'un état complet, y compris play/sync/master ;
- cache démo/import/blobs chargés, absence de recompression aux sauvegardes
  suivantes, partage des samples entre patterns et comparaison du rendu ;
- conservation dès le premier bloc des paramètres, trigs/CLEAR, longueurs,
  slices et samples modifiés après un rappel, sur instance neuve ou déjà
  préparée, avec sauvegarde/réouverture avant et après ce bloc ;
- rappel remplaçant un ancien Select encore en attente, et Select suivant
  un rappel conservant le kit du pattern demandé ;
- timing du swing en Song : automation live héritée, override explicite
  de la ligne prioritaire ;
- ordre note-on/off simultané, overflow observable et admission des releases
  même après une rafale de releases redondantes ;
- import asynchrone, erreur, annulation, remplacement d'une demande, navigation,
  remplacement du pattern de destination et destruction du processeur ;
- completion pouvant détruire le processeur, et notification host demandant
  un nouvel import sans perdre son état pending ;
- Control All annulé après une transition automatique sans écraser le kit
  suivant, avec gestes d'automation équilibrés.

Les tests numériques vérifient le saturateur master borné à ±1, valeurs
extrêmes et erreur inférieure à 0,024 par rapport à `tanh`. Les caches de
filtres sont vérifiés sur les 49 transitions de machines ; le moteur MIDI
teste les événements triés/non triés et l'ordre simultané entre sous-blocs.
Les mesures de performance et leurs limites sont dans
[REVIEW_FIXES_0_3_1.md](REVIEW_FIXES_0_3_1.md).

La vérification Linux du bundle réel passe également : scan et instanciation
VST3, MIDI stéréo, automation, sauvegarde/rappel et rendu WAV quatre mesures.
La suite GUI valide les imports asynchrones, CANCEL, fermeture de l'éditeur,
navigation et prévisualisation LFO abandonnée après transition automatique,
en plus des gestes d'édition et d'arrangement existants. La capture 0.3.1
du README provient de ce rendu de l'éditeur.

L'utilisateur a retiré la compatibilité avec les versions précédentes comme
exigence. Le contrôle avec un ancien bundle et la parité de son rendu ne font
plus partie de la validation de ce lot. `main` reste au commit `185ad20`.

Le [run Windows 38056853020](https://github.com/BaptisteBulabois/digitakt/actions/runs/38056853020),
pour le commit `edd8733b9a51d36c6b770d216b123e23a747ab6f`, a réussi :
compilation x64 Release, cinq cibles CTest et suite GUI/host sur le vrai
VST3 Windows. Les nouveaux cas de rappel sur instance déjà préparée, Select
avant/après Restore, swing Song et import asynchrone passent aussi sur Windows.
L'[archive installable 0.3.1](https://github.com/BaptisteBulabois/digitakt/actions/runs/38056853020/artifacts/11671683037)
a été téléchargée et inspectée : bundle complet, binaire PE AMD64, manifeste
0.3.1, identité VST3 conservée, capture Windows 900 × 780 et LISEZ-MOI présents.
Cette validation utilise un host de test JUCE ; elle ne constitue pas un essai
de la 0.3.1 dans Ableton Live.
