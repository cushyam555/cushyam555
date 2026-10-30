# Verify the installed package without Qt or developer directories on PATH.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$installer = Get-ChildItem "$root/installer-output/*-setup.exe" | Select-Object -First 1
if (-not $installer) { throw 'Installer not found' }
$destination = Join-Path $env:RUNNER_TEMP ('BT-Karaoke-check-' + [guid]::NewGuid().ToString('N'))
$setup = Start-Process $installer.FullName -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/DIR=`"$destination`"") -Wait -PassThru
if ($setup.ExitCode -ne 0) { throw "Installation failed: $($setup.ExitCode)" }
try {
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = Join-Path $destination 'HandyKaraoke.exe'
    $info.Arguments = '--smoke-test'
    $info.WorkingDirectory = $env:RUNNER_TEMP
    $info.UseShellExecute = $false
    $info.RedirectStandardError = $true
    $info.RedirectStandardOutput = $true
    $info.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    $info.EnvironmentVariables.Remove('QT_PLUGIN_PATH') | Out-Null
    $info.EnvironmentVariables.Remove('QT_QPA_PLATFORM_PLUGIN_PATH') | Out-Null
    $info.EnvironmentVariables['QT_FORCE_STDERR_LOGGING'] = '1'
    $app = [System.Diagnostics.Process]::Start($info)
    $stdout = $app.StandardOutput.ReadToEndAsync()
    $stderr = $app.StandardError.ReadToEndAsync()
    if (-not $app.WaitForExit(30000)) { $app.Kill(); throw 'Installed app timed out' }
    Write-Host $stdout.GetAwaiter().GetResult()
    Write-Host $stderr.GetAwaiter().GetResult()
    if ($app.ExitCode -ne 0) { throw "Installed app failed: $($app.ExitCode)" }
} finally {
    $uninstall = Join-Path $destination 'unins000.exe'
    if (Test-Path $uninstall) {
        $remove = Start-Process $uninstall -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -Wait -PassThru
        if ($remove.ExitCode -ne 0) { throw "Uninstallation failed: $($remove.ExitCode)" }
    }
}
if (Test-Path (Join-Path $destination 'HandyKaraoke.exe')) { throw 'Application remains after uninstall' }
Write-Host 'BT_INSTALLER_CHECK PASS: install, isolated startup and uninstall'
