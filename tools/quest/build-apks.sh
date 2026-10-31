#!/usr/bin/env bash
set -euo pipefail
repo_root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$repo_root"
sdk_dir=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
[[ -d "$sdk_dir/build-tools" ]] || { echo 'Set ANDROID_HOME to the Android SDK.' >&2; exit 1; }
build_tools=$(find "$sdk_dir/build-tools" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -1)
readelf=$(find "$sdk_dir/ndk/${DROIDDECK_NDK_VERSION:-27.3.13750724}/toolchains/llvm/prebuilt" -name llvm-readelf -type f | head -1)
[[ -x "$readelf" ]] || { echo 'NDK llvm-readelf not found.' >&2; exit 1; }
if [[ ${DROIDDECK_QUEST_REUSE_INPUTS:-0} != 1 ]]; then
    bash tools/build_local.sh
fi
python3 tools/release/runtime_inputs.py verify
mkdir -p "$repo_root/dist"
output=$(mktemp -d "$repo_root/dist/.quest.XXXXXX")
trap 'rm -rf -- "$output"' EXIT
stores_check=()
gradle_args=()
if [[ ${DROIDDECK_SKIP_RUST:-0} == 1 ]]; then
    stores_check+=(--without-stores)
    gradle_args+=(-PskipRust=true)
fi
for target in quest3 quest3s quest2; do
    ./gradlew :app:assembleRelease --console=plain "-PquestTarget=$target" "${gradle_args[@]}"
    apk=app/build/outputs/apk/release/app-release.apk
    python3 tools/release/check_session_assets.py "$apk"
    python3 tools/release/runtime_inputs.py apk "$apk"
    python3 - "$apk" <<'PYPACKS'
import hashlib
from pathlib import Path
import sys
from zipfile import ZipFile
with ZipFile(sys.argv[1]) as apk:
    for name in ('proot', 'directaudio', 'tracking', 'xbox', 'comet', 'butler'):
        expected = Path(f'app/build/packs/{name}.tar.zst').read_bytes()
        embedded = apk.read(f'assets/addon-packs/{name}.tar.zst')
        if hashlib.sha256(embedded).digest() != hashlib.sha256(expected).digest():
            sys.exit(f'Embedded {name} pack differs from verified inputs')
print('Verified all six embedded add-on packs')
PYPACKS
    python3 tools/release/check_android_libraries.py "$apk" --readelf "$readelf" "${stores_check[@]}"
    "$build_tools/zipalign" -c -p 4 "$apk"
    "$build_tools/apksigner" verify --verbose --print-certs "$apk" > "$output/$target-signature.txt"
    for scheme in v1 v2 v3; do
        grep -q "Verified using $scheme scheme .*: true" "$output/$target-signature.txt"
    done
    "$build_tools/aapt" dump badging "$apk" > "$output/$target-badging.txt"
    grep -q "package: name='com.yomiplush.deckdroid.$target'" "$output/$target-badging.txt"
    case "$target" in quest3) label='DeckDroid Quest 3' ;; quest3s) label='DeckDroid Quest 3S' ;; quest2) label='DeckDroid Quest 2' ;; esac
    python3 - "$output/$target-badging.txt" "$label" <<'PYLABEL'
from pathlib import Path
import sys
lines = Path(sys.argv[1]).read_text().splitlines()
labels = [line.split(':', 1)[1] for line in lines if line.startswith('application-label')]
if not labels or any(label != repr(sys.argv[2]) for label in labels):
    sys.exit('APK launcher label does not identify its Quest target in every locale')
PYLABEL
    cp "$apk" "$output/DeckDroid-$target-experimental.apk"
done
(cd "$output" && sha256sum DeckDroid-quest3-experimental.apk DeckDroid-quest3s-experimental.apk DeckDroid-quest2-experimental.apk > SHA256SUMS)
{
    printf 'source_commit=%s\n' "$(git rev-parse HEAD)"
    printf 'tree_state=%s\n' "$(git status --porcelain | wc -l) changed paths"
    printf 'hardware_validation=pending (all targets)\nsigning=public repository test key\nembedded_addon_packs=proot,directaudio,tracking,xbox,comet,butler\n'
    printf 'store_engines=%s\n' "$([[ ${DROIDDECK_SKIP_RUST:-0} == 1 ]] && echo omitted || echo included)"
} > "$output/BUILD.txt"
destination="$repo_root/dist/quest"
previous="${output}.previous"
if [[ -e "$destination" ]]; then mv -- "$destination" "$previous"; fi
if ! mv -- "$output" "$destination"; then
    if [[ -e "$previous" ]]; then mv -- "$previous" "$destination"; fi
    exit 1
fi
trap - EXIT
if [[ -e "$previous" ]]; then rm -rf -- "$previous"; fi
echo "Verified experimental APKs: $destination"
