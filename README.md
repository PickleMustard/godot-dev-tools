# dev_tools

Godot 4 addon: asset pipeline and shared developer tooling (Git status/staging/diff/history, `.gitattributes`/LFS pattern management, LFS visualizer) for reuse across game projects.

## What this is

`dev_tools` is a Godot 4 **addon**, not a standalone game. It's meant to be installed into other Godot projects at `res://addons/dev_tools/`. The `project.godot` at this repo's root is a minimal demo/test harness for exercising the addon in isolation — it is not shippable and isn't part of what you install.

## Features

Usable today (GDScript, works in any stock Godot 4.5+ editor once the addon is installed):
- Git status / staging / diff / side-by-side diff / history / branches / stash views
- `.gitattributes` and LFS-pattern management (dotfiles view)
- LFS visualizer (status, quarantine, lock badges)

Requires a custom-built editor (see [Two-part install](#two-part-install-story) below):
- All of the above Git/LFS panels actually talking to a real repo — they need the `GitBackend`/`Lfs*` C++ classes compiled into the editor binary.

Planned, not yet implemented:
- `addons/dev_tools/asset_pipeline/` — asset import/convert/process tooling (currently an empty placeholder)
- `addons/dev_tools/cli/` — headless CLI entry points (currently an empty placeholder)

## Two-part install story

- **Part A — GDScript addon.** Copy or git-submodule `addons/dev_tools/` into your project, enable the plugin. Works standalone on a stock Godot editor.
- **Part B — Git/LFS panels (optional).** `GitBackend`/`Lfs*` are core C++ engine classes, not a GDExtension — they only exist in a custom-compiled Godot editor binary with this repo's module built in.

Running Part A only on a stock editor is a fully supported configuration: the addon detects the missing module and shows a "module missing" notice in place of the Git/Dotfiles/LFS tabs instead of erroring.

See **[INSTALL.md](INSTALL.md)** for full step-by-step instructions for both parts.

## Quick start

```
git submodule add <this-repo-url> addons/dev_tools
git submodule update --init --recursive
```
Then in the Godot editor: Project Settings > Plugins > enable "Dev Tools". Full detail (including the Part B engine build) is in [INSTALL.md](INSTALL.md).

## Requirements

- Godot 4.5+ (stock editor is fine for Part A)
- `libgit2` dev headers (`libgit2-devel` / `libgit2-dev`) — only needed if building the custom engine for Part B

## Project status

Early (`plugin.cfg` version `0.1.0`). Asset pipeline and CLI directories are placeholders. No automated test suite yet.

## License

Not yet chosen — no `LICENSE` file exists in this repo.

## Contributing / development

See [CLAUDE.md](CLAUDE.md) for developer/contributor-facing conventions (build commands, architecture notes, UI styling rules, the C++ module build workflow).
