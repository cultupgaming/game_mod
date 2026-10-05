# Red Dead Knifemare

## Current Release - v0.1.0

Red Dead Knifemare is an RDR1 PC gameplay mod that turns a normal aimed Throwing Knife attack into an Assassin-style close-range contextual execution against the exact NPC John was targeting.

The release also renames the weapon slot to **Assassin Knife** and replaces the stock Throwing Knife UI artwork with the reviewed red version.

The Assassin Knife UI is mandatory for this release.

## Requirements

- Red Dead Redemption 1 for PC
- ScriptHookRDR installed and working
- `ScriptHookRDR.dll` and `dinput8.dll` present in the same folder as `RDR.exe`
- MagicRDR, downloaded separately from the official releases page: https://github.com/Foxxyyy/Magic-RDR/releases

MagicRDR is required only to patch the mandatory Assassin Knife UI into your own `mapres.rpf` override. It is not redistributed inside the Knifemare release ZIP.

The package includes the additional Ultimate ASI Loader files needed for Knifemare's `update\game\mapres.rpf` UI override. It also includes Ultimate ASI Loader's upstream MIT `LICENSE.txt` beside those files. It does not include or redistribute Rockstar's `mapres.rpf`.

## Installation

1. Close Red Dead Redemption completely.
2. Open the downloaded Knifemare ZIP and copy **all files and folders inside it** into your Red Dead Redemption game folder, the same folder that contains `RDR.exe`.
3. Double-click `Install.bat`.
4. If MagicRDR is missing, the installer will stop before changing any game files and show the official MagicRDR download page.
5. Download and extract MagicRDR into:

```text
Red Dead Redemption\RedDeadKnifemareInstaller\tools\MagicRDR\
```

6. Make sure that folder contains `MagicRDR.exe` and its `Assemblies` folder.
7. Run `Install.bat` again.
8. Wait for the installer to report that installation completed successfully.
9. Start the game normally.

After copying the Knifemare package, your game folder should look like this:

```text
Red Dead Redemption/
├── RDR.exe
├── ScriptHookRDR.dll
├── dinput8.dll
├── Install.bat
├── Uninstall.bat
├── README.md
├── SOURCE.txt
└── RedDeadKnifemareInstaller/
    ├── install.ps1
    ├── uninstall.ps1
    ├── assets/
    │   ├── radial_thrn_knife.wtd
    │   └── weapons_thrn_knife.wtd
    ├── payload/
    │   └── RedDeadKnifemare.asi
    └── tools/
        ├── MagicRDR/
        │   └── DOWNLOAD-MAGICRDR.txt
        └── ultimate-asi-loader/
            ├── LICENSE.txt
            ├── wininet.dll
            └── wininet.ini
```

After downloading MagicRDR, place its extracted files in the `MagicRDR` folder beside `DOWNLOAD-MAGICRDR.txt`.

`Install.bat` then automatically:

- verifies the mandatory Assassin Knife UI files
- verifies that MagicRDR and its required assemblies are present
- backs up the relevant pre-existing files
- creates or updates `update\game\mapres.rpf` from your own game archive
- installs the mandatory Assassin Knife UI textures into `root\mapres`
- installs the required Ultimate ASI Loader files
- installs `RedDeadKnifemare.asi`
- records hashes so uninstall can safely restore the previous state

## Why MagicRDR Is Downloaded Separately

MagicRDR contains compiled executable code. To keep the Knifemare release ZIP smaller and reduce avoidable antivirus/Nexus quarantine triggers, Knifemare does not redistribute MagicRDR.

The UI is still mandatory. The installer will not install only the gameplay ASI while skipping the Assassin Knife UI.

## How to Play

1. Load normal gameplay.
2. Equip the Throwing Knife / Assassin Knife.
3. Aim at one supported living human NPC on foot.
4. Keep TARGET / aim held and throw once.
5. Release FIRE / attack after the throw starts.
6. Knifemare moves John into close execution range and asks RDR1 to start a contextual linked knife execution against that exact target.

RDR1 chooses the contextual execution that fits the current position, facing and target state. Knifemare does not run a custom finisher roulette.

## Controls

There are no public F6/F7/F8/F9 debug or outfit-toggle hotkeys in v0.1.0.

Normal aim and attack controls are used for gameplay.

## Assassin Outfit

The release uses the fixed Assassin-inspired Deadly Assassin preset prepared by the mod. The old F9 inspection/toggle control is disabled in the public build.

## Logging

Normal release builds have Knifemare runtime trace logging disabled and do not create `RedDeadKnifemare.trace.log`.

## Supported Targets / Known Limitations

The normal path is intended for ordinary living human NPCs who are on foot and available for a contextual execution.

Mounted or vehicle targets are unsupported. Tight geometry, steep terrain, interruptions, scripted sequences and some mission contexts can prevent a valid execution. In unsupported situations the mod is designed to reject or abort instead of substituting a nearby NPC.

## Uninstallation

1. Close Red Dead Redemption completely.
2. Double-click `Uninstall.bat` from the same game folder.
3. The uninstaller first verifies that the files installed by this package still match the recorded hashes.
4. If the checks pass, it removes `RedDeadKnifemare.asi` and restores the pre-install `update\game\mapres.rpf`, `wininet.dll` and `wininet.ini` state.

The uninstaller deliberately refuses to overwrite files that were changed after Knifemare was installed.

After a successful uninstall, you may delete these leftover installer-package files manually:

```text
Install.bat
Uninstall.bat
README.md
SOURCE.txt
RedDeadKnifemareInstaller/
```

## Updating

The installer intentionally does not perform an in-place update. If a Knifemare install is already tracked, run `Uninstall.bat` first, then copy the newer release files into the game folder and run `Install.bat` again.

## Notes

- The installer patches a copy of your own `game\mapres.rpf`; Rockstar's original archive remains unchanged.
- Existing `update\game\mapres.rpf`, `wininet.dll`, `wininet.ini` and `RedDeadKnifemare.asi` files are backed up when present before installation.
- If one of the tracked installed files changes before uninstall, automatic uninstall stops rather than overwriting the newer file.
- Keep the copied installer files until you are finished testing so `Uninstall.bat` remains available.

## Source Code

Public source repository:

https://github.com/cultupgaming/game_mod

For release verification, use `SOURCE.txt` inside the ZIP. The release script verifies that the local build commit exactly matches the published public branch head before packaging, then writes both the full commit SHA and a commit-pinned source URL into `SOURCE.txt`.

The commit-pinned URL in `SOURCE.txt` is the authoritative source link for the packaged `RedDeadKnifemare.asi`; it does not move when `main` changes later.

## Bug Reports

If installation or gameplay fails, report what happened and include the exact message shown by `Install.bat` or `Uninstall.bat`.
