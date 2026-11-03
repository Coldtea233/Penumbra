param([Parameter(Mandatory)][string]$PayloadPath,[Parameter(Mandatory)][string]$PublishDirectory,[string]$ExpectedVersion='0.7.0')
$ErrorActionPreference='Stop'
$payload=(Resolve-Path -LiteralPath $PayloadPath).Path
$publish=(Resolve-Path -LiteralPath $PublishDirectory).Path
$temp=Join-Path ([IO.Path]::GetTempPath()) ('Penumbra-payload-check-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null
Add-Type @'
using System;using System.IO;using System.Runtime.InteropServices;
public static class CabinetFromMsi {
 [DllImport("msi.dll",CharSet=CharSet.Unicode)]static extern uint MsiOpenDatabase(string p,IntPtr mode,out uint db);
 [DllImport("msi.dll",CharSet=CharSet.Unicode)]static extern uint MsiDatabaseOpenView(uint db,string sql,out uint view);
 [DllImport("msi.dll")]static extern uint MsiViewExecute(uint view,uint record);
 [DllImport("msi.dll")]static extern uint MsiViewFetch(uint view,out uint record);
 [DllImport("msi.dll")]static extern uint MsiRecordReadStream(uint record,uint field,byte[] bytes,ref uint count);
 [DllImport("msi.dll")]static extern uint MsiCloseHandle(uint h);
 static void Check(uint result){if(result!=0)throw new InvalidOperationException("MSI read error "+result);}
 public static void Extract(string msi,string path){uint db=0,view=0,record=0;try{
   Check(MsiOpenDatabase(msi,IntPtr.Zero,out db));Check(MsiDatabaseOpenView(db,"SELECT `Data` FROM `_Streams` WHERE `Name`='payload.cab'",out view));Check(MsiViewExecute(view,0));Check(MsiViewFetch(view,out record));
   using(var output=new FileStream(path,FileMode.CreateNew,FileAccess.Write)){byte[] bytes=new byte[65536];while(true){uint count=(uint)bytes.Length;Check(MsiRecordReadStream(record,1,bytes,ref count));if(count==0)break;output.Write(bytes,0,(int)count);}}
 }finally{if(record!=0)MsiCloseHandle(record);if(view!=0)MsiCloseHandle(view);if(db!=0)MsiCloseHandle(db);}}
}
'@
$installer=New-Object -ComObject WindowsInstaller.Installer
$database=$installer.OpenDatabase($payload,0)
function Rows([string]$sql,[int]$count){
    $view=$database.OpenView($sql);[void]$view.Execute()
    try {while($record=$view.Fetch()){$values=@();for($field=1;$field -le $count;$field++){$values+=$record.StringData($field)};Write-Output -NoEnumerate $values}}
    finally {[void]$view.Close()}
}
try {
    $properties=@{};foreach($row in (Rows 'SELECT `Property`,`Value` FROM `Property`' 2)){$properties[$row[0]]=$row[1]}
    if($properties.ProductVersion -ne $ExpectedVersion -or $properties.UpgradeCode -ne '{0CB1971B-FE53-4E66-9699-C87884CB2D4E}' -or $properties.MSIINSTALLPERUSER -ne '1' -or $properties.ContainsKey('ALLUSERS')){throw 'Installer identity or per-user policy mismatch.'}
    $directories=@{};foreach($row in (Rows 'SELECT `Directory`,`Directory_Parent`,`DefaultDir` FROM `Directory`' 3)){$directories[$row[0]]=@{Parent=$row[1];Name=($row[2].Split('|')[-1])}}
    function Relative([string]$directory){if($directory -eq 'INSTALLFOLDER'){return ''};$item=$directories[$directory];if(!$item){throw "Unknown directory $directory"};$parent=Relative $item.Parent;if($parent){return (Join-Path $parent $item.Name)};return $item.Name}
    $components=@{};foreach($row in (Rows 'SELECT `Component`,`Directory_` FROM `Component`' 2)){$components[$row[0]]=$row[1]}
    $cab=Join-Path $temp 'payload.cab';[CabinetFromMsi]::Extract($payload,$cab)
    & "$env:WINDIR/System32/expand.exe" $cab '-F:*' $temp | Out-Null
    if($LASTEXITCODE){throw 'Cabinet extraction failed.'}
    $verified=0
    foreach($row in (Rows 'SELECT `File`,`Component_`,`FileName` FROM `File`' 3)){
        $folder=Relative $components[$row[1]];$name=$row[2].Split('|')[-1]
        $relative=if($folder){Join-Path $folder $name}else{$name}
        if($relative -match '(^|\\)(settings\.ini|status\.txt|ui-error\.txt|verification|animation-trace\.csv)($|\\)'){throw "Private artifact in payload: $relative"}
        if([int]$properties.PENUMBRA_PATH_LIMIT+1+$relative.Length -ge 260){throw 'Path limit does not protect the payload.'}
        if((Get-FileHash (Join-Path $temp $row[0])).Hash -ne (Get-FileHash (Join-Path $publish $relative)).Hash){throw "Packaged file mismatch: $relative"}
        $verified++
    }
    if($verified -ne @(Get-ChildItem -LiteralPath $publish -File -Recurse).Count){throw 'Payload and staging file counts differ.'}
    $features=@{};foreach($row in @(Rows 'SELECT `Feature`,`Level` FROM `Feature`' 2)){$features[$row[0]]=[int]$row[1]}
    if($features.Count -ne 1 -or $features.Main -ne 1){throw 'Installer must contain only the Main feature.'}
    if(@(Rows 'SELECT `Shortcut` FROM `Shortcut`' 1).Count){throw 'Shortcut creation must be completely removed.'}
    foreach($component in @('C_DesktopShortcut','C_StartMenuShortcut')){if($components.ContainsKey($component)){throw "Removed shortcut component remains: $component"}}
    foreach($directory in @('DesktopFolder','MenuFolder','ProgramMenuFolder')){if($directories.ContainsKey($directory)){throw "Removed shortcut directory remains: $directory"}}
    $upgrades=@(Rows 'SELECT `VersionMin`,`VersionMax`,`Attributes`,`ActionProperty` FROM `Upgrade`' 4)
    if(!($upgrades|Where-Object {$_[0] -eq '0.0.0' -and $_[1] -eq $ExpectedVersion -and $_[2] -eq '257' -and $_[3] -eq 'OLDPRODUCTS'})){throw 'Legacy version upgrade rule missing.'}
    if(!($upgrades|Where-Object {$_[0] -eq $ExpectedVersion -and $_[2] -eq '258' -and $_[3] -eq 'NEWERPRODUCTFOUND'})){throw 'Downgrade protection missing.'}
    $actions=@{};foreach($row in (Rows 'SELECT `Action`,`Type`,`Source`,`Target` FROM `CustomAction`' 4)){$actions[$row[0]]=$row}
    foreach($name in @('CheckEnvironment','ResolveInstallDirectory','RememberInstallDirectory','PrepareStartupMigration','RollbackMigratedStartup','MigrateOwnedStartup','RemoveOwnedStartup','RollbackOwnedStartup')){if(!$actions.ContainsKey($name)){throw "Missing action: $name"}}
    if($actions.RememberInstallDirectory[1] -ne '51' -or $actions.RememberInstallDirectory[2] -ne 'ARPINSTALLLOCATION' -or $actions.RememberInstallDirectory[3] -ne '[INSTALLFOLDER]'){throw 'Directory persistence is not correctly authored.'}
    foreach($table in @('InstallExecuteSequence','InstallUISequence')){
        $sequence=@{};foreach($row in (Rows ('SELECT `Action`,`Sequence` FROM `'+$table+'`') 2)){$sequence[$row[0]]=[int]$row[1]}
        if($sequence.ResolveInstallDirectory -ge $sequence.CostInitialize -or $sequence.RememberInstallDirectory -le $sequence.CostFinalize){throw 'Directory resolution/persistence has the wrong sequence.'}
        if($table -eq 'InstallExecuteSequence'){
            if($sequence.RemoveExistingProducts -ne $sequence.InstallInitialize+1 -or $sequence.PrepareStartupMigration -ge $sequence.RemoveExistingProducts -or $sequence.RollbackMigratedStartup -ge $sequence.MigrateOwnedStartup -or $sequence.MigrateOwnedStartup -le $sequence.InstallFiles){throw 'Upgrade/startup rollback sequence is unsafe.'}
        }
    }
    [pscustomobject]@{Passed=$true;FilesVerifiedBySha256=$verified;Version=$properties.ProductVersion;PerUser=$true;InstallPathLimit=$properties.PENUMBRA_PATH_LIMIT;UpgradeAndDowngradeRules=$true;InstallDirectoryPersistence=$true;StartupMigrationAndRollback=$true;PerformedInstall=$false;PerformedUninstall=$false}|ConvertTo-Json -Compress
} finally {
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database)
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($installer)
    foreach($file in Get-ChildItem -LiteralPath $temp -File){Remove-Item -LiteralPath $file.FullName}
    Remove-Item -LiteralPath $temp
}
