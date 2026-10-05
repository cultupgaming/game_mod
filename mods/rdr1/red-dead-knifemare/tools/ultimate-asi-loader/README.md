# Ultimate ASI Loader startup dependency

Red Dead Possession issue #104 uses the repository's x64 UAL 7.7.0
`wininet.dll` from the supplied Colorized Weapon Wheel Icons package.
Its SHA-256 is `BB8767F918C52A2AD055D2DE9BAFFD2478598643B9894F09ABD20D1F1FFD170C`.
No downloader is used. The existing game-root `dinput8.dll` stays untouched.

Copying wininet.dll alone did not load it in the user's runtime. The ASI now
imports `IsUltimateASILoader` through a small MSVC-generated import library.
This is a normal, non-delayed dependency: Windows must load UAL before entering
RedDeadPossession's DllMain, rather than waiting for ScriptMain or an incidental
game import. The ASI therefore requires the deployed UAL DLL; a missing DLL or
one without that export prevents this candidate ASI from loading. If no new
Possession trace appears, send the deployment and ScriptHook logs.

`wininet.ini` configures UAL for file overloading only (`LoadPlugins=0`), keeping
ASI discovery with the existing dinput8 loader. `DontLoadFromDllMain=1` leaves
UAL's normal deferred file-hook setup enabled. Do not change that setting to
zero: UAL 7.7 installs the global file hooks through its deferred path.
The mapping remains `game\mapres.rpf` -> `update\game\mapres.rpf`.

UAL 7.7 reads game-root, scripts, plugins and update `global.ini` files after
wininet.ini. Deployment refuses conflicting relevant settings in those files
and leaves them untouched. It backs up a pre-existing wininet.ini, tracks the
DLL and INI hashes in version 2 of
`RedDeadPossessionAssets\possessor-wininet-state.json`, and supports upgrading
version-1 DLL-only ownership. Undeploy restores original files or removes only
unchanged mod-installed copies. Version-1 undeployment never touches the INI.

Runtime evidence is deliberately separated:

- `STARTUP imported wininet IsUltimateASILoader = 1`: dependency loaded before
  ASI script registration.
- `module=wininet.dll loaded=1 ... ual=1`: loaded module identity/path.
- `overload-root-status=api-unavailable`: expected for UAL 7.7; it has no
  `GetOverloadPathW` export and this is not an "overloading disabled" result.
- `mapres-mapped=1`: the mapping API predicts the update archive.
- `mapres-open-update=1`: a read-only Win32 open actually resolved to the update
  archive. This does not by itself prove which archive RAGE already mounted.

Upstream source: https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/v7.7.0/source/dllmain.cpp
License: MIT (see LICENSE.txt).
