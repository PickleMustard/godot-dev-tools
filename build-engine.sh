#!/usr/bin/env bash
set -euo pipefail

DEV_TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GODOT_CUSTOM_DIR="${GODOT_CUSTOM_DIR:-$(cd "$DEV_TOOLS_DIR/.." && pwd)/godot-custom}"
JOBS="$(nproc)"

usage() {
    echo "Usage: $0 [-j N] [--godot-dir PATH]"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -j)
            JOBS="$2"
            shift 2
            ;;
        --godot-dir)
            GODOT_CUSTOM_DIR="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            ;;
    esac
done

if [[ ! -d "$DEV_TOOLS_DIR/.git" ]]; then
    echo "error: $DEV_TOOLS_DIR is not a git repo" >&2
    exit 1
fi
if [[ ! -d "$GODOT_CUSTOM_DIR/.git" ]]; then
    echo "error: $GODOT_CUSTOM_DIR is not a git repo (set GODOT_CUSTOM_DIR or --godot-dir)" >&2
    exit 1
fi

echo "==> dev_tools: fetching origin"
if ! git -C "$DEV_TOOLS_DIR" fetch origin; then
    echo "warning: fetch failed (offline?) — continuing with local state"
fi

CURRENT_BRANCH="$(git -C "$DEV_TOOLS_DIR" rev-parse --abbrev-ref HEAD)"
echo "==> dev_tools: current branch is $CURRENT_BRANCH"

if UPSTREAM="$(git -C "$DEV_TOOLS_DIR" rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null)"; then
    read -r AHEAD BEHIND <<< "$(git -C "$DEV_TOOLS_DIR" rev-list --left-right --count HEAD..."$UPSTREAM")"
    if [[ "$BEHIND" -gt 0 ]]; then
        echo "warning: $CURRENT_BRANCH is behind $UPSTREAM by $BEHIND commit(s) — not pulling automatically"
    fi
    if [[ "$AHEAD" -gt 0 ]]; then
        echo "note: $CURRENT_BRANCH is ahead of $UPSTREAM by $AHEAD commit(s)"
    fi
else
    echo "warning: $CURRENT_BRANCH has no upstream tracking branch — skipping ahead/behind check"
fi

if [[ -n "$(git -C "$DEV_TOOLS_DIR" status --porcelain)" ]]; then
    echo "warning: dev_tools working tree is dirty — building with uncommitted changes:"
    git -C "$DEV_TOOLS_DIR" status --porcelain | sed 's/^/    /'
fi

DEV_TOOLS_HEAD="$(git -C "$DEV_TOOLS_DIR" rev-parse HEAD)"

echo "==> godot-custom: submodule status before sync"
git -C "$GODOT_CUSTOM_DIR" submodule status modules/dev_tools_git

echo "==> godot-custom: fetching submodule origin (github)"
git -C "$GODOT_CUSTOM_DIR/modules/dev_tools_git" fetch origin

echo "==> godot-custom: checking out dev_tools HEAD ($DEV_TOOLS_HEAD) in submodule"
git -C "$GODOT_CUSTOM_DIR/modules/dev_tools_git" checkout "$DEV_TOOLS_HEAD"

echo "==> godot-custom: submodule status after sync"
git -C "$GODOT_CUSTOM_DIR" submodule status modules/dev_tools_git

echo "==> godot-custom: building engine (-j$JOBS)"
(
    cd "$GODOT_CUSTOM_DIR"
    scons platform=linuxbsd target=editor dev_build=false module_mono_enabled=yes arch=x86_64 precision=single -j"$JOBS"
)

BIN_PATH="$GODOT_CUSTOM_DIR/bin/godot.linuxbsd.editor.x86_64.mono"
echo "==> build complete: $BIN_PATH"
