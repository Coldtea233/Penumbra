param(
    [string]$Toolchain=$env:LLVM_MINGW_ROOT,
    [string]$PublishDirectory=(Join-Path $PSScriptRoot '..\dist\Penumbra-Beta-0.3'),
    [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\dist\release-Beta-0.3')
)
$ErrorActionPreference='Stop'
if(!$Toolchain){throw 'Pass -Toolchain with the llvm-mingw x64 directory.'}
$publish=[IO.Path]::GetFullPath($PublishDirectory)
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(!(Test-Path -LiteralPath (Join-Path $publish 'app\Penumbra.exe'))){throw 'Build the complete application first.'}
New-Item -ItemType Directory -Force -Path $output|Out-Null
$buildDirectory=Join-Path $output ('build-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $buildDirectory|Out-Null
$msiPath=Join-Path $output 'Penumbra-Beta-0.3-Setup-x64.msi'
if(Test-Path -LiteralPath $msiPath){throw "Output already exists; choose another directory: $msiPath"}

# Use Windows' own MSI database/cabinet tools; no packager runtime is shipped.
Add-Type @'
using System;using System.Text;using System.Collections.Generic;using System.Runtime.InteropServices;
public static class PenumbraMsi {
 [DllImport("msi.dll",CharSet=CharSet.Unicode)] static extern uint MsiOpenDatabase(string path,IntPtr mode,out uint database);
 [DllImport("msi.dll",CharSet=CharSet.Unicode)] static extern uint MsiDatabaseOpenView(uint database,string query,out uint view);
 [DllImport("msi.dll")] static extern uint MsiViewExecute(uint view,uint record);
 [DllImport("msi.dll")] static extern uint MsiViewFetch(uint view,out uint record);
 [DllImport("msi.dll")] public static extern uint MsiCloseHandle(uint handle);
 [DllImport("msi.dll")] static extern uint MsiCreateRecord(uint fields);
 [DllImport("msi.dll",CharSet=CharSet.Unicode)] static extern uint MsiRecordSetString(uint record,uint field,string value);
 [DllImport("msi.dll")] static extern uint MsiRecordSetInteger(uint record,uint field,int value);
 [DllImport("msi.dll",CharSet=CharSet.Unicode)] static extern uint MsiRecordSetStream(uint record,uint field,string file);
 [DllImport("msi.dll")] public static extern uint MsiDatabaseCommit(uint database);
 [DllImport("msi.dll",CharSet=CharSet.Unicode)] static extern uint MsiDatabaseImport(uint database,string folder,string file);
 [DllImport("msi.dll",CharSet=CharSet.Unicode)] static extern uint MsiGetSummaryInformation(uint database,string file,uint count,out uint summary);
 [DllImport("msi.dll",CharSet=CharSet.Unicode)] static extern uint MsiSummaryInfoSetProperty(uint summary,uint property,uint type,int number,IntPtr time,string value);
 [DllImport("msi.dll")] static extern uint MsiSummaryInfoPersist(uint summary);
 [DllImport("msi.dll")] static extern uint MsiGetLastErrorRecord();
 [DllImport("msi.dll",CharSet=CharSet.Unicode)] static extern uint MsiFormatRecord(uint install,uint record,StringBuilder buffer,ref uint size);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint GetShortPathName(string path,StringBuilder buffer,uint size);
 public static void Check(uint result,string operation) {
   if(result==0)return;uint record=MsiGetLastErrorRecord();string detail="";
   if(record!=0){uint size=0;MsiFormatRecord(0,record,null,ref size);var text=new StringBuilder((int)size+1);size++;MsiFormatRecord(0,record,text,ref size);detail=text.ToString();MsiCloseHandle(record);}
   throw new Exception(operation+" failed ("+result+"): "+detail);
 }
 public static uint Create(string path){uint database;Check(MsiOpenDatabase(path,new IntPtr(3),out database),"Create MSI");return database;}
 public static string ShortPath(string path){var b=new StringBuilder(32768);if(GetShortPathName(path,b,32768)==0)throw new Exception("Cannot resolve cabinet source path");string p=b.ToString();foreach(char c in p)if(c>127)throw new Exception("Cabinet source path requires an ASCII short path: "+path);return p;}
 public static void Sql(uint database,string sql){uint view;Check(MsiDatabaseOpenView(database,sql,out view),sql);try{Check(MsiViewExecute(view,0),sql);}finally{MsiCloseHandle(view);}}
 public sealed class Stream {public string Path;public Stream(string path){Path=path;}}
 public static void Insert(uint database,string table,string[] columns,object[] values) {
   string[] names=new string[columns.Length],parameters=new string[columns.Length];for(int i=0;i<names.Length;i++){names[i]="`"+columns[i]+"`";parameters[i]="?";}
   string sql="INSERT INTO `"+table+"` ("+string.Join(",",names)+") VALUES ("+string.Join(",",parameters)+")";
   uint view;Check(MsiDatabaseOpenView(database,sql,out view),sql);uint record=MsiCreateRecord((uint)values.Length);
   try{for(int i=0;i<values.Length;i++){object v=values[i];if(v==null)continue;uint field=(uint)i+1;
      if(v is Stream)Check(MsiRecordSetStream(record,field,((Stream)v).Path),"Set stream");
      else if(v is int)Check(MsiRecordSetInteger(record,field,(int)v),"Set integer");
      else Check(MsiRecordSetString(record,field,v.ToString()),"Set string");}
      Check(MsiViewExecute(view,record),sql);
   }finally{MsiCloseHandle(record);MsiCloseHandle(view);}
 }
 public static void Codepage(uint database,string directory){Check(MsiDatabaseImport(database,directory,"_ForceCodepage.idt"),"Set database code page");}
 public static void Summary(uint database,string packageCode) {
   uint summary;Check(MsiGetSummaryInformation(database,null,20,out summary),"Open summary");
   try{Check(MsiSummaryInfoSetProperty(summary,1,2,936,IntPtr.Zero,null),"Code page");
     var values=new Dictionary<uint,string>{{2,"Penumbra 半影 Beta 0.3"},{3,"Desktop vignette utility"},{4,"冷茶Reicha"},{5,"Penumbra;Desktop;Vignette"},{7,"x64;2052"},{9,packageCode}};
     foreach(var pair in values)Check(MsiSummaryInfoSetProperty(summary,pair.Key,30,0,IntPtr.Zero,pair.Value),"Summary string");
     Check(MsiSummaryInfoSetProperty(summary,14,3,500,IntPtr.Zero,null),"Installer version");
     Check(MsiSummaryInfoSetProperty(summary,15,3,10,IntPtr.Zero,null),"Compressed long filenames, no elevation");
     Check(MsiSummaryInfoSetProperty(summary,19,3,2,IntPtr.Zero,null),"Read-only recommended");
     Check(MsiSummaryInfoPersist(summary),"Persist summary");
   }finally{MsiCloseHandle(summary);}
 }
}
'@

$helper=Join-Path $buildDirectory 'InstallerActions.dll'
& (Join-Path $Toolchain 'bin\x86_64-w64-mingw32-clang++.exe') (Join-Path $PSScriptRoot 'InstallerActions.cpp') -std=c++17 -Os -static -shared -s -Wall -Wextra -lmsi -ladvapi32 -o $helper
if($LASTEXITCODE){throw 'Installer helper compilation failed.'}
$files=@(Get-ChildItem -LiteralPath $publish -File -Recurse|Sort-Object FullName)
if(!$files.Count){throw 'Empty payload.'}
foreach($file in $files){
    if($file.Name -match '^(settings\.ini|animation-trace\.csv|animation-diagnostics\.txt|status\.txt|ui-error\.txt)$' -or $file.Extension -in '.pfx','.key','.pdb'){
        throw "Private/debug file cannot be published: $($file.FullName)"
    }
}
$rows=@();$cabinetLines=[Collections.Generic.List[string]]::new()
$cabinetLines.AddRange([string[]]@('.OPTION EXPLICIT','.Set CabinetNameTemplate=payload.cab','.Set DiskDirectoryTemplate=.','.Set RptFileName=cabinet.rpt','.Set InfFileName=cabinet.inf','.Set Cabinet=ON','.Set Compress=ON','.Set CompressionType=LZX','.Set CompressionMemory=21','.Set MaxDiskSize=0'))
function StableHash([string]$value){
    $data=[Text.Encoding]::UTF8.GetBytes('PenumbraInstaller:'+ $value.ToLowerInvariant())
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($data))
}
function StableGuid([string]$value){
    $hash=StableHash $value
    return '{'+$hash.Substring(0,8)+'-'+$hash.Substring(8,4)+'-'+$hash.Substring(12,4)+'-'+$hash.Substring(16,4)+'-'+$hash.Substring(20,12)+'}'
}
$sequence=0
foreach($file in $files){
    $sequence++;$relative=[IO.Path]::GetRelativePath($publish,$file.FullName)
    $hash=StableHash $relative;$id='F_'+$hash.Substring(0,24)
    $rows+=[pscustomobject]@{File=$file;Relative=$relative;Id=$id;Component='C_'+$hash.Substring(0,24);Sequence=$sequence}
    $cabinetLines.Add('"'+[PenumbraMsi]::ShortPath($file.FullName)+'" '+$id)
}
$ddf=Join-Path $buildDirectory 'payload.ddf'
[IO.File]::WriteAllLines($ddf,$cabinetLines,[Text.Encoding]::ASCII)
Push-Location $buildDirectory
try{$cabinetOutput=& "$env:WINDIR\System32\makecab.exe" /F payload.ddf /V1;if($LASTEXITCODE){throw ($cabinetOutput -join "`n")}}
finally{Pop-Location}
[IO.File]::WriteAllText((Join-Path $buildDirectory '_ForceCodepage.idt'),"`r`n`r`n936`t_ForceCodepage`r`n",[Text.Encoding]::ASCII)
$database=[PenumbraMsi]::Create($msiPath)
function Row([string]$table,[string[]]$columns,[object[]]$values){[PenumbraMsi]::Insert($database,$table,$columns,$values)}
try{
    [PenumbraMsi]::Codepage($database,$buildDirectory)
    $schemas=@{
        Property='`Property` CHAR(72) NOT NULL, `Value` CHAR(0) LOCALIZABLE PRIMARY KEY `Property`'
        Directory='`Directory` CHAR(72) NOT NULL, `Directory_Parent` CHAR(72), `DefaultDir` CHAR(255) NOT NULL LOCALIZABLE PRIMARY KEY `Directory`'
        Component='`Component` CHAR(72) NOT NULL, `ComponentId` CHAR(38), `Directory_` CHAR(72) NOT NULL, `Attributes` SHORT NOT NULL, `Condition` CHAR(255), `KeyPath` CHAR(72) PRIMARY KEY `Component`'
        Feature='`Feature` CHAR(38) NOT NULL, `Feature_Parent` CHAR(38), `Title` CHAR(64) LOCALIZABLE, `Description` CHAR(255) LOCALIZABLE, `Display` SHORT, `Level` SHORT NOT NULL, `Directory_` CHAR(72), `Attributes` SHORT NOT NULL PRIMARY KEY `Feature`'
        FeatureComponents='`Feature_` CHAR(38) NOT NULL, `Component_` CHAR(72) NOT NULL PRIMARY KEY `Feature_`, `Component_`'
        File='`File` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255) NOT NULL LOCALIZABLE, `FileSize` LONG NOT NULL, `Version` CHAR(72), `Language` CHAR(20), `Attributes` SHORT, `Sequence` SHORT NOT NULL PRIMARY KEY `File`'
        Media='`DiskId` SHORT NOT NULL, `LastSequence` SHORT NOT NULL, `DiskPrompt` CHAR(64) LOCALIZABLE, `Cabinet` CHAR(255), `VolumeLabel` CHAR(32), `Source` CHAR(72) PRIMARY KEY `DiskId`'
        Registry='`Registry` CHAR(72) NOT NULL, `Root` SHORT NOT NULL, `Key` CHAR(255) NOT NULL LOCALIZABLE, `Name` CHAR(255) LOCALIZABLE, `Value` CHAR(0) LOCALIZABLE, `Component_` CHAR(72) NOT NULL PRIMARY KEY `Registry`'
        Shortcut='`Shortcut` CHAR(72) NOT NULL, `Directory_` CHAR(72) NOT NULL, `Name` CHAR(128) NOT NULL LOCALIZABLE, `Component_` CHAR(72) NOT NULL, `Target` CHAR(255) NOT NULL, `Arguments` CHAR(255), `Description` CHAR(255) LOCALIZABLE, `Hotkey` SHORT, `Icon_` CHAR(72), `IconIndex` SHORT, `ShowCmd` SHORT, `WkDir` CHAR(72) PRIMARY KEY `Shortcut`'
        RemoveFile='`FileKey` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255) LOCALIZABLE, `DirProperty` CHAR(72) NOT NULL, `InstallMode` SHORT NOT NULL PRIMARY KEY `FileKey`'
        Binary='`Name` CHAR(72) NOT NULL, `Data` OBJECT NOT NULL PRIMARY KEY `Name`'
        CustomAction='`Action` CHAR(72) NOT NULL, `Type` SHORT NOT NULL, `Source` CHAR(72), `Target` CHAR(255) PRIMARY KEY `Action`'
        Upgrade='`UpgradeCode` CHAR(38) NOT NULL, `VersionMin` CHAR(20), `VersionMax` CHAR(20), `Language` CHAR(255), `Attributes` LONG NOT NULL, `Remove` CHAR(255), `ActionProperty` CHAR(72) NOT NULL PRIMARY KEY `UpgradeCode`, `VersionMin`, `VersionMax`, `Language`, `Attributes`'
        LaunchCondition='`Condition` CHAR(255) NOT NULL, `Description` CHAR(255) NOT NULL LOCALIZABLE PRIMARY KEY `Condition`'
        InstallExecuteSequence='`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`'
        InstallUISequence='`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`'
    }
    foreach($entry in $schemas.GetEnumerator()){[PenumbraMsi]::Sql($database,('CREATE TABLE `'+$entry.Key+'` ('+$entry.Value+')'))}
    $upgrade='{0CB1971B-FE53-4E66-9699-C87884CB2D4E}'
    $properties=@{
        ProductCode='{1120FEAB-73E8-4879-934D-BC1C5AB7930C}';UpgradeCode=$upgrade;ProductName='Penumbra 半影';ProductVersion='0.3.0';ProductLanguage='2052';Manufacturer='冷茶Reicha'
        MSIINSTALLPERUSER='1';LIMITUI='1';ARPNOMODIFY='1';ARPNOREPAIR='1';REBOOT='ReallySuppress';INSTALLLEVEL='1';ARPCOMMENTS='Penumbra 半影 Beta 0.3 · 桌面暗角';ARPURLINFOABOUT='https://github.com/Coldtea233/Penumbra';SecureCustomProperties='OLDPRODUCTS;NEWERPRODUCTFOUND;INSTALLFOLDER'
    }
    foreach($entry in $properties.GetEnumerator()){Row 'Property' @('Property','Value') @($entry.Key,$entry.Value)}
    Row 'Directory' @('Directory','Directory_Parent','DefaultDir') @('TARGETDIR',$null,'SourceDir')
    Row 'Directory' @('Directory','Directory_Parent','DefaultDir') @('LocalAppDataFolder','TARGETDIR','.')
    Row 'Directory' @('Directory','Directory_Parent','DefaultDir') @('UserPrograms','LocalAppDataFolder','Programs')
    Row 'Directory' @('Directory','Directory_Parent','DefaultDir') @('INSTALLFOLDER','UserPrograms','Penumbra')
    Row 'Directory' @('Directory','Directory_Parent','DefaultDir') @('ProgramMenuFolder','TARGETDIR','.')
    Row 'Directory' @('Directory','Directory_Parent','DefaultDir') @('MenuFolder','ProgramMenuFolder','Penumbra')
    $directories=@{''='INSTALLFOLDER'}
    $allDirectories=@(Get-ChildItem -LiteralPath $publish -Directory -Recurse|ForEach-Object{[IO.Path]::GetRelativePath($publish,$_.FullName)}|Sort-Object { $_.Length }, { $_ })
    foreach($relative in $allDirectories){
        $parent=Split-Path $relative -Parent;$name=Split-Path $relative -Leaf
        $id='D_'+(StableHash $relative).Substring(0,24)
        if(!$directories.ContainsKey($parent)){throw "Missing parent directory: $parent"}
        $alias='D'+('{0:X6}' -f $directories.Count)+'|'+$name
        Row 'Directory' @('Directory','Directory_Parent','DefaultDir') @($id,$directories[$parent],$alias)
        $directories[$relative]=$id
    }
    Row 'Feature' @('Feature','Title','Description','Display','Level','Directory_','Attributes') @('Main','Penumbra 半影','桌面暗角工具 Beta 0.3',1,1,'INSTALLFOLDER',0)
    foreach($item in $rows){
        $parent=Split-Path $item.Relative -Parent;$registry='R_'+$item.Id.Substring(2)
        Row 'Component' @('Component','ComponentId','Directory_','Attributes','KeyPath') @($item.Component,(StableGuid $item.Relative),$directories[$parent],260,$registry)
        Row 'FeatureComponents' @('Feature_','Component_') @('Main',$item.Component)
        Row 'Registry' @('Registry','Root','Key','Name','Value','Component_') @($registry,1,'Software\Penumbra\Installer\Components',$item.Component,('[#'+$item.Id+']'),$item.Component)
        $extension=$item.File.Extension.TrimStart('.').ToUpperInvariant();if($extension.Length -gt 3){$extension=$extension.Substring(0,3)}
        $shortName='F'+('{0:X6}' -f $item.Sequence);if($extension){$shortName+='.'+$extension}
        $info=$item.File.VersionInfo
        # The display string is not necessarily a legal MSI version (msquic's
        # revision exceeds 65535). Use the fixed, four-WORD PE version instead.
        $version=if($info.FileVersion){'{0}.{1}.{2}.{3}' -f $info.FileMajorPart,$info.FileMinorPart,$info.FileBuildPart,$info.FilePrivatePart}else{$null}
        Row 'File' @('File','Component_','FileName','FileSize','Version','Language','Attributes','Sequence') @($item.Id,$item.Component,($shortName+'|'+$item.File.Name),[int]$item.File.Length,$version,$null,16896,[int]$item.Sequence)
    }
    $launcher=$rows|Where-Object Relative -eq 'Penumbra.exe'|Select-Object -First 1
    if(!$launcher){throw 'Root launcher missing.'}
    Row 'Shortcut' @('Shortcut','Directory_','Name','Component_','Target','Description','ShowCmd','WkDir') @('StartMenu','MenuFolder','PENUMBRA|Penumbra 半影',$launcher.Component,('[#'+$launcher.Id+']'),'Penumbra 半影 Beta 0.3',1,'INSTALLFOLDER')
    Row 'RemoveFile' @('FileKey','Component_','DirProperty','InstallMode') @('EmptyMenuFolder',$launcher.Component,'MenuFolder',2)
    Row 'Media' @('DiskId','LastSequence','Cabinet') @(1,[int]$rows.Count,'#payload.cab')
    Row '_Streams' @('Name','Data') @('payload.cab',([PenumbraMsi+Stream]::new((Join-Path $buildDirectory 'payload.cab'))))
    Row 'Binary' @('Name','Data') @('InstallerActions',([PenumbraMsi+Stream]::new($helper)))
    foreach($action in @(@('CheckEnvironment',1,'CheckEnvironment'),@('PrepareStartupCleanup',1,'PrepareStartupCleanup'),@('RollbackOwnedStartup',1281,'RollbackOwnedStartup'),@('RemoveOwnedStartup',1025,'RemoveOwnedStartup'))){
        Row 'CustomAction' @('Action','Type','Source','Target') @($action[0],[int]$action[1],'InstallerActions',$action[2])
    }
    Row 'Upgrade' @('UpgradeCode','VersionMin','VersionMax','Attributes','ActionProperty') @($upgrade,'0.0.0','0.3.0',257,'OLDPRODUCTS')
    Row 'Upgrade' @('UpgradeCode','VersionMin','Attributes','ActionProperty') @($upgrade,'0.3.0',258,'NEWERPRODUCTFOUND')
    Row 'LaunchCondition' @('Condition','Description') @('VersionNT64','Penumbra 需要 64 位 Windows。')
    Row 'LaunchCondition' @('Condition','Description') @('Installed OR NOT NEWERPRODUCTFOUND','已安装相同或更新版本的 Penumbra，请先从系统设置卸载。')
    $actions=@(
        @('FindRelatedProducts',$null,25),@('CheckEnvironment',$null,27),@('LaunchConditions',$null,100),@('ValidateProductID',$null,700),
        @('CostInitialize',$null,800),@('FileCost',$null,900),@('CostFinalize',$null,1000),@('MigrateFeatureStates',$null,1200),@('InstallValidate',$null,1400),
        @('InstallInitialize',$null,1500),@('RemoveExistingProducts','OLDPRODUCTS',1501),@('ProcessComponents',$null,1600),@('UnpublishFeatures',$null,1800),
        @('RemoveRegistryValues',$null,2600),@('PrepareStartupCleanup','REMOVE~="ALL" AND NOT UPGRADINGPRODUCTCODE',2991),
        @('RollbackOwnedStartup','RollbackOwnedStartup',2993),@('RemoveOwnedStartup','RemoveOwnedStartup',2995),
        @('RemoveShortcuts',$null,3200),@('RemoveFiles',$null,3500),@('CreateFolders',$null,3700),@('InstallFiles',$null,4000),@('CreateShortcuts',$null,4500),
        @('WriteRegistryValues',$null,5000),@('RegisterUser',$null,6000),@('RegisterProduct',$null,6100),@('PublishFeatures',$null,6300),@('PublishProduct',$null,6400),@('InstallFinalize',$null,6600)
    )
    foreach($action in $actions){Row 'InstallExecuteSequence' @('Action','Condition','Sequence') @($action[0],$action[1],[int]$action[2])}
    foreach($action in @(@('FindRelatedProducts',$null,25),@('CheckEnvironment',$null,27),@('LaunchConditions',$null,100),@('CostInitialize',$null,800),@('FileCost',$null,900),@('CostFinalize',$null,1000),@('ExecuteAction',$null,1300))){
        Row 'InstallUISequence' @('Action','Condition','Sequence') @($action[0],$action[1],[int]$action[2])
    }
    [PenumbraMsi]::Summary($database,('{'+[Guid]::NewGuid().ToString().ToUpperInvariant()+'}'))
    [PenumbraMsi]::Check([PenumbraMsi]::MsiDatabaseCommit($database),'Commit MSI')
}finally{[PenumbraMsi]::MsiCloseHandle($database)|Out-Null}
Write-Output ([pscustomobject]@{Installer=$msiPath;Files=$rows.Count;PayloadBytes=($files|Measure-Object Length -Sum).Sum;InstallerBytes=(Get-Item -LiteralPath $msiPath).Length;ProductVersion='0.3.0';DisplayVersion='Beta 0.3';PerUser=$true}|ConvertTo-Json -Compress)
