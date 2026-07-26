# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`dev_tools` is a Godot 4 (GDScript) **addon**, not a standalone game. It provides an asset pipeline and shared developer tooling meant to be installed into other Godot game projects at `res://addons/dev_tools/`. The `project.godot` at the repo root is a minimal demo/test harness for exercising the addon in isolation — it is not a shippable game.

## Commands

On this machine the Godot binary is installed as `godot-limbo` (a Godot 4.6 build bundled with the LimboAI plugin), not `godot4` — use `godot-limbo` in place of `godot4` below.

- **Open in editor**: `godot-limbo --editor --path .` (or open `project.godot` from the Godot editor's project manager). Enable the plugin under Project Settings > Plugins if it isn't already active.
- **Run a headless script**: `godot-limbo --headless --path . --script res://addons/dev_tools/cli/<script_name>.gd`
- **Check for script errors without opening the editor UI**: `godot-limbo --headless --path . --check-only --script res://addons/dev_tools/plugin.gd` (`--check-only` requires `--script`; it won't run standalone or with `--editor`)

No test framework is wired up yet — `tests/` exists as a placeholder. When tests are added (GUT is the Godot ecosystem standard), they'll run via something like:
`godot-limbo --headless --path . -s addons/gut/gut_cmdln.gd -gdir=res://tests -gexit`

## Architecture

- `addons/dev_tools/plugin.cfg` — addon manifest (name, version, entry script). This is what a consuming project points at when installing the addon.
- `addons/dev_tools/plugin.gd` — `EditorPlugin` entry point (`_enter_tree` / `_exit_tree`). Any editor-side registration (custom types, docks, import plugins) should be wired up here.
- `addons/dev_tools/asset_pipeline/` — asset import/convert/process logic. Intended home for anything that transforms source assets (art, audio, data) into engine-ready formats.
- `addons/dev_tools/cli/` — headless entry points invoked via `godot --headless --script`, for tooling that runs outside the editor (batch processing, CI, etc).

## Consuming this addon from another project

This repo is meant to be dropped into other Godot projects as `addons/dev_tools/` (via git submodule or copy — no distribution mechanism is set up yet, decide per consuming project). Only the `addons/dev_tools/` subdirectory is relevant to a consumer; the root `project.godot` and `tests/` exist solely for developing this addon in isolation.
