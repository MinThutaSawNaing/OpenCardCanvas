<#
  tools/make_icon.ps1 - generates resources/win/app.ico used by the build and the
  installer.

  The icon is generated from Logos/Cardinal ID Card Mascot Logo.png so it is
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

# The supplied logo is the single source for the application and Windows icon.
$sourcePath = Join-Path $root 'Logos\Cardinal ID Card Mascot Logo.png'
$source = [System.Drawing.Image]::FromFile($sourcePath)

function New-IconBitmap([int] $size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $g.Clear([System.Drawing.Color]::Transparent)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $scale = [Math]::Min($size / [double]$source.Width, $size / [double]$source.Height)
        $w = [float]($source.Width * $scale)
        $h = [float]($source.Height * $scale)
        $g.DrawImage($source, [float](($size - $w) / 2), [float](($size - $h) / 2), $w, $h)
    } finally {
        $g.Dispose()
    }
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

$source.Dispose()
$info = Get-Item $OutFile
Write-Host ("Wrote {0} ({1} bytes, {2} resolutions: {3})" -f `
    $info.FullName, $info.Length, $images.Count, ($sizes -join ', '))
