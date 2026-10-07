<#
  tools/make_icon.ps1 - generates resources/win/app.ico used by the build and the
  installer.

  The icon is drawn programmatically (no binary asset is checked in) so it is
  reproducible: run this script to regenerate it after changing the artwork.

      powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_icon.ps1

  Output: a multi-resolution .ico with 16, 24, 32, 48, 64, 128 and 256 px images
  stored as uncompressed 32-bit BGRA bitmaps, which every supported Windows
  version understands.
#>
[CmdletBinding()]
param([string] $OutFile = '')

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutFile)) {
    $OutFile = Join-Path $root 'resources\win\app.ico'
}
$outDir = Split-Path -Parent $OutFile
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }

$sizes = @(16, 24, 32, 48, 64, 128, 256)

function New-IconBitmap([int] $size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $s = [double]$size
    $margin = [double]($s * 0.06)
    $w = $s - 2 * $margin
    $h = [double]($w * 0.64)
    $x = $margin
    $y = ($s - $h) / 2.0
    $radius = [Math]::Max(1.0, $s * 0.07)
    $d = $radius * 2

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $path.CloseFigure()

    $body = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 21, 52, 96))
    $g.FillPath($body, $path)
    $outline = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 92, 140, 214), [float][Math]::Max(0.75, $s * 0.035))
    $g.DrawPath($outline, $path)

    # Header band, clipped to the card outline.
    $bandH = [Math]::Max(1.0, $h * 0.26)
    $band = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 62, 128, 214))
    $g.SetClip($path)
    $g.FillRectangle($band, [float]$x, [float]$y, [float]$w, [float]$bandH)
    $g.ResetClip()

    if ($size -ge 32) {
        $photo = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 224, 232, 244))
        $pw = [Math]::Max(1.0, $w * 0.26)
        $ph = [Math]::Max(1.0, $h * 0.30)
        $px = $x + $w * 0.10
        $py = $y + $h * 0.42
        $g.FillRectangle($photo, [float]$px, [float]$py, [float]$pw, [float]$ph)

        $line = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(230, 236, 243, 252))
        $lx = $px + $pw * 1.35
        $lw = ($x + $w) - $lx - $w * 0.10
        $lh = [Math]::Max(0.8, $h * 0.075)
        $g.FillRectangle($line, [float]$lx, [float]($py + $ph * 0.06), [float]$lw, [float]$lh)
        $g.FillRectangle($line, [float]$lx, [float]($py + $ph * 0.50), [float]($lw * 0.72), [float]$lh)
        $line.Dispose(); $photo.Dispose()
    }

    $band.Dispose(); $outline.Dispose(); $body.Dispose(); $path.Dispose()
    $g.Dispose()
    return $bmp
}


function Get-IconEntryBytes([System.Drawing.Bitmap] $bmp) {
    $w = $bmp.Width
    $h = $bmp.Height
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)

    # BITMAPINFOHEADER - height doubled because it covers the XOR image plus the AND mask.
    $bw.Write([uint32]40); $bw.Write([int32]$w); $bw.Write([int32]($h * 2))
    $bw.Write([uint16]1);  $bw.Write([uint16]32)
    $bw.Write([uint32]0);  $bw.Write([uint32]($w * $h * 4))
    $bw.Write([int32]0);   $bw.Write([int32]0)
    $bw.Write([uint32]0);  $bw.Write([uint32]0)

    # XOR image: BGRA, bottom-up.
    $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $stride = $data.Stride
        $buffer = New-Object byte[] ($stride * $h)
        [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buffer, 0, $buffer.Length)
        for ($y = $h - 1; $y -ge 0; $y--) {
            $bw.Write($buffer, $y * $stride, $w * 4)
        }
    } finally {
        $bmp.UnlockBits($data)
    }

    # AND mask: all zero (fully opaque - the alpha channel carries transparency),
    # one bit per pixel with rows padded to 4 bytes.
    $maskStride = [int](([Math]::Ceiling($w / 32.0)) * 4)
    $mask = New-Object byte[] ($maskStride * $h)
    for ($y = 0; $y -lt $h; $y++) { $bw.Write($mask, 0, $maskStride) }

    $bw.Flush()
    $bytes = $ms.ToArray()
    $bw.Dispose(); $ms.Dispose()
    return $bytes
}

$images = @()
foreach ($s in $sizes) {
    $bmp = New-IconBitmap $s
    $bytes = Get-IconEntryBytes $bmp
    $images += [pscustomobject]@{ Size = $s; Bytes = $bytes }
    $bmp.Dispose()
}

# ICO container: ICONDIR, then one ICONDIRENTRY per image, then the image data.
$fs = New-Object System.IO.FileStream($OutFile, [System.IO.FileMode]::Create)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([uint16]0)              # reserved
$bw.Write([uint16]1)              # type: icon
$bw.Write([uint16]$images.Count)  # image count

$offset = 6 + (16 * $images.Count)
foreach ($img in $images) {
    $dim = if ($img.Size -ge 256) { 0 } else { $img.Size }
    $bw.Write([byte]$dim)         # width  (0 means 256)
    $bw.Write([byte]$dim)         # height (0 means 256)
    $bw.Write([byte]0)            # palette entries
    $bw.Write([byte]0)            # reserved
    $bw.Write([uint16]1)          # colour planes
    $bw.Write([uint16]32)         # bits per pixel
    $bw.Write([uint32]$img.Bytes.Length)
    $bw.Write([uint32]$offset)
    $offset += $img.Bytes.Length
}
foreach ($img in $images) { $bw.Write($img.Bytes, 0, $img.Bytes.Length) }
$bw.Flush(); $bw.Dispose(); $fs.Dispose()

$info = Get-Item $OutFile
Write-Host ("Wrote {0} ({1} bytes, {2} resolutions: {3})" -f `
    $info.FullName, $info.Length, $images.Count, ($sizes -join ', '))
