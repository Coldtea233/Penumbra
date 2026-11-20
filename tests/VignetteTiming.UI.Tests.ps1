param([Parameter(Mandatory)][string]$ExecutablePath)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$exe = (Resolve-Path -LiteralPath $ExecutablePath).Path
$config = Join-Path $env:LOCALAPPDATA 'Nocturne\settings.ini'
$log = Join-Path $PSScriptRoot 'bin\VignetteTimingStartup.txt'
New-Item -ItemType Directory -Force -Path (Split-Path $log) | Out-Null
function ConfigHash { if (Test-Path -LiteralPath $config) { (Get-FileHash -LiteralPath $config).Hash } }
$hash = ConfigHash
$app = Start-Process -FilePath $exe -ArgumentList '--layout-test', ('"--startup-log='+$log+'"') -PassThru -WindowStyle Hidden
try {
    for ($attempt=0; $attempt -lt 60; $attempt++) {
        $app.Refresh()
        if ($app.HasExited) { throw 'Isolated test application exited.' }
        if ($app.MainWindowHandle -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 100
    }
    $root = [Windows.Automation.AutomationElement]::FromHandle($app.MainWindowHandle)
    function Find-Control($property, [string]$value) {
        $condition = [Windows.Automation.PropertyCondition]::new($property, $value)
        for ($attempt=0; $attempt -lt 40; $attempt++) {
            $item = $root.FindFirst([Windows.Automation.TreeScope]::Descendants, $condition)
            if ($item) { return $item }
            Start-Sleep -Milliseconds 100
        }
        throw "Missing control: $value. See $log"
    }
    function Invoke-Control($element) { $element.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke() }
    function Name-Control([string]$name) { Find-Control ([Windows.Automation.AutomationElement]::NameProperty) $name }
    function Open-Timing { Invoke-Control (Name-Control '高级设置'); $null = Name-Control '动画速率设置' }
    function Range([string]$name) { (Name-Control ($name + '时长（毫秒）')).GetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern) }
    function Check-Numbers($values) {
        $actual = @('快速','均衡','优雅' | ForEach-Object { (Range $_).Current.Value })
        if (($actual -join ',') -ne ($values -join ',')) { throw "Expected $values but saw $actual" }
    }
    $speedSlider = Name-Control '动画速率（快速、均衡、优雅）'
    if (!(Test-Path -LiteralPath $config) -or !(Select-String -LiteralPath $config -Pattern '^vignetteSpeed\s*=' -Quiet)) {
        $defaultSpeed = $speedSlider.GetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern).Current.Value
        if ($defaultSpeed -ne 1) { throw "Expected Balanced as the default animation speed, received $defaultSpeed" }
    }
    Open-Timing
    Invoke-Control (Name-Control '恢复默认时长')
    Check-Numbers @(520,800,1200)
    (Range '快速').SetValue(0)
    (Range '均衡').SetValue(444)
    (Range '优雅').SetValue(1380)
    Check-Numbers @(0,444,1380)
    foreach ($speed in @('快速','均衡','优雅')) {
        foreach ($direction in @('淡入','淡出')) {
            Invoke-Control (Name-Control ($speed + $direction))
            Start-Sleep -Milliseconds 100
            $app.Refresh()
            if ($app.HasExited) { throw "Playback failed for $speed$direction" }
        }
    }
    Invoke-Control (Name-Control '保存')
    Start-Sleep -Milliseconds 400
    Open-Timing
    Check-Numbers @(0,444,1380)
    Invoke-Control (Name-Control '恢复默认时长')
    Invoke-Control (Name-Control '取消')
    Start-Sleep -Milliseconds 400
    Open-Timing
    Check-Numbers @(0,444,1380)
    Invoke-Control (Name-Control '取消')
    'PASS: three editable timings, six preview commands, reset, accept, and cancel.'
} finally {
    if (!$app.HasExited) { $null = $app.CloseMainWindow(); $null = $app.WaitForExit(3000) }
    if ((ConfigHash) -ne $hash) { throw 'Preview altered saved settings.' }
}
