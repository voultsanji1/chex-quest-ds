#!/usr/bin/env bash
#
# Downloads the Chex Quest game data needed to build a self-contained
# Chex Quest DS ROM, and verifies it against known checksums.
#
# Output:
#   nitrofs/chex.wad   - Chex Quest IWAD (12,361,532 bytes)
#   nitrofs/chex.deh   - Chex Quest dehacked patch (20,367 bytes)
#
# The files are proprietary. Download them only if you are entitled to
# use Chex Quest (e.g. you own a copy of the original promotional CD).
#
# Safe to re-run: existing, correctly-checksummed files are kept.

set -euo pipefail

DEST="nitrofs"
mkdir -p "$DEST"

# Authoritative checksums (from Debian game-data-packager / DoomWiki).
WAD_MD5="25485721882b050afa96a56e5758dd52"
DEH_MD5="928cc1593cda42b78c8326807d5c80b6"

WAD_URLS=(
  "https://raw.githubusercontent.com/Akbar30Bill/DOOM_wads/master/chex.wad"
  "https://raw.githubusercontent.com/Akbar30Bill/DOOM_wads/main/chex.wad"
)

DEH_ZIP_URLS=(
  "https://www.gamers.org/pub/idgames/themes/chex/chexdeh.zip"
  "https://youfailit.net/pub/idgames/themes/chex/chexdeh.zip"
  "https://ftpmirror1.infania.net/pub/idgames/themes/chex/chexdeh.zip"
)

md5_of() { md5sum "$1" | cut -d' ' -f1; }

# Try each URL in turn until one downloads and matches the expected MD5.
fetch_verified() {
  local expected="$1"; shift
  local out="$1"; shift
  local urls=("$@")
  if [[ -f "$out" ]] && [[ "$(md5_of "$out")" == "$expected" ]]; then
    echo "  $out already present and verified."
    return 0
  fi
  local url
  for url in "${urls[@]}"; do
    echo "  trying: $url"
    if curl -fsSL --retry 3 --connect-timeout 30 -o "$out.tmp" "$url"; then
      if [[ "$(md5_of "$out.tmp")" == "$expected" ]]; then
        mv -f "$out.tmp" "$out"
        echo "  downloaded and verified: $out"
        return 0
      else
        echo "  checksum mismatch, skipping."
        rm -f "$out.tmp"
      fi
    else
      echo "  download failed."
    fi
  done
  echo "ERROR: could not fetch a verified copy of $out" >&2
  return 1
}

echo "==> Fetching chex.wad"
fetch_verified "$WAD_MD5" "$DEST/chex.wad" "${WAD_URLS[@]}"

echo "==> Fetching chex.deh (from chexdeh.zip)"
if [[ -f "$DEST/chex.deh" ]] && [[ "$(md5_of "$DEST/chex.deh")" == "$DEH_MD5" ]]; then
  echo "  nitrofs/chex.deh already present and verified."
else
  ZIP_TMP="$DEST/__chexdeh.zip"
  ok=0
  for url in "${DEH_ZIP_URLS[@]}"; do
    echo "  trying: $url"
    if curl -fsSL --retry 3 --connect-timeout 30 -o "$ZIP_TMP" "$url"; then
      if unzip -o -j "$ZIP_TMP" chex.deh -d "$DEST" >/dev/null 2>&1; then
        if [[ "$(md5_of "$DEST/chex.deh")" == "$DEH_MD5" ]]; then
          echo "  extracted and verified: nitrofs/chex.deh"
          ok=1
          break
        fi
      fi
      echo "  checksum mismatch, skipping."
    else
      echo "  download failed."
    fi
  done
  rm -f "$ZIP_TMP"
  [[ "$ok" == "1" ]] || { echo "ERROR: could not fetch a verified copy of chex.deh" >&2; exit 1; }
fi

echo "==> Done. nitrofs/ contains:"
ls -l "$DEST"
