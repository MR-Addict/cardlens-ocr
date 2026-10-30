#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "$(uname -s)" == Linux && "${CARDLENS_LIMITED_BUILD:-}" != 1 && "${GITHUB_ACTIONS:-}" != true ]]; then
  exec python3 "$root/scripts/build_limits.py" bash "$root/scripts/build-harmonyos.sh" "$@"
fi
if [[ -z "${OHOS_NDK_HOME:-}" ]]; then
  toolroot="${CARDLENS_HARMONY_TOOLS:-$HOME/.local/share/harmony-ocr/command-line-tools}"
  export OHOS_NDK_HOME="$toolroot/sdk/default/openharmony/native"
  export DEVECO_SDK_HOME="$toolroot/sdk"
  export JAVA_HOME="${JAVA_HOME:-$HOME/.local/share/harmony-ocr/jdk}"
  export PATH="$toolroot/tool/node/bin:$toolroot/bin:$toolroot/ohpm/bin:$JAVA_HOME/bin:$OHOS_NDK_HOME/build-tools/cmake/bin:$PATH"
fi
python3 "$root/scripts/build.py" harmonyos --deps-only > "$root/out/harmonyos-deps.log" 2>&1
python3 "$root/scripts/stage-harmonyos.py"
cd "$root/out/workspace/sdk/harmonyos"
ohpm install
hvigorw --mode module -p product=default -p module=cardlens_ocr@default -p buildMode=release assembleHar --no-daemon
mkdir -p "$root/out/release"
cp cardlens_ocr/build/default/outputs/default/cardlens_ocr.har "$root/out/release/cardlens-ocr.har"
python3 "$root/scripts/check-har.py"
