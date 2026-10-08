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
