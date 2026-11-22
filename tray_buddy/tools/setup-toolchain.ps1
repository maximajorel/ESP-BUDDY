#Requires -Version 5.1
<#
.SYNOPSIS
  Instala el toolchain que tray_buddy necesita en Windows.

.DESCRIPTION
  Dos piezas:
    1. Rust (rustup) con el toolchain MSVC.
    2. Visual Studio Build Tools con la carga de C++: el linker y el Windows SDK
       que Rust usa para enlazar en Windows.

  Existe porque pegar los argumentos del instalador de VS a mano en una
  terminal es frágil: PowerShell conserva el salto de línea dentro de las
  comillas, así que un argumento partido en dos líneas llega mutilado al
  instalador y setup.exe responde 87 (parámetro inválido). Acá los argumentos
  son un arreglo, sin comillas ni saltos de por medio.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\setup-toolchain.ps1

.NOTES
  Necesita una consola elevada: Build Tools pide administrador. Rust no.
#>

$ErrorActionPreference = 'Stop'

$vcToolsComponent = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
$cargoBin = Join-Path $env:USERPROFILE '.cargo\bin'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

function Test-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Report([string]$Text, [string]$State = 'ok') {
    $tag = switch ($State) { 'ok' { '[ ok ]' } 'warn' { '[warn]' } 'fail' { '[fail]' } }
    Write-Host "$tag $Text"
}

# ── 1 · Rust ────────────────────────────────────────────────────────────────
$rustup = Join-Path $cargoBin 'rustup.exe'
$cargo = Join-Path $cargoBin 'cargo.exe'
if (Test-Path $cargo) {
    $version = (& $cargo --version) -join ' '
    Report "Rust ya instalado: $version"
} else {
    Report 'Rust no está: bajando rustup-init.exe' 'warn'
    $rustupInit = Join-Path $env:TEMP 'rustup-init.exe'
    Invoke-WebRequest -Uri 'https://win.rustup.rs/x86_64' -OutFile $rustupInit
    # -y: sin preguntas · --default-toolchain: la que usa Tauri en Windows
    $proc = Start-Process -Wait -PassThru -FilePath $rustupInit -ArgumentList @(
        '-y', '--default-toolchain', 'stable-msvc', '--profile', 'minimal'
    )
    if ($proc.ExitCode -ne 0) {
        Report "rustup-init terminó con $($proc.ExitCode)" 'fail'
        exit 1
    }
    Report 'Rust instalado'
}

# ── 2 · Visual Studio Build Tools ───────────────────────────────────────────
$hasVC = $false
if (Test-Path $vswhere) {
    $found = & $vswhere -products * -requires $vcToolsComponent -property installationPath
    if ($found) {
        $hasVC = $true
        Report "Build Tools ya instalados en: $found"
    }
}

if (-not $hasVC) {
    if (-not (Test-Admin)) {
        Report 'Build Tools falta y esta consola no es administrador: abril como administrador y volvé a correrlo' 'fail'
        exit 1
    }

    $bootstrapper = Join-Path $env:TEMP 'vs_buildtools.exe'
    if (-not (Test-Path $bootstrapper)) {
        Report 'Bajando el instalador de Build Tools' 'warn'
        Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vs_buildtools.exe' -OutFile $bootstrapper
    }

    # Un arreglo, no una cadena: así ningún salto de línea puede romper un
    # argumento (el error 87 de la instalación hecha a mano).
    # --includeRecommended trae el Windows SDK, que Rust necesita para enlazar.
    # --passive muestra la ventana de progreso del instalador (sin preguntas):
    # 20 minutos de silencio total se leen como un colgado.
    $installArgs = @(
        '--passive'
        '--wait'
        '--norestart'
        '--nocache'
        '--add'
        'Microsoft.VisualStudio.Workload.VCTools'
        '--includeRecommended'
    )

    Report 'Instalando Build Tools con la carga de C++ (tarda 10-20 minutos)' 'warn'
    $proc = Start-Process -Wait -PassThru -FilePath $bootstrapper -ArgumentList $installArgs
    if ($proc.ExitCode -ne 0 -and $proc.ExitCode -ne 3010) {
        Report "el instalador de VS terminó con $($proc.ExitCode) (los logs están en %TEMP%\dd_*.log)" 'fail'
        exit 1
    }
    if ($proc.ExitCode -eq 3010) {
        Report 'Build Tools instalado; pide reiniciar la máquina' 'warn'
    } else {
        Report 'Build Tools instalado'
    }
}

# ── 3 · Verificación ────────────────────────────────────────────────────────
$link = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -products * -requires $vcToolsComponent -property installationPath
    if ($vsPath) {
        $link = Get-ChildItem -Path $vsPath -Recurse -Filter 'link.exe' -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match 'Hostx64\\x64' } |
            Select-Object -First 1
    }
}

$failed = $false
if (Test-Path $cargo) { Report "cargo: $cargo" } else { Report 'cargo no aparece' 'fail'; $failed = $true }
if ($link) {
    Report "link.exe: $($link.FullName)"
} else {
    Report 'link.exe no aparece: Build Tools no quedó completo' 'fail'
    $failed = $true
}

if ($failed) { exit 1 }

Write-Host ''
Report 'Toolchain listo. Compilá con:  cd tray_buddy; npm install; npm run build'
