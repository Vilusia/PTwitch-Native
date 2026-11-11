$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$logDirectory = Join-Path $projectRoot 'native/test-results'
New-Item -ItemType Directory -Force $logDirectory | Out-Null
function Invoke-WslLogged {
    param([string]$LogPath, [string[]]$WslArguments)
    $previousPreference = $ErrorActionPreference
    try {
        # Windows PowerShell can represent native stderr as ErrorRecords.
        # Capture them as build output; use the actual native exit status.
        $ErrorActionPreference = 'Continue'
        & wsl.exe @WslArguments 2>&1 | ForEach-Object { $_.ToString() } | Tee-Object -FilePath $LogPath | Out-Host
        $nativeExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    return $nativeExitCode
}
$snapshot = Join-Path $projectRoot 'native/upstream-source.tar'
$digest = (Get-FileHash -Algorithm SHA256 -LiteralPath $snapshot).Hash.ToLowerInvariant()
if ($digest -ne 'bdd47190f155725df6f8473a4df406b2d34c514a513339cb1ab2d0c06bca7591') {
    throw 'Pinned source snapshot checksum mismatch'
}
if (-not (Test-Path -LiteralPath "$projectRoot/upstream/komi-tube/vendor/tilefinch/CMakeLists.txt")) {
    New-Item -ItemType Directory -Force "$projectRoot/upstream/komi-tube" | Out-Null
    & tar -xf "$projectRoot/native/upstream-source.tar" -C "$projectRoot/upstream/komi-tube"
    if ($LASTEXITCODE -ne 0) { throw 'Could not extract pinned source snapshot' }
}
$windowsRoot = $projectRoot.Replace('\', '/')
$pathOutput = @(& wsl.exe -d Ubuntu --exec wslpath -a -u $windowsRoot)
$pathExitCode = $LASTEXITCODE
if ($pathExitCode -ne 0 -or $pathOutput.Count -ne 1 -or [string]::IsNullOrWhiteSpace([string]$pathOutput[0])) {
    throw "Could not convert the project path in WSL Ubuntu (exit $pathExitCode). Check that Ubuntu can access $projectRoot."
}
$linuxRoot = ([string]$pathOutput[0]).Trim()
if (-not $linuxRoot.StartsWith('/')) { throw "WSL returned an invalid Linux project path: $linuxRoot" }
$imageLog = Join-Path $logDirectory 'builder-image.log'
$imageExitCode = Invoke-WslLogged -LogPath $imageLog -WslArguments @('-d', 'Ubuntu', '--', 'docker', 'build', '-t', 'ptwitch-native-builder:1', '-f', "$linuxRoot/native/Dockerfile", "$linuxRoot/native")
if ($imageExitCode -ne 0) { throw "Native build environment failed. Full log: $imageLog" }
$buildLog = Join-Path $logDirectory 'psp-build.log'
$buildExitCode = Invoke-WslLogged -LogPath $buildLog -WslArguments @('-d', 'Ubuntu', '--', 'docker', 'run', '--rm', '-v', "${linuxRoot}:/work", 'ptwitch-native-builder:1', 'sh', '/work/native/build.sh')
if ($buildExitCode -ne 0) { throw "Native PSP build failed. Full log: $buildLog" }
Get-FileHash -Algorithm SHA256 -LiteralPath "$projectRoot/dist/PTwitch-native-psp.zip"
