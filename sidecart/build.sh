#!/bin/bash
# Build the MD/ROTT microfirmware: the m68k cartridge stub, then the RP2040
# firmware that embeds it, then the Booster app descriptor.
# From md-doom's build.sh.
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
cd "$SCRIPT_DIR"

if [ -z "${1:-}" ] || [ -z "${2:-}" ] || [ -z "${3:-}" ]; then
    echo "Usage: $0 <board_type> <build_type> <app_uuid_key>"
    echo "Example: $0 pico_w debug|release 123e4567-e89b-12d3-a456-426614174000"
    exit 1
fi

# Bump the patch version unless SKIP_VERSION_BUMP=1 (make build releases
# as-is), and sync it into rp/ and target/ either way.
"$SCRIPT_DIR/tools/bump_version.sh" --repo-root "$SCRIPT_DIR"
export VERSION=$(cat version.txt)
export BOARD_TYPE=$1
export BUILD_TYPE=$(echo "$2" | tr '[:upper:]' '[:lower:]')
export APP_UUID_KEY=$3
echo "MD/ROTT $VERSION: $BOARD_TYPE $BUILD_TYPE, app $APP_UUID_KEY"

rm -rf dist
mkdir dist

echo "Building the cartridge stub"
(cd target/atarist && ./build.sh "$SCRIPT_DIR/target/atarist" release)

echo "Building the RP2040 firmware"
rp/build.sh "$BOARD_TYPE" "$BUILD_TYPE"
if [ "$BUILD_TYPE" = "release" ]; then
    cp rp/dist/rp-$BOARD_TYPE.uf2 dist/rp.uf2
else
    cp rp/dist/rp-$BOARD_TYPE-$BUILD_TYPE.uf2 dist/rp.uf2
fi
if [ ! -f dist/rp.uf2 ]; then
    echo "ERROR: RP firmware not produced"
    exit 1
fi

if command -v md5sum > /dev/null; then
    md5sum dist/rp.uf2 > dist/rp.uf2.md5sum
else
    md5 -r dist/rp.uf2 > dist/rp.uf2.md5sum
fi
MD5=$(cut -d ' ' -f 1 dist/rp.uf2.md5sum)

mv dist/rp.uf2 "dist/$APP_UUID_KEY-$VERSION.uf2"
sed -e "s/<APP_UUID>/$APP_UUID_KEY/g" -e "s/<BINARY_MD5_HASH>/$MD5/g" \
    -e "s/<APP_VERSION>/$VERSION/g" desc/app.json > "dist/$APP_UUID_KEY.json"
echo "Built dist/$APP_UUID_KEY-$VERSION.uf2"
cat "dist/$APP_UUID_KEY.json"
