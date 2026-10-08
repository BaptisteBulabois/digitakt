# Takt II — sampler séquenceur VST3

Une réimplémentation indépendante de fonctions du **Digitakt II** pour **Windows 64 bits et Ableton Live**, écrite en C++17/JUCE. Le plugin s'appelle **Takt II**. Le projet contient également une application autonome et une cible Linux pour les tests.

Il s'agit d'une première version fonctionnelle, pas d'une émulation sonore certifiée de la machine. Le moteur, l'interface et les sons de démonstration sont originaux ; ils ne contiennent pas de firmware ni de banque de sons Elektron.

La priorité est maintenant la **fidélité de l'interface et du workflow musical** du Digitakt II, avec les samples importés par l'utilisateur. Overbridge et les fonctions de connexion au matériel sont hors périmètre. Les décisions de référence sont conservées dans [docs/PRODUCT_SCOPE.md](docs/PRODUCT_SCOPE.md).

## Jouer dans Live

1. Copier le dossier complet `Takt II.vst3` dans `C:\Program Files\Common Files\VST3\` (ou un dossier VST3 personnalisé de Live).
2. Activer les plugins VST3 dans **Préférences → Plug-ins** et relancer l'analyse.
3. Charger **Takt II** sur une piste MIDI. Activer **Host** dans le plugin et lancer le transport de Live : le pattern de démonstration joue immédiatement.
4. Les notes MIDI **36 à 51** déclenchent les pistes **1 à 16**, indépendamment du séquenceur. Cette correspondance est propre au plugin.
5. Sélectionner une piste, utiliser **Import**, puis cliquer sur les pas pour écrire un pattern. Les sons intégrés permettent de jouer sans importer de fichiers.

Avec **Host** désactivé, **Run** et le tempo interne pilotent le séquenceur. Les samples et le pattern sont intégrés dans l'état du plugin enregistré avec le projet Live ; le fichier original peut ensuite être déplacé.

## Fonctions implémentées

| Élément | Comportement |
| --- | --- |
| Audio | 16 pistes stéréo, une voix de sample par piste, mixage de sortie stéréo |
| Samples | WAV, AIFF, FLAC ; mono dupliqué en stéréo ; rééchantillonnage linéaire ; 60 s maximum par sample |
| Lecture | Accordage, début/fin, inversion, boucle, volume, panoramique, mute |
| Traitement | Enveloppe d'amplitude attack/decay, filtre passe-bas résonant, saturation, réduction de résolution |
| Séquenceur | 1 à 128 pas par piste, huit pages de 16 pas, swing, microtiming, retriggers |
| Pas | Vélocité, probabilité déterministe, condition tous les N cycles, locks de hauteur et de fréquence du filtre |
| Effets | Delay ping-pong synchronisé au tempo et réverbération algorithmique, envois par piste |
| DAW | VST3 instrument, MIDI, tempo/PPQ/transport du host, paramètres automatisables, rappel de l'état |
| Démonstration | 16 samples synthétisés et un pattern original prêt à jouer |

Un clic sur un pas active/désactive son trig et le sélectionne. Un clic droit ou un clic avec modificateur sélectionne un pas pour éditer ses paramètres. La longueur du pattern se règle par piste. Le bouton **Clear** efface les pas de la piste sélectionnée.

Les fonctions non implémentées sont décrites dans [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md). L'utilisateur a confirmé l'installation du VST3 dans Ableton Live ; les comportements musicaux doivent encore être vérifiés dans Live.

## Compiler sur Windows

Le guide pas à pas est disponible ici : **[installer et compiler Takt II sous Windows](docs/INSTALL_WINDOWS.md)**.

En résumé, installer **Visual Studio 2022** avec le module « Développement Desktop en C++ », Git et CMake 3.22 ou ultérieur. Depuis PowerShell :

```powershell
.\scripts\build-windows.ps1
```

JUCE **8.0.6** est téléchargé au commit `51a8a6d7aeae7326956d747737ccf1575e61e209`. Un checkout existant peut être fourni avec `-DTAKT_JUCE_PATH=...`.

Avec le script PowerShell, le bundle est dans `build-windows/TaktII_artefacts/Release/VST3/Takt II.vst3`. Copier **le dossier entier**, y compris `Contents/x86_64-win/`, dans le dossier VST3 de Live.

## Développer dans le cloud Linux

```bash
bash scripts/setup-cloud.sh
bash scripts/build.sh
```

L'installation cloud conserve les dépendances sous `/workspace/.digitakt-tools` ; elle ne modifie pas les paquets du système. Le build utilise trois jobs par défaut (`TAKT_BUILD_JOBS=2` pour limiter la charge).

Sur un Linux classique, installer les paquets de développement ALSA, X11, Xext, Xrandr, Xinerama, Xcursor, Xrender, Freetype et Fontconfig, puis utiliser CMake/Ninja. Aucun navigateur ni serveur réseau n'est nécessaire.

Pour tester seulement le moteur, sans JUCE ni bibliothèque graphique :

```bash
cmake -S . -B build-engine -DTAKT_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-engine --parallel 3
ctest --test-dir build-engine --output-on-failure
```

Les commandes et résultats de validation de cette livraison seront consignés dans [docs/VALIDATION.md](docs/VALIDATION.md).

## Licence

Le code original de ce dépôt est sous [licence MIT](LICENSE). JUCE 8 est soumis à sa propre licence, notamment **AGPLv3** ou licence JUCE applicable : voir [la licence de JUCE](https://github.com/juce-framework/JUCE/blob/8.0.6/LICENSE.md). La distribution d'un binaire doit respecter la licence choisie pour JUCE. Digitakt et Elektron sont des marques de leurs propriétaires ; ce projet indépendant n'est pas affilié à Elektron.
