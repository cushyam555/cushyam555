# Run from a Visual Studio x64 Native Tools shell with Qt 5 MSVC x64 on PATH.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
foreach ($tool in @('qmake', 'nmake', 'windeployqt', 'cl')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing tool: $tool" }
}
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
$build = Join-Path $root 'build-windows'
$stage = Join-Path $root 'dist-windows'
$testBuild = Join-Path $root 'build-windows-tests'
if ($env:VSCMD_ARG_TGT_ARCH -and $env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    throw 'Use the x64 Native Tools shell, not x86.'
}
$qtVersion = & qmake -query QT_VERSION
if ($LASTEXITCODE -ne 0 -or $qtVersion -notmatch '^5\.') { throw 'Qt 5 is required (recommended: 5.15.2 MSVC x64).' }
New-Item -ItemType Directory -Force $build, $stage, $testBuild | Out-Null
Push-Location $build
try {
    Invoke-Checked 'qmake' @((Join-Path $root 'HandyKaraoke.pro'), 'CONFIG+=release', 'CONFIG-=debug')
    Invoke-Checked 'nmake' @('/NOLOGO')
    Copy-Item 'release/HandyKaraoke.exe' $stage -Force
} finally { Pop-Location }
foreach ($library in @('bass24', 'bassmidi24', 'bass_fx24', 'bassmix24', 'bass_vst24')) {
    Copy-Item "$root/BASS/$library/x64/*.dll" $stage -Force
}
Copy-Item "$root/3rdParty/WinSparkle/x64/WinSparkle.dll" $stage -Force
Copy-Item "$root/3rdParty/VSTChecker_Qt_5_12_5_MSVC2017/VSTChecker_x64.exe" $stage -Force
Copy-Item "$root/Style.ini", "$root/LICENSE", "$root/UPSTREAM.md" $stage -Force
New-Item -ItemType Directory -Force (Join-Path $stage 'languages') | Out-Null
Copy-Item "$root/languages/en.qm" (Join-Path $stage 'languages') -Force
# Keep all upstream notices, including the helper and WinSparkle licenses.
$notices = Join-Path $stage 'third-party-notices'
New-Item -ItemType Directory -Force $notices | Out-Null
foreach ($tree in @('BASS', '3rdParty')) {
    Get-ChildItem (Join-Path $root $tree) -Recurse -File |
        Where-Object { $_.Extension -in @('.txt', '.TXT', '.md') -or $_.Name -match 'LICENSE|COPYING|COPYRIGHT' } |
        ForEach-Object {
            $relative = $_.FullName.Substring($root.Length + 1)
            $destination = Join-Path $notices $relative
            New-Item -ItemType Directory -Force (Split-Path $destination -Parent) | Out-Null
            Copy-Item $_.FullName $destination -Force
        }
}
@'
[Paths]
Prefix=.
Plugins=.
'@ | Set-Content -Path (Join-Path $stage 'qt.conf') -Encoding ascii
Push-Location $stage
try { Invoke-Checked 'windeployqt' @('--release', '--compiler-runtime', '--no-translations', 'HandyKaraoke.exe') }
finally { Pop-Location }
# Deploy the MSVC CRT app-locally so end users do not need Visual Studio.
if (-not $env:VCToolsRedistDir) { throw 'VCToolsRedistDir is missing; use an x64 Native Tools shell.' }
$crtFiles = @(Get-ChildItem "$env:VCToolsRedistDir/x64/Microsoft.VC*.CRT/*.dll" -File)
if ($crtFiles.Count -eq 0) { throw 'Microsoft x64 CRT redistributable DLLs not found.' }
$crtFiles | Copy-Item -Destination $stage -Force
# Run integration tests against the same x64 BASS binaries we ship.
Push-Location $testBuild
try {
    Invoke-Checked 'qmake' @((Join-Path $root 'tests/core.pro'), 'CONFIG+=release', 'CONFIG-=debug')
    Invoke-Checked 'nmake' @('/NOLOGO')
    $oldPath = $env:PATH
    try {
        $env:PATH = "$stage;$oldPath"
        Invoke-Checked (Join-Path $testBuild 'release/bt-core-tests.exe') @('-o', ((Join-Path $testBuild 'results.txt') + ',txt'))
    } finally { $env:PATH = $oldPath }
} finally { Pop-Location }
$info = [System.Diagnostics.ProcessStartInfo]::new()
$info.FileName = Join-Path $stage 'HandyKaraoke.exe'
$info.Arguments = '--smoke-test'
$info.WorkingDirectory = $stage
$info.UseShellExecute = $false
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
$info.EnvironmentVariables['QT_FORCE_STDERR_LOGGING'] = '1'
$process = [System.Diagnostics.Process]::Start($info)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
if (-not $process.WaitForExit(30000)) { $process.Kill(); throw 'Application smoke test timed out' }
Write-Host $stdout.GetAwaiter().GetResult()
Write-Host $stderr.GetAwaiter().GetResult()
if ($process.ExitCode -ne 0) { throw "Application smoke test failed: $($process.ExitCode)" }
Write-Host "Staged and tested application: $stage"
Write-Host 'To build the installer: ISCC.exe packaging/bt-x64.iss'
