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

Le workflow GitHub Actions compile et teste séparément la cible Windows avec Visual Studio. Le chargement final dans Ableton Live doit être vérifié sur une machine Windows avec Live installé.
