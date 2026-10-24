# Quarantine review facts — 2026-10-25

Mod: https://www.nexusmods.com/sifu/mods/1754
Uploaded display name: SifuButtonControls 0.2.1 With Loader.
The screenshot's Nexus file-version field displays 0.21; the editor identifies itself as 0.2.1.

## VirusTotal observations

- Uploaded ZIP: 8408D78F678E42382F226368F8FB0A6F7DD815FE39E88DB67CD98427111D0ADE
  Report: https://www.virustotal.com/gui/file/8408d78f678e42382f226368f8fb0a6f7dd815fe39e88db67cd98427111d0ade
  Observed result: 0/67; report analysis time 2026-10-25 02:02:30 Asia/Shanghai.
- Contained SifuButtonControls.asi: 3F2973D122D8BC3BE182FB71D2EC0D3C3FBB54AB33E020DFB27144874819304A
  Report: https://www.virustotal.com/gui/file/3f2973d122d8bc3be182fb71d2ec0d3c3fbb54ab33e020dfb27144874819304a
  Observed result: 2/70; report analysis time 2026-10-25 01:42:44 Asia/Shanghai.
  Microsoft: Trojan:Win32/Wacatac.B!ml
  Bkav Pro: W32.Malware.14DEA3F8
- Contained dxgi.dll / upstream OptiScaler.dll: D6285A478FBE390A6FC92AC38C073AAAE561B0ADE4EC8F73686BD025A6A65342
  Report: https://www.virustotal.com/gui/file/d6285a478fbe390a6fc92ac38c073aaae561b0ade4ec8f73686bd025a6a65342
  Observed result: 0/71; report analysis time 2026-10-20 02:16:00 Asia/Shanghai.
- Paired ButtonControls_P.pak: AEE592BE30516B97CBB763DAEF52996C39588A017F0D139AA6945F9B83A59DAF

The ZIP contains 33 files: no EXE, password protection or nested archive. Its active-code entries are the ASI, the unmodified upstream OptiScaler DLL, two CMD launchers and one PowerShell installer/uninstaller script. All archived bytes were read back and checked against local staging hashes.

These observations identify the contained ASI detections. They do not establish which exact Nexus rule caused quarantine, prove that the detections are false positives, or guarantee that the software is safe. No released binary has been changed in response to the scan.

## Reproduction

The exact included editor source was independently rebuilt with GCC 16.2.0, with the original MinGW preferred image base pinned for output-directory independence. The rebuilt ASI is byte-identical to the uploaded ASI. All seven offline C++ suites passed. See Reproduction.json and Build-Review.ps1.

## Review route

Nexus guidance: https://help.nexusmods.com/article/117-why-has-my-mod-been-quarantined
Recipient: support@nexusmods.com
Include the mod link, archive and ASI report links, exact hashes, accessible source and build steps. Keep the existing quarantined file for moderator review. This source snapshot is published for review. The quarantined runtime and its recorded hashes remain unchanged.
