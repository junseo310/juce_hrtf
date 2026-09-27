#!/usr/bin/env bash
# Downloads a couple of freely licensed HRTF datasets into ./hrtfs.
# These are large and are deliberately not committed to the repo.
set -euo pipefail

DEST="$(cd "$(dirname "$0")/.." && pwd)/hrtfs"
mkdir -p "$DEST"

fetch () {
  local name="$1" url="$2"
  if [ -f "$DEST/$name" ]; then
    echo "have   $name"
  else
    echo "fetch  $name"
    curl -fL --progress-bar -o "$DEST/$name" "$url"
  fi
}

# SADIE II, Neumann KU100 dummy head. The usual sane default.
fetch "SADIE_KU100.sofa" \
  "https://www.york.ac.uk/sadie-project/Resources/SADIIDatabase/KU100/KU100_HRIR_SOFA/HRIR_FULL2DEG.sofa"

echo
echo "Datasets in $DEST"
echo "Try: ./build/render_offline orbit.wav $DEST/SADIE_KU100.sofa"
