param([Parameter(Mandatory)][string]$SamplesPath,[Parameter(Mandatory)][string]$OutputPath)
$ErrorActionPreference='Stop'
$samples=Get-Content -LiteralPath $SamplesPath -Raw|ConvertFrom-Json
$summary=@()
foreach($mode in @('Baseline','BlurOff','BlurOn2','BlurOn12','BlurOn48')){
 $group=@($samples|Where-Object Mode -eq $mode);if(!$group.Count){continue}
 $bad=@($group|Where-Object{!$_.ForegroundStable -or ($mode -ne 'Baseline' -and (($_.BackendState -band 2) -eq 0 -or ($_.BackendState -band 24) -ne 0)) -or ($mode -like 'BlurOn*' -and ($_.BackendState -band 4) -eq 0) -or ($mode -eq 'BlurOff' -and ($_.BackendState -band 4) -ne 0)})
 $row=[ordered]@{Mode=$mode;Samples=$group.Count;Seconds=($group.ElapsedSeconds|Measure-Object -Sum).Sum;InvalidSamples=$bad.Count;ControlledState=($bad.Count -eq 0)}
 foreach($metric in @('SystemCpuPercent','SystemGpuBusyPercent')){$v=@($group|ForEach-Object{$_.$metric}|Where-Object{$null -ne $_});$row[$metric]=if($v.Count){($v|Measure-Object -Average).Average}else{$null}}
 foreach($process in @('Penumbra','Dwm','WallpaperEngine')){
  $metrics=[ordered]@{}
  foreach($metric in @('CpuPercent','WorkingSetMiB','PrivateMiB','GpuBusyPercent','DedicatedGpuMiB','SharedGpuMiB')){
   $v=@($group|ForEach-Object{$_.$process.$metric}|Where-Object{$null -ne $_});$metrics[$metric]=if($v.Count){($v|Measure-Object -Average).Average}else{$null}
  }
  $row[$process]=$metrics
 }
 $summary+=[pscustomobject]$row
}
$summary|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $OutputPath -Encoding utf8
$summary|ConvertTo-Json -Depth 8
