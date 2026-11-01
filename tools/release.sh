#!/bin/sh
# Build and publish a release into release/ for the in-page "Check for updates" button.
# Usage: tools/release.sh "short notes"
set -e
cd "$(dirname "$0")/.."
make
V=$(grep FW_VERSION src/board.h | sed 's/.*"\(.*\)".*/\1/')
mkdir -p release
cp build/openpix.bin "release/openpix-$V.bin"
python3 - "$V" "$1" <<'PY'
import json,sys,zlib,os
v,notes=sys.argv[1],sys.argv[2] if len(sys.argv)>2 else ''
f=f'openpix-{v}.bin'; d=open('release/'+f,'rb').read()
json.dump({'version':v,'file':f,'size':len(d),'crc32':zlib.crc32(d)&0xffffffff,'notes':notes},open('release/latest.json','w'),indent=1)
print('release',v,len(d))
PY
