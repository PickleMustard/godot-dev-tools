# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`dev_tools` is a Godot 4 (GDScript) **addon**, not a standalone game. It provides an asset pipeline and shared developer tooling meant to be installed into other Godot game projects at `res://addons/dev_tools/`. The `project.godot` at the repo root is a minimal demo/test harness for exercising the addon in isolation — it is not a shippable game.

## Commands

On this machine the Godot binary is installed as `godot-limbo` (a Godot 4.6 build bundled with the LimboAI plugin), not `godot4` — use `godot-limbo` in place of `godot4` below. `godot-limbo` does **not** have `GitBackend` or any `Lfs*` class compiled in (see "Git/LFS backend module" below) — scripts touching those classes must run against a custom-built editor binary instead.

- **Open in editor**: `godot-limbo --editor --path .` (or open `project.godot` from the Godot editor's project manager). Enable the plugin under Project Settings > Plugins if it isn't already active.
- **Run a headless script**: `godot-limbo --headless --path . --script res://addons/dev_tools/cli/<script_name>.gd`
- **Check for script errors without opening the editor UI**: `godot-limbo --headless --path . --check-only --script res://addons/dev_tools/plugin.gd` (`--check-only` requires `--script`; it won't run standalone or with `--editor`)

No test framework is wired up yet — `tests/` exists as a placeholder. When tests are added (GUT is the Godot ecosystem standard), they'll run via something like:
`godot-limbo --headless --path . -s addons/gut/gut_cmdln.gd -gdir=res://tests -gexit`

## Architecture

- `addons/dev_tools/plugin.cfg` — addon manifest (name, version, entry script). This is what a consuming project points at when installing the addon.
- `addons/dev_tools/plugin.gd` — `EditorPlugin` entry point (`_enter_tree` / `_exit_tree`). Any editor-side registration (custom types, docks, import plugins) should be wired up here.
- `addons/dev_tools/asset_pipeline/` — currently an empty placeholder directory (`.gitkeep` only, no implementation yet). Planned home for asset import/convert/process logic once built — nothing to consume here today.
- `addons/dev_tools/cli/` — currently an empty placeholder directory (`.gitkeep` only, no implementation yet). Planned home for headless entry points invoked via `godot --headless --script` (batch processing, CI, etc) once built.

## Git/LFS backend module

`GitBackend` and every `Lfs*` class (`LfsPointer`, `LfsManifest`, `LfsObjectStore`, `LfsScanner`, `LfsQuarantine`, `LfsStatusScanner`, `LfsCredentialProvider`/`LfsHttpsCredentialProvider`/`LfsSshCredentialProvider`, `LfsRemoteClient`, `LfsPullGuard`, `LfsPushService`, `LfsRebuildService`) are **not GDScript** — they're C++ engine classes that ship as a core Godot module, not a GDExtension. There is no `.gdextension` file and no `.so` in this repo anymore; consuming (or developing) any feature that touches these classes requires a custom-built Godot editor binary with this repo's C++ compiled in.

To build one:

1. Clone the Godot engine source (matching this repo's target version — see `compatibility_minimum`-equivalent in `register_types.cpp`/engine module conventions, currently tracking 4.5+).
2. Check out this repo as a submodule (or symlink, for local iteration) at `<godot-engine-checkout>/modules/dev_tools_git` — the repo root itself *is* the module content (`config.py`, `SCsub`, `register_types.{h,cpp}`, `git/`, `lfs/`), not nested under an extra subdirectory.
3. Ensure `libgit2` dev headers are installed (`libgit2-devel` on Fedora, `libgit2-dev` on Debian/Ubuntu) — `config.py`'s `can_build` checks for this via `pkg-config` and fails the build with a clear error if missing.
4. From the engine checkout root: `scons platform=linuxbsd target=editor dev_build=yes` (swap `platform=` for your OS). The resulting binary has `GitBackend`/`Lfs*` registered as built-in engine classes, usable from GDScript exactly like any other core type (`GitBackend.new()`, no `class_name`/script needed).

This repo's own root `project.godot` (the demo/test harness) needs that custom-built binary to exercise the git/LFS panels — `godot-limbo` alone will report `GitBackend`/`Lfs*` as unknown identifiers.

## UI conventions

Styling (StyleBoxFlat, fonts/FontVariation, colors, borders, margins) belongs in `.tscn` scene files — as node `theme_override_*` properties and `sub_resource` blocks — not in GDScript. GDScript builds behavior (signals, data population, view switching), not visual layout. Don't write `add_theme_stylebox_override` / `add_theme_font_override` / `add_theme_color_override` calls in `_ready()` or elsewhere to construct panel/label appearance; define the StyleBoxFlat/FontVariation as scene sub-resources instead.

## Consuming this addon from another project

This repo is meant to be dropped into other Godot projects as `addons/dev_tools/` — see `README.md`/`INSTALL.md` at repo root for consumer-facing install steps (git submodule is the primary documented path). Only the `addons/dev_tools/` subdirectory is relevant to a consumer for the GDScript/UI side; the root `project.godot` and `tests/` exist solely for developing this addon in isolation.

Because `GitBackend`/`Lfs*` are now a core engine module rather than a GDExtension, a consuming project also needs its Godot editor built from a source tree with this repo checked out at `modules/dev_tools_git` (see "Git/LFS backend module" above) — dropping in `addons/dev_tools/` alone is no longer sufficient for the git/LFS features to function; the rest of the addon (asset pipeline, non-git tooling) doesn't need the custom build.
