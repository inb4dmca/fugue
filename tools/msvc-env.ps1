# Return the x64 MSVC environment as JSON, without changing the caller's environment.
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
if ($env:VSCMD_ARG_TGT_ARCH -eq "x64") {
    $lines = Get-ChildItem Env: | ForEach-Object { "$($_.Name)=$($_.Value)" }
} else {
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vs) { throw "Visual Studio with Desktop development with C++ is required." }
    $lines = & $env:ComSpec /d /c "call `"$vs/Common7/Tools/VsDevCmd.bat`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) { throw "Could not initialize x64 MSVC." }
}
$result = @{}
foreach ($line in $lines) {
    if ($line -match '^([^=]+)=(.*)$') { $result[$Matches[1]] = $Matches[2] }
}
ConvertTo-Json -InputObject $result -Compress
