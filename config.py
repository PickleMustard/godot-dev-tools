import subprocess


def can_build(env, platform):
    try:
        subprocess.run(["pkg-config", "--exists", "libgit2"], check=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        import SCons.Errors

        raise SCons.Errors.UserError(
            "libgit2 not found via pkg-config. Install the libgit2 development "
            "package first (e.g. `sudo dnf install libgit2-devel` on Fedora, "
            "`sudo apt install libgit2-dev` on Debian/Ubuntu)."
        )
    return True


def configure(env):
    pass
