[CmdletBinding()]
param(
    [ValidateSet('Install','Uninstall')][string]$Mode='Install',
    [string]$GameRoot
)
$ErrorActionPreference='Stop'
if (-not $GameRoot) {$GameRoot=(Read-Host 'Paste the Sifu installation folder (Steam > Browse local files)').Trim().Trim('"')}
$game=(Resolve-Path -LiteralPath $GameRoot).Path.TrimEnd('\','/')
if (Get-Process -Name 'Sifu','Sifu-Win64-Shipping' -ErrorAction SilentlyContinue) {throw 'Close Sifu before installing or uninstalling.'}
$binary=Join-Path $game 'Sifu\Binaries\Win64'
$statePath=Join-Path $game 'SifuButtonControls.bundle-install.json'
$allowed=@('Sifu/Binaries/Win64/dxgi.dll','Sifu/Binaries/Win64/OptiScaler.ini',
    'Sifu/Binaries/Win64/OptiScaler/plugins/SifuButtonControls.asi',
    'Sifu/Binaries/Win64/OptiScaler/plugins/ButtonControls.ini',
    'Sifu/Content/Paks/~mods/ButtonControls_P.pak','Sifu/Content/Paks/~mods/ButtonControls_P.sig')
function Hash([string]$path) {(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
function Target([string]$relative) {
    if ($relative -notin $allowed) {throw "Unrecognized installation path: $relative"}
    $resolved=[IO.Path]::GetFullPath((Join-Path $game $relative))
    if (-not $resolved.StartsWith($game+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {throw 'Path is outside the selected game.'}
    # Do not follow junctions into another checkout or installation.
    $parent=Split-Path $resolved -Parent
    while ($parent.Length -ge $game.Length) {
        if (Test-Path -LiteralPath $parent) {
            if ((Get-Item -LiteralPath $parent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {throw "Linked installation folder is unsupported: $parent"}
        }
        if ($parent -eq $game) {break}
        $parent=Split-Path $parent -Parent
    }
    if ((Test-Path -LiteralPath $resolved) -and (Get-Item -LiteralPath $resolved -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {throw 'Linked target file is unsupported.'}
    return $resolved
}
function Read-State {
    $state=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    if ($state.schema -ne 1 -or $state.gameRoot -ne $game -or $state.status -ne 'installed') {throw 'Installation record does not match this game folder.'}
    if ($state.backupFolder -notmatch '^SifuButtonControls-backups/[0-9]{8}-[0-9]{6}-[0-9]{4}$') {throw 'Invalid backup folder in installation record.'}
    $recorded=@($state.entries | ForEach-Object {$_.target})
    if ($recorded.Count -ne 6 -or (Compare-Object $allowed $recorded)) {throw 'Installation record has unexpected files.'}
    foreach ($entry in $state.entries) {
        $path=Target $entry.target
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Hash $path) -ne $entry.installedSha256) {throw "Installed file was changed; kept untouched: $($entry.target)"}
        if ($entry.existed) {
            $backup=[IO.Path]::GetFullPath((Join-Path $game $entry.backup))
            $backupRoot=Join-Path $game 'SifuButtonControls-backups'
            if (-not $backup.StartsWith($backupRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {throw 'Invalid backup path.'}
            if ((Hash $backup) -ne $entry.previousSha256) {throw 'Backup changed; nothing has been restored.'}
        }
    }
    return $state
}
if ($Mode -eq 'Uninstall') {
    if (-not (Test-Path -LiteralPath $statePath)) {throw 'No bundled installation record exists here.'}
    $state=Read-State
    foreach ($entry in $state.entries) {
        $path=Target $entry.target
        if ($entry.existed) {Copy-Item -LiteralPath (Join-Path $game $entry.backup) -Destination $path -Force}
        else {Remove-Item -LiteralPath $path}
    }
    $state.status='uninstalled'
    $state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $game ($state.backupFolder+'/uninstalled.json')) -Encoding UTF8
    Remove-Item -LiteralPath $statePath
    Write-Output 'Uninstalled. Previous files restored; personal profiles and game progress kept.'
    return
}
if (Test-Path -LiteralPath $statePath) {
    $null=Read-State
    Write-Output 'This bundle is already installed. Files and personal settings were left unchanged.'
    return
}
$manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Bundle-files.json') -Raw | ConvertFrom-Json
$expectedExe='66845653ff78e4f1c31d7cfefccdcea435cbb5dc186451d443747b849c125e7e'
$exe=Join-Path $binary 'Sifu-Win64-Shipping.exe'
if (-not (Test-Path -LiteralPath $exe) -or (Hash $exe) -ne $expectedExe) {throw 'Unsupported game executable. This preview requires Steam build 18739835.'}
$payloadTargets=@($manifest.files | ForEach-Object {$_.target})
if ($payloadTargets.Count -ne 5 -or (Compare-Object @($allowed | Where-Object {$_ -notlike '*.sig'}) $payloadTargets)) {throw 'Bundle payload is incomplete or has unexpected files.'}
$plan=@(foreach ($file in $manifest.files) {
    $path=Target $file.target
    $source=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot $file.source))
    $runtimeRoot=Join-Path $PSScriptRoot 'runtime'
    if (-not $source.StartsWith($runtimeRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $source -PathType Leaf) -or (Hash $source) -ne $file.sha256) {throw 'Bundle payload failed its integrity check.'}
    @{target=$file.target;path=$path;source=$source;sha256=$file.sha256;replacement=$null}
})
$loader=@($plan | Where-Object {$_.target -like '*/dxgi.dll'})[0]
$ini=@($plan | Where-Object {$_.target -like '*/OptiScaler.ini'})[0]
if (Test-Path -LiteralPath $loader.path) {
    if ((Hash $loader.path) -ne $loader.sha256) {throw 'A different dxgi.dll is already installed. Use the standard mod-only download with your existing loader.'}
    if (Test-Path -LiteralPath $ini.path) {
        $text=[IO.File]::ReadAllText($ini.path)
        $section=[regex]::Match($text,'(?ms)^\[Plugins\][^\r\n]*\r?\n.*?(?=^\[|\z)')
        if (-not $section.Success) {throw 'Existing OptiScaler.ini has no Plugins section. Use the standard download.'}
        $pluginPath=[regex]::Match($section.Value,'(?im)^Path\s*=\s*([^\r\n;]*)')
        if ($pluginPath.Success -and $pluginPath.Groups[1].Value.Trim() -notin @('auto','')) {throw 'Existing loader uses a custom plugin path. Use the standard download.'}
        $setting=[regex]::Match($section.Value,'(?im)^LoadAsiPlugins\s*=\s*([^\r\n]*)')
        if (-not $setting.Success) {throw 'Existing loader has no ASI setting. Use the standard download.'}
        if ($setting.Groups[1].Value.Trim() -eq 'true') {$ini.source=$ini.path;$ini.sha256=Hash $ini.path}
        else {
            $changed=[regex]::Replace($section.Value,'(?im)^LoadAsiPlugins\s*=\s*[^\r\n]*','LoadAsiPlugins=true')
            $ini.replacement=$text.Substring(0,$section.Index)+$changed+$text.Substring($section.Index+$section.Length)
        }
    }
} elseif (Test-Path -LiteralPath $ini.path) {throw 'An existing OptiScaler.ini was found without its loader. Kept untouched; use the standard download.'}
$signature=Join-Path $game 'Sifu\Content\Paks\pakchunk0-WindowsNoEditor.sig'
if (-not (Test-Path -LiteralPath $signature -PathType Leaf)) {throw 'Original game signature was not found; nothing was installed.'}
$sigTarget='Sifu/Content/Paks/~mods/ButtonControls_P.sig'
$plan+=@{target=$sigTarget;path=(Target $sigTarget);source=$signature;sha256=(Hash $signature);replacement=$null}
foreach ($item in $plan) {
    if ((Test-Path -LiteralPath $item.path) -and -not (Test-Path -LiteralPath $item.path -PathType Leaf)) {throw 'An installation target is a directory.'}
}
$backupRelative='SifuButtonControls-backups/'+(Get-Date -Format 'yyyyMMdd-HHmmss-ffff')
$backupFolder=Join-Path $game $backupRelative
$backupParent=Join-Path $game 'SifuButtonControls-backups'
if ((Test-Path -LiteralPath $backupParent) -and (Get-Item -LiteralPath $backupParent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {throw 'Linked backup folder is unsupported; nothing was installed.'}
New-Item -ItemType Directory -Path $backupFolder | Out-Null
$entries=@(foreach ($item in $plan) {
    $existed=Test-Path -LiteralPath $item.path -PathType Leaf
    $saved=$backupRelative+'/'+(Split-Path $item.path -Leaf)
    if ($existed) {Copy-Item -LiteralPath $item.path -Destination (Join-Path $game $saved)}
    @{target=$item.target;existed=$existed;backup=$(if($existed){$saved}else{$null});previousSha256=$(if($existed){Hash $item.path}else{$null});installedSha256=$null}
})
$applied=@()
try {
    for ($i=0;$i -lt $plan.Count;$i++) {
        $item=$plan[$i];$entry=$entries[$i]
        New-Item -ItemType Directory -Path (Split-Path $item.path -Parent) -Force | Out-Null
        $applied+=,$entry
        if ($null -ne $item.replacement) {[IO.File]::WriteAllText($item.path,$item.replacement,[Text.UTF8Encoding]::new($false))}
        elseif ($item.source -ne $item.path -and (-not (Test-Path -LiteralPath $item.path) -or (Hash $item.path) -ne $item.sha256)) {Copy-Item -LiteralPath $item.source -Destination $item.path -Force}
        $entry.installedSha256=Hash $item.path
        if ($null -eq $item.replacement -and $entry.installedSha256 -ne $item.sha256) {throw 'Installed file hash mismatch.'}
    }
    $stateText=@{schema=1;version=$manifest.version;status='installed';gameRoot=$game;backupFolder=$backupRelative;entries=$entries} | ConvertTo-Json -Depth 8
    $temporaryState=Join-Path $backupFolder 'installed.json'
    [IO.File]::WriteAllText($temporaryState,$stateText,[Text.UTF8Encoding]::new($false))
    [IO.File]::Move($temporaryState,$statePath)
} catch {
    foreach ($entry in $applied) {
        $path=Target $entry.target
        if ($entry.existed) {Copy-Item -LiteralPath (Join-Path $game $entry.backup) -Destination $path -Force}
        elseif (Test-Path -LiteralPath $path -PathType Leaf) {Remove-Item -LiteralPath $path}
    }
    throw
}
Write-Output 'Installed. Launch through Steam, select the native Default controller preset, enter a level and pause.'
Write-Output 'R3 / F8 opens the editor; X / F5 saves and applies. Personal profiles and game progress were kept.'
