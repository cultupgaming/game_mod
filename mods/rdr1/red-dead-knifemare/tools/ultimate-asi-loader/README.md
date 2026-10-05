# Ultimate ASI Loader dependency

Red Dead Knifemare includes the repository's x64 Ultimate ASI Loader 7.7.0 `wininet.dll` for the `update`-folder file-overload path used by the Assassin Knife UI.

The packaged `wininet.dll` SHA-256 is:

```text
BB8767F918C52A2AD055D2DE9BAFFD2478598643B9894F09ABD20D1F1FFD170C
```

No downloader is used for Ultimate ASI Loader. The existing game-root `dinput8.dll` remains responsible for normal ASI plugin loading and is not replaced by Knifemare.

## Why Knifemare imports Ultimate ASI Loader

Knifemare stores its modified UI archive at:

```text
update\game\mapres.rpf
```

The ASI imports `IsUltimateASILoader` through a small MSVC-generated import library. This is a normal, non-delayed dependency, so Windows loads the packaged `wininet.dll` before entering `RedDeadKnifemare.asi`'s `DllMain`.

That makes the file-overload dependency explicit instead of relying on an incidental game import. If the required UAL DLL is missing, or does not export `IsUltimateASILoader`, the Knifemare ASI will not load.

## Configuration

`wininet.ini` configures Ultimate ASI Loader for file overloading only:

```ini
[GlobalSets]
LoadPlugins=0
DontLoadFromDllMain=1
ForceEntryPointHook=0
LoadFromAPI=
FindModule=0

[FileLoader]
OverloadFromFolder=update
```

`LoadPlugins=0` leaves ASI discovery with the existing `dinput8.dll` loader.

`DontLoadFromDllMain=1` keeps Ultimate ASI Loader's normal deferred file-hook setup enabled. The effective mapping used by Knifemare is:

```text
game\mapres.rpf -> update\game\mapres.rpf
```

## Install and uninstall ownership

The Knifemare release package includes:

- `wininet.dll`
- `wininet.ini`
- `LICENSE.txt`

During installation, Knifemare backs up any pre-existing game-root `wininet.dll` and `wininet.ini`, installs the packaged copies, verifies their SHA-256 hashes, and records the installed state in:

```text
RedDeadKnifemareAssets\release-install-state.json
```

Uninstall verifies the tracked installed files before restoring the pre-install copies or removing files that did not exist before Knifemare was installed. This prevents the uninstaller from overwriting a loader/configuration that was changed after installation.

Upstream project: https://github.com/ThirteenAG/Ultimate-ASI-Loader

Version used here: 7.7.0

License: MIT (see `LICENSE.txt`).
