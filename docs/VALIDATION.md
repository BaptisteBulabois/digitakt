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

Le workflow Windows compile et exécute CTest, puis les scénarios GUI et le
chargement du VST3 Windows avant de produire l'archive. Le résultat de son
exécution est à vérifier dans GitHub Actions pour le commit livré. L'installation
et le comportement de 0.2.0 dans Live nécessitent le retour de l'utilisateur ;
les tests JUCE ne constituent pas un test automatisé d'Ableton Live.
