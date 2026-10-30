$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
function Run([string]$command,[string[]]$arguments) {
    & $command @arguments
    if ($LASTEXITCODE -ne 0) { throw "$command failed: $LASTEXITCODE" }
}
Run 'cmake' @('-S', '.', '-B', 'build', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$env:QT_ROOT_DIR")
Run 'cmake' @('--build', 'build', '--target', 'BTKaraoke', 'bt_tests', '--parallel', '3')
Run 'ctest' @('--test-dir', 'build', '--output-on-failure')
New-Item -ItemType Directory -Force dist | Out-Null
Copy-Item build/BTKaraoke.exe,build/BTKaraokeApp.exe dist -Force
Run 'windeployqt' @('--release','--compiler-runtime','--no-translations','dist/BTKaraokeApp.exe')
if (-not (Test-Path dist/sqldrivers/qsqlite.dll)) { throw 'SQLite driver was not deployed' }
$crt = @(Get-ChildItem "$env:VCToolsRedistDir/x64/Microsoft.VC*.CRT/*.dll" -File)
if (-not $crt.Count) { throw 'MSVC runtime not found' }
$crt | Copy-Item -Destination dist -Force
Copy-Item LICENSE,THIRD_PARTY.md,README.md dist -Force
Copy-Item LICENSES dist -Recurse -Force
Copy-Item build/licenses dist/dependency-licenses -Recurse -Force
@'
[Paths]
Prefix=.
Plugins=.
'@ | Set-Content dist/qt.conf -Encoding ascii
function CheckApp([string]$path, [string]$mode = '--smoke-test', [bool]$expectFailure = $false) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $path
    $info.Arguments = $mode
    $info.UseShellExecute = $false
    $info.WorkingDirectory = $env:RUNNER_TEMP
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    $info.EnvironmentVariables.Remove('QT_PLUGIN_PATH')
    $info.EnvironmentVariables.Remove('QT_QPA_PLATFORM_PLUGIN_PATH')
    $info.EnvironmentVariables['QT_FORCE_STDERR_LOGGING'] = '1'
    $p = [Diagnostics.Process]::Start($info)
    $stdout = $p.StandardOutput.ReadToEndAsync()
    $stderr = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit(30000)) { $p.Kill(); throw 'UI smoke test timeout' }
    $text = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
    Write-Host $text
    if ($expectFailure) {
        if ($p.ExitCode -eq 0) { throw 'Launcher did not detect the missing Qt runtime' }
        return
    }
    $marker = if ($mode -eq '--startup-test') { 'BT_STARTUP_VISIBLE PASS' } else { 'BT_UI_SMOKE PASS' }
    if ($p.ExitCode -ne 0 -or $text -notmatch $marker) { throw "Application startup failed: $($p.ExitCode)" }
}
CheckApp (Join-Path $root 'dist/BTKaraoke.exe')
CheckApp (Join-Path $root 'dist/BTKaraoke.exe') '--startup-test'
# The static native launcher must report a missing Qt runtime instead of disappearing.
Rename-Item dist/Qt6Core.dll Qt6Core.dll.unavailable
try {
    CheckApp (Join-Path $root 'dist/BTKaraoke.exe') '--smoke-test' $true
} finally { Rename-Item dist/Qt6Core.dll.unavailable Qt6Core.dll }

Run "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" @('packaging/bt.iss')
$setup = Get-Item installer-output/BTKaraoke-0.4.0-Windows-x64-setup.exe
$install = Join-Path $env:RUNNER_TEMP ('BT-Studio-' + [guid]::NewGuid().ToString('N'))
$p = Start-Process $setup.FullName -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/DIR=`"$install`"") -Wait -PassThru
if ($p.ExitCode -ne 0) { throw 'Installer failed' }
try {
    CheckApp (Join-Path $install 'BTKaraoke.exe')
    CheckApp (Join-Path $install 'BTKaraoke.exe') '--startup-test'
}
finally {
    $p = Start-Process (Join-Path $install 'unins000.exe') -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw 'Uninstaller failed' }
}
if (Test-Path (Join-Path $install 'BTKaraoke.exe')) { throw 'Uninstall left executable behind' }
Get-FileHash $setup.FullName -Algorithm SHA256 | Format-List | Out-File installer-output/SHA256.txt
Write-Host 'BT_WINDOWS_PACKAGE PASS'
