# Installing dev_tools

This addon has two independent parts. Pick based on what you need:

| Want | Do |
|---|---|
| Asset/dev tooling, Git/LFS UI panels visible but inert | Part A only |
| Git/LFS panels actually working against a real repo | Part A + Part B |

Part A alone is a fully supported configuration — not a degraded or error state. The addon detects at runtime whether the C++ module (Part B) is present and shows a "module missing" notice in place of the Git/Dotfiles/LFS tabs if it isn't, instead of failing.

## Part A — GDScript addon (baseline)

**Prerequisite:** a stock Godot 4.5+ editor. No special build needed.

### Option 1: git submodule (recommended)

From your consuming project's root:
```
git submodule add <this-repo-url> addons/dev_tools
git submodule update --init --recursive
```

Teammates cloning your project afterward need either:
```
git clone --recurse-submodules <your-consuming-repo-url>
```
or, after a plain clone:
```
git submodule update --init --recursive
```

To pull in updates to `dev_tools` later:
```
git submodule update --remote addons/dev_tools
git add addons/dev_tools
git commit -m "chore: bump dev_tools submodule"
```

### Option 2: plain copy

Copy the `addons/dev_tools/` directory into your project's `addons/` folder. Simpler, but you lose easy update tracking — you'll need to manually re-copy to pick up changes.

### Enable the plugin

In the Godot editor: **Project Settings > Plugins**, enable "Dev Tools". A "Dev Tools" tab should appear on the main editor screen. On a stock editor (no Part B), you'll see a "module missing" notice inside that tab instead of the Git/Dotfiles/LFS sub-tabs — this is expected.

The addon nests safely at any depth: every script/scene reference inside it uses the full `res://addons/dev_tools/...` path, and there are no hardcoded `../`-relative paths, so `addons/dev_tools/` works whether it's a submodule, a copy, or nested inside another addon's structure.

## Part B — Git/LFS panels (optional, requires custom engine build)

**Why:** `GitBackend` and every `Lfs*` class are core C++ engine classes shipped as a Godot module, not a GDExtension — there is no `.gdextension`/`.so` to drop in. They only exist in an editor binary built from source with this repo's module compiled in.

1. Clone the Godot engine source, matching this repo's target version (4.5+).
2. Check out this repo as a submodule (or symlink, for local iteration) at `<godot-engine-checkout>/modules/dev_tools_git`. The repo root itself *is* the module content (`config.py`, `SCsub`, `register_types.{h,cpp}`, `git/`, `lfs/`) — don't nest it under an extra subdirectory.
3. Install `libgit2` dev headers: `libgit2-devel` (Fedora) or `libgit2-dev` (Debian/Ubuntu). The build's `config.py` checks for this via `pkg-config` and fails with a clear error if missing.
4. From the engine checkout root:
   ```
   scons platform=linuxbsd target=editor dev_build=yes
   ```
   (swap `platform=` for your OS). The resulting binary registers `GitBackend`/`Lfs*` as built-in engine classes usable from GDScript like any other core type — no `class_name`/script needed on the consuming side.
5. Point your project at the built binary: in the Godot Project Manager, add a custom editor binary, or launch directly:
   ```
   <built-binary> --editor --path <your-project>
   ```

If you're a maintainer/contributor of this repo itself with a sibling `godot-custom` checkout already set up, `build-engine.sh` (repo root) wraps the sync-and-rebuild loop — see `--godot-dir`/`$GODOT_CUSTOM_DIR` and its other flags. That script is a convenience for this repo's own dev loop, not a substitute for the from-scratch steps above on a fresh consumer project.

**Verify:** with the custom binary running your project, the Git/Dotfiles/LFS tabs should render real functionality instead of the "module missing" notice.

## Troubleshooting

- **Submodule directory is empty after clone** — you forgot `--recurse-submodules` on the parent clone, or need `git submodule update --init --recursive`.
- **`pkg-config` can't find `libgit2` during `scons`** — `libgit2-devel`/`libgit2-dev` isn't installed, or its `.pc` file isn't on `PKG_CONFIG_PATH`.
- **Plugin doesn't appear in Project Settings > Plugins** — confirm `addons/dev_tools/plugin.cfg` exists at that exact path in your project (submodule/copy landed somewhere else, or is missing).
- **"Incompatible project" warning on open** — check your project's `project.godot` `config/features` against your editor's actual version.

## Updating

- **Part A:** `git submodule update --remote addons/dev_tools`, commit the bump.
- **Part B:** pull the latest `dev_tools` commit into your engine's `modules/dev_tools_git` checkout, rebuild with `scons` as in step 4 above.
