#!/usr/bin/env bash
# The iOS home screen icons, drawn from the logo: tools/ios/icons.sh [out]
# (default build/ios-icons, where src/CMakeLists.txt looks). Needs
# rsvg-convert (brew install librsvg). iOS rounds the corners itself and
# shows transparency as black, so the logo's own dark fills the square.
set -euo pipefail
cd "$(dirname "$0")/../.."
out="${1:-build/ios-icons}"
mkdir -p "$out"
draw() { rsvg-convert -w "$1" -h "$1" -b '#0a0a0a' resources/branding/scancore.svg -o "$out/$2"; }
draw 120 'AppIcon60x60@2x.png'
draw 180 'AppIcon60x60@3x.png'
draw 152 'AppIcon76x76@2x~ipad.png'
draw 167 'AppIcon83.5x83.5@2x~ipad.png'
ls -l "$out"
