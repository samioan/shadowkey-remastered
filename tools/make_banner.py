#!/usr/bin/env python3
"""Prepare the launcher's banner artwork for embedding.

The launcher shows one background image. This script downscales the source
artwork to the largest size the launcher can actually display and re-encodes
it as a JPEG, which `port/src/launcher/launcher.rc` embeds as RCDATA and
`sk::launcher::DecodeBanner` decodes at runtime through WIC.

Why a JPEG and not raw pixels
-----------------------------
M113 originally stored raw BGRX under a plain DEFLATE stream so that the
launcher could reuse the `puff` inflater already vendored for the zone-file
reader, and add no image decoder at all. That worked because the artwork of
the day was flat vector-style colour: 3.2 MB of pixels compressed to 155 KB.

M114's artwork is a rendered piece, all gradients and soft light, and
DEFLATE has nothing to work with -- measured at 1200x896 it gives 2.44 MB,
against 0.19 MB for the same image as a quality-88 JPEG. A thirteen-fold
difference in the size of every download and every auto-update is worth a
decoder, so the launcher now asks Windows for one (WIC, a system component
-- nothing to vendor, nothing to redistribute).

Usage:
    python tools/make_banner.py <image> [--width 1600] [--quality 90]
        [-o port/src/launcher/assets/banner.jpg]

The decode and the re-encode both go through PowerShell's System.Drawing,
the one image codec guaranteed to be on a Windows dev box without
installing Pillow.
"""

import argparse
import os
import subprocess
import sys
import tempfile

# Comfortably more than the launcher's own header is ever asked to draw
# (820 logical px, so 1640 at 200% DPI), and small enough that the whole
# thing stays well under a quarter of a megabyte.
DEFAULT_WIDTH = 1600
DEFAULT_QUALITY = 90

_RESIZE_PS = r"""
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$src = New-Object System.Drawing.Bitmap($args[0])
$targetWidth = [int]$args[1]
$quality = [long]$args[2]
$outPath = $args[3]

# Never upscale: if the source is already smaller, keep it as it is.
if ($targetWidth -ge $src.Width) { $targetWidth = $src.Width }
$targetHeight = [int][math]::Round($src.Height * $targetWidth / $src.Width)

$dst = New-Object System.Drawing.Bitmap($targetWidth, $targetHeight,
    [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$g = [System.Drawing.Graphics]::FromImage($dst)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$g.DrawImage($src, 0, 0, $targetWidth, $targetHeight)
$g.Dispose()

$encoder = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() |
           Where-Object { $_.MimeType -eq 'image/jpeg' }
$params = New-Object System.Drawing.Imaging.EncoderParameters(1)
$params.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter(
    [System.Drawing.Imaging.Encoder]::Quality, $quality)
$dst.Save($outPath, $encoder, $params)

Write-Output "$($src.Width) $($src.Height) $targetWidth $targetHeight"
$dst.Dispose()
$src.Dispose()
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("image", help="source artwork (JPEG/PNG/BMP)")
    parser.add_argument("--width", type=int, default=DEFAULT_WIDTH)
    parser.add_argument("--quality", type=int, default=DEFAULT_QUALITY)
    parser.add_argument("-o", "--output", default=os.path.join(
        "port", "src", "launcher", "assets", "banner.jpg"))
    args = parser.parse_args()

    output = os.path.abspath(args.output)
    os.makedirs(os.path.dirname(output), exist_ok=True)

    with tempfile.TemporaryDirectory() as scratch:
        script_path = os.path.join(scratch, "resize.ps1")
        with open(script_path, "w", encoding="utf-8") as handle:
            handle.write(_RESIZE_PS)
        result = subprocess.run(
            ["powershell", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
             "-File", script_path, os.path.abspath(args.image),
             str(args.width), str(args.quality), output],
            capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit("resize failed:\n" + (result.stderr or result.stdout))
        sourceWidth, sourceHeight, width, height = (int(n) for n in result.stdout.split())

    size = os.path.getsize(output)
    print("%s: %dx%d -> %dx%d at quality %d, %.0f KB"
          % (args.output, sourceWidth, sourceHeight, width, height, args.quality, size / 1024.0))
    # The launcher tolerates a bad banner by drawing a flat background, so a
    # silently truncated file would be a quiet cosmetic bug rather than a
    # crash. Check the JPEG markers here instead.
    with open(output, "rb") as handle:
        data = handle.read()
    if not data.startswith(b"\xff\xd8") or not data.rstrip(b"\x00").endswith(b"\xff\xd9"):
        sys.exit("output is not a complete JPEG (missing SOI/EOI marker)")


if __name__ == "__main__":
    main()
