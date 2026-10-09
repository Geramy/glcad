param(
    [Parameter(Mandatory = $true)][string]$PackageDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT') { throw 'This test requires native Windows.' }

$packages = (Resolve-Path -LiteralPath $PackageDirectory).Path
$setups = @(Get-ChildItem -LiteralPath $packages -Filter 'glcad-*-win64.exe')
$archives = @(Get-ChildItem -LiteralPath $packages -Filter 'glcad-*-win64.zip')
if ($setups.Count -ne 1 -or $archives.Count -ne 1) {
    throw 'Provide exactly one Windows setup EXE and its portable ZIP.'
}
if ($setups[0].BaseName -ne $archives[0].BaseName) {
    throw 'Setup and portable ZIP versions must match.'
}

# Use a disposable Windows runner: setup creates machine-wide registration.
$work = Join-Path ([System.IO.Path]::GetTempPath()) ('glcad-setup-' + [guid]::NewGuid())
$portable = Join-Path $work 'portable'
$installed = Join-Path $work 'installed app'
$uninstaller = Join-Path $installed 'Uninstall.exe'
New-Item -ItemType Directory -Path $work | Out-Null
try {
    Expand-Archive -LiteralPath $archives[0].FullName -DestinationPath $portable
    $roots = @(Get-ChildItem -LiteralPath $portable -Directory)
    if ($roots.Count -ne 1) { throw 'Expected one application root in the ZIP.' }
    $source = $roots[0].FullName
    if (-not (Test-Path -LiteralPath (Join-Path $source 'glcad.exe'))) {
        throw 'Portable application executable is missing.'
    }

    # NSIS requires /D to be last, with the remaining path left unquoted.
    $setup = Start-Process -FilePath $setups[0].FullName -ArgumentList "/S /D=$installed" -Wait -PassThru
    if ($setup.ExitCode -ne 0) { throw "Setup exited with $($setup.ExitCode)." }
    if (-not (Test-Path -LiteralPath $uninstaller)) { throw 'Uninstaller is missing.' }

    $files = @(Get-ChildItem -LiteralPath $source -File -Recurse)
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($source.Length + 1)
        $destination = Join-Path $installed $relative
        if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
            throw "Installed payload is missing: $relative"
        }
        if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne
            (Get-FileHash -LiteralPath $destination).Hash) {
            throw "Installed payload differs from ZIP: $relative"
        }
    }
    Write-Output "PASS: installed $($files.Count) payload files with matching SHA-256 hashes."

    $remove = Start-Process -FilePath $uninstaller -ArgumentList '/S' -Wait -PassThru
    if ($remove.ExitCode -ne 0) { throw "Uninstaller exited with $($remove.ExitCode)." }
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ((Test-Path -LiteralPath $installed) -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 250
    }
    if (Test-Path -LiteralPath $installed) { throw 'Uninstall left the application directory behind.' }
    Write-Output 'PASS: native Windows silent install, payload verification and uninstall.'
} finally {
    if (Test-Path -LiteralPath $uninstaller) {
        Start-Process -FilePath $uninstaller -ArgumentList '/S' -Wait | Out-Null
    }
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
