#!/usr/bin/env bash
# Build a release of defjam-sw for GitHub: dist/ gets the files of a release
# (defjamx.nro, defjamx_env.txt, LICENSES.txt) and the SHA-256 values that the
# installer's release/manifest.json needs.
#
#   tools/release.sh            # devkitPro installed: DEVKITPRO, default /opt/devkitpro
#   DOCKER=1 tools/release.sh   # in the devkitpro/devkita64 Docker image instead
#
# Needs game/default.xbe and the lifted code in gen/ (tools/regen.sh). The NRO
# contains no game data; never upload anything from game/ or gen/.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"
XBE="${DEFJAM_XBE:-$REPO/game/default.xbe}"
[ -f "$XBE" ] || { echo "no $XBE: extract the disc first (README)"; exit 1; }
[ -d gen ] || { echo "no gen/: run tools/regen.sh first"; exit 1; }

if [ "${DOCKER:-0}" = "1" ]; then
    docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -e STAGE_GAME=0 \
        -v "$REPO:$REPO" -w "$REPO" devkitpro/devkita64 bash switch/build.sh
else
    STAGE_GAME=0 bash switch/build.sh
fi

rm -rf dist && mkdir -p dist
cp build-switch/defjam_recomp.nro dist/defjamx.nro
cp switch/defjamx_env.txt switch/LICENSES.txt dist/
VERSION=$(sed -n 's/.*set(DEFJAM_SWITCH_VERSION "\(.*\)").*/\1/p' CMakeLists.txt | head -1)
NRO_SHA=$(sha256sum dist/defjamx.nro | cut -d' ' -f1)
XBE_SHA=$(sha256sum "$XBE" | cut -d' ' -f1)
XBE_SIZE=$(stat -c %s "$XBE")
(cd dist && sha256sum defjamx.nro defjamx_env.txt LICENSES.txt > SHA256SUMS)
cat <<MSG

dist/ is release v$VERSION:
$(ls -la dist | tail -n +4)

Upload defjamx.nro, defjamx_env.txt and LICENSES.txt to a GitHub release:
  gh release create v$VERSION dist/defjamx.nro dist/defjamx_env.txt dist/LICENSES.txt dist/SHA256SUMS \\
      --title "v$VERSION" --notes-file docs/release-notes.md

Then in defjam-sw-installer, release/manifest.json:
  "xbe_sha256": "$XBE_SHA",
  "xbe_size": $XBE_SIZE,
  "nro_sha256": "$NRO_SHA"
and copy dist/defjamx_env.txt and dist/LICENSES.txt to its release/ folder.
MSG
