$ErrorActionPreference = 'Stop'

$qtAlias = $env:QT_ROOT
$compilerRoot = $env:MINGW_ROOT
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectDrive = if ($env:SPARAM_PROJECT_DRIVE) { $env:SPARAM_PROJECT_DRIVE } else { 'P:' }

if ([string]::IsNullOrWhiteSpace($qtAlias) -or [string]::IsNullOrWhiteSpace($compilerRoot)) {
    throw 'Set QT_ROOT and MINGW_ROOT before running this build script.'
}

if (-not (Test-Path -LiteralPath "$qtAlias\6.8.3\mingw_64")) {
    throw "Qt alias not found: $qtAlias"
}
if (-not (Test-Path -LiteralPath "$compilerRoot\bin\g++.exe")) {
    throw "MinGW compiler not found: $compilerRoot"
}

$existing = (subst) | Select-String '^P:'
if (-not $existing) {
    subst $projectDrive $projectRoot
} elseif (-not (Test-Path -LiteralPath "$projectDrive\gui\CMakeLists.txt")) {
    throw 'P: is already mapped to a different directory.'
}

$env:Path = "$compilerRoot\bin;$qtAlias\Tools\CMake_64\bin;$qtAlias\Tools\Ninja;" +
            "$qtAlias\6.8.3\mingw_64\bin;$env:Path"
$compilerForCmake = $compilerRoot.Replace([char]92, [char]47)
$qtForCmake = $qtAlias.Replace([char]92, [char]47)

if (-not (Test-Path -LiteralPath "$projectDrive\build-gui-verified\CMakeCache.txt")) {
    & "$qtAlias\Tools\CMake_64\bin\cmake.exe" `
        -S "$projectDrive\gui" `
        -B "$projectDrive\build-gui-verified" `
        -G Ninja `
        -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_CXX_COMPILER=$compilerForCmake/bin/g++.exe" `
        "-DCMAKE_PREFIX_PATH=$qtForCmake/6.8.3/mingw_64"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

& "$qtAlias\Tools\CMake_64\bin\cmake.exe" `
    --build "$projectDrive\build-gui-verified" --parallel 2
if ($LASTEXITCODE) { exit $LASTEXITCODE }

& "$qtAlias\6.8.3\mingw_64\bin\windeployqt.exe" `
    --release --no-translations "$projectDrive\build-gui-verified\sparam_gui.exe"
exit $LASTEXITCODE
