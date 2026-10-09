# Installer Takt II sous Windows et Ableton Live

Ce guide concerne Takt II 0.2.0, Windows 10/11 64 bits et Ableton Live 11 ou 12.
Takt II est un instrument VST3 : il doit être chargé sur une piste MIDI.

## Méthode simple : télécharger le build GitHub

Chaque envoi sur la branche `main` lance une compilation Windows automatique.

1. Ouvrir le dépôt GitHub, puis l'onglet **Actions**.
2. Ouvrir l'exécution **Build Windows VST3** correspondant à la version souhaitée,
   marquée d'une coche verte.
3. Dans **Artifacts**, télécharger `Takt-II-Windows-VST3`.
4. Décompresser l'archive. Elle contient le dossier `Takt II.vst3`.
5. Copier ce dossier complet dans :

   ```text
   C:\Program Files\Common Files\VST3\
   ```

   Windows demandera probablement une autorisation administrateur.

6. Ouvrir Live, puis **Options > Préférences > Plug-ins**.
7. Vérifier que **Utiliser les dossiers système VST3** est activé. Maintenir `Alt` en cliquant sur **Réanalyser** pour forcer une analyse complète.
8. Dans le navigateur de Live, ouvrir **Plug-ins > VST3**, puis faire glisser **Takt II** sur une piste MIDI.

Si aucune exécution verte n'est encore affichée, utiliser la méthode de compilation ci-dessous et consulter les erreurs dans l'onglet Actions.

## Mettre à jour une installation existante

1. Sauvegarder le projet Live, puis fermer Live.
2. Télécharger et décompresser le nouveau build comme indiqué ci-dessus.
3. Remplacer le dossier complet `Takt II.vst3` dans
   `C:\Program Files\Common Files\VST3\` par le nouveau bundle.
4. Rouvrir Live et réanalyser les plug-ins si nécessaire, puis ouvrir le projet
   existant : conserver son instance de Takt II pour retrouver ses réglages.

La version 0.2.0 conserve l'identité VST3 et les identifiants des paramètres de
la première version. Le rappel des anciens projets Live reste pris en charge ;
les nouvelles pages présentent les mêmes paramètres automatisables.

## Compiler soi-même

### 1. Installer les outils

Installer :

- [Visual Studio 2022 Community](https://visualstudio.microsoft.com/fr/vs/community/) ;
- la charge de travail **Développement Desktop en C++** dans Visual Studio Installer ;
- les composants **MSVC v143**, **SDK Windows 10 ou 11**, **CMake tools for Windows** et **Ninja** ;
- [Git for Windows](https://git-scm.com/download/win).

JUCE 8 ne prend pas en charge MinGW. Utiliser le compilateur Microsoft installé avec Visual Studio.

### 2. Cloner le projet

Ouvrir PowerShell dans le dossier où placer le projet :

```powershell
git clone https://github.com/BaptisteBulabois/digitakt.git
cd digitakt
```

### 3. Compiler et tester

Exécuter :

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build-windows.ps1
```

Le script configure une compilation x64 Release, télécharge JUCE 8.0.6 depuis son dépôt officiel, compile le VST3 et lance les tests. La première compilation peut prendre plusieurs minutes.

Le résultat se trouve ici :

```text
build-windows\TaktII_artefacts\Release\VST3\Takt II.vst3
```

Pour compiler puis copier automatiquement le plugin dans le dossier VST3 système, ouvrir PowerShell **en tant qu'administrateur** et utiliser :

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build-windows.ps1 -Install
```

### 4. Charger le plugin dans Live

1. Créer une piste MIDI avec `Ctrl+Shift+T`.
2. Charger **Takt II** depuis **Plug-ins > VST3**.
3. Laisser **HOST SYNC** activé, puis lancer le transport de Live pour entendre le pattern de démonstration.
4. Les notes MIDI 36 à 51 déclenchent les pistes 1 à 16.
5. Choisir une piste, cliquer sur **IMPORT SAMPLE**, puis activer **REC** pour
   éditer les pas du séquenceur.

Le bouton **AUDITION** écoute la piste sélectionnée. Avec **HOST SYNC**
désactivé, **PLAY** permet la lecture et la pause au tempo interne. Les samples et
le pattern sont sauvegardés dans le projet Live.

## Dépannage

### Takt II n'apparaît pas

- Vérifier que le chemin contient bien le dossier complet `Takt II.vst3`, pas seulement le fichier DLL situé à l'intérieur.
- Dans Live, maintenir `Alt` pendant le clic sur **Réanalyser**.
- Vérifier que Live et le plugin sont tous deux en 64 bits.
- Consulter `Preferences > Plug-ins` pour confirmer l'activation des dossiers système VST3.

### Windows bloque le téléchargement

Le binaire produit automatiquement par GitHub n'est pas signé. Après avoir téléchargé l'archive depuis votre propre dépôt, Windows peut afficher un avertissement. Ouvrir les propriétés du fichier ZIP, cocher **Débloquer** si cette option existe, puis extraire de nouveau l'archive.

### La compilation ne trouve pas Visual Studio

Lancer **Visual Studio Installer**, sélectionner **Modifier**, puis ajouter **Développement Desktop en C++** et un SDK Windows. Fermer et rouvrir PowerShell avant de relancer le script.

### Le projet Live est silencieux

- Démarrer le transport de Live lorsque **HOST SYNC** est actif.
- Cliquer sur **AUDITION** pour vérifier le son de la piste sélectionnée.
- Charger le pattern avec **LOAD DEMO**.
- Vérifier le volume de la piste, le volume **MASTER** et les mutes.

## Workflow et périmètre

Le [guide de la version 0.2.0](WORKFLOW_0_2.md) décrit les pages, les modes de
jeu/édition et les opérations de copie et restauration disponibles. Il distingue
les gestes adaptés à la souris des fonctions du Digitakt II restant à développer.

Overbridge, Outbox et la maintenance d'un appareil Elektron sont hors périmètre.
Les samples sont ceux de l'utilisateur ; les traitements audio sont indépendants.
