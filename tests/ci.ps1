# Проверка собранных программ: сборка демонстрационного дистрибутива и
# сценарии интерфейса через псевдоконсоль (tests\scenarios).
#
#   powershell -ExecutionPolicy Bypass -File tests\ci.ps1 [-Bin build\Release]
#
# Сценарии устанавливают демонстрационную программу (файлы в %TEMP%, реестр HKCU,
# ярлык в меню «Пуск») и затем удаляют её. После удаления проверяется, что от
# установки ничего не осталось.

param([string]$Bin = 'build\Release')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = (Resolve-Path (Join-Path $root $Bin)).Path
$dist = Join-Path $root 'dist-ci'
$logs = Join-Path $root 'ci-logs'
$target = Join-Path $env:TEMP 'xpsetup-ci\Демо'
$productCode = '{6F1A2C3D-4B5E-4F60-8172-93A4B5C6D7E8}'
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$productCode"
$appKey = 'HKCU:\Software\roosslan\XPSetup Demo'
$shortcuts = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\XPSetup Demo'
$failed = $false

function Fail([string]$message) {
    Write-Host "ОШИБКА: $message" -ForegroundColor Red
    $script:failed = $true
}

function Check([bool]$condition, [string]$message) {
    if ($condition) { Write-Host "  OK: $message" } else { Fail $message }
}

function Run-Scenario([string]$name) {
    Write-Host "--- Сценарий $name"
    & (Join-Path $bin 'tuitest.exe') (Join-Path $PSScriptRoot "scenarios\$name.txt")
    if ($LASTEXITCODE -ne 0) { Fail "сценарий $name не выполнен" }
}

Remove-Item $dist, $logs -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $logs | Out-Null

Write-Host '--- Сборка демонстрационного дистрибутива'
& (Join-Path $bin 'xpsbuild.exe') --build (Join-Path $root 'example\demo.xsp') --mode both --out $dist
if ($LASTEXITCODE -ne 0) { throw 'Сборка дистрибутива не выполнена' }
$single = Get-ChildItem $dist -Filter '*_setup.exe' | Select-Object -First 1
if (-not $single) { throw 'Не найден однофайловый дистрибутив' }

$env:XPS_BIN = $bin
$env:XPS_DIST = $dist
$env:XPS_SINGLE = $single.Name
$env:XPS_TARGET = $target
$env:XPS_LOG = Join-Path $logs 'setup.log'
$env:XPS_PROJECT = Join-Path $root 'example\demo.xsp'

# Остатки прошлого запуска (локально) мешают сценарию установки.
if (Test-Path $uninstallKey) { throw "Демонстрационная программа уже установлена: $uninstallKey" }

Run-Scenario 'exit'
Run-Scenario 'install'
Check (Test-Path (Join-Path $target 'uninst.exe')) 'установлен uninst.exe'
Check (Test-Path (Join-Path $target 'Документация\manual.txt')) 'установлен второй компонент'
Check (-not (Test-Path (Join-Path $target 'demo.pdb'))) 'исключённый файл не установлен'
Check (Test-Path (Join-Path $target 'setup.log')) 'копия журнала в директории установки'
Check (Test-Path $uninstallKey) 'запись в «Программах и компонентах»'
Check ((Get-ItemProperty $appKey -ErrorAction SilentlyContinue).InstallPath -eq $target) 'значение реестра'
Check (Test-Path (Join-Path $shortcuts 'Прочти меня.lnk')) 'ярлык в меню «Пуск»'

Run-Scenario 'reinstall'
Run-Scenario 'uninstall'
Check (-not (Test-Path (Split-Path $target))) 'директория установки удалена'
Check (-not (Test-Path $uninstallKey)) 'запись в «Программах и компонентах» удалена'
Check (-not (Test-Path $appKey)) 'значения реестра удалены'
Check (-not (Test-Path $shortcuts)) 'ярлыки удалены'

Run-Scenario 'builder'

$log = Get-Content $env:XPS_LOG -Encoding UTF8
Check (($log | Select-String -SimpleMatch 'ОШИБКА').Count -eq 0) 'в журнале нет ошибок'
Check (($log | Select-String -SimpleMatch 'Код завершения: 0').Count -ge 4) 'коды завершения в журнале'

if ($failed) {
    Write-Host '--- Журнал программы установки'
    $log | ForEach-Object { Write-Host $_ }
    exit 1
}
Write-Host 'Все проверки выполнены'
