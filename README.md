# Sifu Button Controls 0.2.1

Mod page: https://www.nexusmods.com/sifu/mods/1754

This public review snapshot contains the exact editor source, MinHook source, seven offline C++ check suites, installer source and asset-generation scripts for the quarantined With Loader download. It does not contain compiled executables, the game EXE/PDB/signature, original game assets, personal configurations or machine logs.

## Build the editor from source

Use Windows x64 and w64devkit's GCC 16.2.0, the compiler used for the uploaded ASI. From this extracted packet, run:

```powershell
.\Build-Review.ps1 -CompilerBin '<w64devkit folder>\bin'
```

The script builds MinHook from the included C source, builds and runs seven offline C++ suites, and links `review-build/SifuButtonControls.asi`. It does not install or load the resulting ASI, modify a game installation, launch Sifu or change system settings. No game assets or Python installation are needed for this code build.

The original MinGW preferred image base is pinned in the link command because its default value depends on the output path. With the original toolchain this reproduces the uploaded ASI byte for byte:

`3F2973D122D8BC3BE182FB71D2EC0D3C3FBB54AB33E020DFB27144874819304A`

The locally completed build is recorded in `Reproduction.json`; all seven suites passed and the ASI SHA-256 matches. This verifies binary/source correspondence, not a malware-vendor verdict.

## Runtime behavior relevant to review

- OptiScaler loads the ASI into the Sifu process. The ASI checks the game executable SHA-256 before installing build-specific MinHook hooks.
- Hooks handle gameplay input, controller hints and an in-game editor overlay. They do not inject into a separately selected process.
- The editor reads/writes its own profile, three preset slots, temporary replacements, backups and runtime log beside the ASI. Its native calls still enforce game skill and action conditions.
- The editor source contains no network client or account/credential collection code. The bundled OptiScaler is a separate upstream component; VirusTotal's aggregate behavior can include its GitHub release-update check.
- The installer source explicitly lists the six game-relative installation targets, validates the game and payload, backs up overwritten files and copies the player's original signature. Uninstall validates tracked hashes before restoring/removing files.
- The CMD launchers use PowerShell's per-process `-ExecutionPolicy Bypass` argument to run the included local script. They do not persistently change user or machine execution-policy settings.

## Assets and other compiled code

`asset-generation/` contains the author's Python asset-overlay generation scripts as review material. Rebuilding the pak separately requires the matching legally installed game's exported assets, UAssetGUI, UnrealLocres and repak; those game inputs and third-party executables are not redistributed here. The pak SHA-256 is recorded in `Quarantine-report.md`. The executable ASI build above is standalone and uses the already-generated catalogue header in `customizer/src/`.

`installer/` contains the scripts and configuration shipped in the download. Its runtime manifest documents the binary hashes; the binaries themselves are not in this source packet.

The included OptiScaler DLL is unmodified official nightly-20261019, commit `1db5042aa57425d59ac979b20d43eea0053a15a5`. Its exact upstream source and dependency commits are listed in `Dependency-source.json` and [OPTISCALER-SOURCE-ACCESS.md](OPTISCALER-SOURCE-ACCESS.md). Licenses and notices are in `licenses/`; MinHook's license is also retained under `tools/minhook/`.

This source is published for code review. No additional blanket reuse license is granted for the editor by this publication; third-party license rights remain intact.

## Quarantine status

The ZIP-level VirusTotal result and contained-ASI result differ. Read [Quarantine-report.md](Quarantine-report.md) for the observed hashes, engine labels and report links. The exact Nexus rule that quarantined this file has not been disclosed, and no manual malware verdict has been obtained.

Official Nexus guidance: https://help.nexusmods.com/article/117-why-has-my-mod-been-quarantined

The existing quarantined upload is retained for moderator review. This repository provides its matching source and standalone build instructions. Version remains 0.2.1; publishing this source does not replace or modify the uploaded runtime.
