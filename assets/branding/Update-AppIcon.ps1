param([Parameter(Mandatory)][string]$SourcePath)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$source=[Drawing.Image]::FromFile((Resolve-Path -LiteralPath $SourcePath).Path)
try {
    $frames=@()
    foreach($size in @(16,20,24,32,40,48,64,128,256)) {
        $bitmap=[Drawing.Bitmap]::new($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics=[Drawing.Graphics]::FromImage($bitmap)
        $stream=[IO.MemoryStream]::new()
        try {
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.CompositingQuality=[Drawing.Drawing2D.CompositingQuality]::HighQuality
            $graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode=[Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $scale=[Math]::Min($size/[double]$source.Width,$size/[double]$source.Height)
            $width=[Math]::Max(1,[int][Math]::Round($source.Width*$scale));$height=[Math]::Max(1,[int][Math]::Round($source.Height*$scale))
            $graphics.DrawImage($source,[Drawing.Rectangle]::new([int](($size-$width)/2),[int](($size-$height)/2),$width,$height))
            $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
            $frames+=@{Size=$size;Data=$stream.ToArray()}
            if($size -eq 256){$bitmap.Save((Join-Path $PSScriptRoot 'AppIcon.png'),[Drawing.Imaging.ImageFormat]::Png)}
        } finally {$stream.Dispose();$graphics.Dispose();$bitmap.Dispose()}
    }
    $file=[IO.File]::Create((Join-Path $PSScriptRoot 'AppIcon.ico'))
    $writer=[IO.BinaryWriter]::new($file)
    try {
        $writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$frames.Count)
        $offset=6+16*$frames.Count
        foreach($frame in $frames){
            $dimension=if($frame.Size -eq 256){0}else{$frame.Size}
            $writer.Write([byte]$dimension);$writer.Write([byte]$dimension);$writer.Write([byte]0);$writer.Write([byte]0)
            $writer.Write([uint16]1);$writer.Write([uint16]32)
            $writer.Write([uint32]$frame.Data.Length);$writer.Write([uint32]$offset)
            $offset+=$frame.Data.Length
        }
        foreach($frame in $frames){$writer.Write([byte[]]$frame.Data)}
    } finally {$writer.Dispose()}
} finally {$source.Dispose()}
Write-Output 'Updated application PNG/ICO. TrayWhite.ico and TrayBlack.ico are untouched.'
