"""Materialize the upstream revision and patch series for a rendering backend."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def git(repo, *args, env=None):
    return subprocess.check_output(
        ["git", "-C", repo, *args], text=True, env=env
    ).strip()


def prepare(name):
    pins = json.loads((ROOT / "scripts/integration-backends.json").read_text())
    if name == "cheeky-vdxr":
        repo = ROOT / "external/cheeky-vdxr/source"
        revision = pins["cheeky"]
    elif name == "optiscaler-nr":
        repo = ROOT / "external/optiscaler-nr"
        revision = pins["optiScalerNr"]
    else:
        raise ValueError(f"Unknown rendering backend: {name}")

    patches = sorted((ROOT / "patches" / name).glob("*.patch"))
    if name == "cheeky-vdxr":
        patches.append(ROOT / "patches/cheeky/dcs-joint-nr.patch")
    with tempfile.TemporaryDirectory() as directory:
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(directory) / "index"))
        git(repo, "read-tree", revision, env=env)
        for patch in patches:
            git(repo, "apply", "--cached", str(patch), env=env)
        tree = git(repo, "write-tree", env=env)

    # A deterministic local commit lets a failed build resume from the same sources.
    identity = {"NAME": "DCS Control", "EMAIL": "build@localhost", "DATE": "2000-01-01T00:00:00Z"}
    env = dict(os.environ, **{f"GIT_{role}_{k}": v for role in ("AUTHOR", "COMMITTER")
                              for k, v in identity.items()})
    commit = git(repo, "commit-tree", tree, "-p", revision, "-m", "Apply DCS rendering patches", env=env)
    source = ROOT / "artifacts/native" / name / commit[:12]
    if not source.exists():
        source.parent.mkdir(parents=True, exist_ok=True)
        git(repo, "worktree", "add", "--detach", str(source), commit)
    if git(source, "rev-parse", "HEAD") != commit:
        raise ValueError(f"Unexpected revision in {source}")
    if name == "cheeky-vdxr":
        # The build regenerates the checked-in upstream shader header from patched HLSL.
        git(source, "diff", "--quiet", "HEAD", "--", ".", ":(exclude)shared/generated/fg_shader_bytecode.hpp")
        generator = source / "tools/compile-fg-shaders.py"
        if subprocess.run([sys.executable, generator, "--check"], capture_output=True).returncode:
            subprocess.run([sys.executable, generator], check=True, stdout=sys.stderr)
    else:
        git(source, "diff", "--quiet", "HEAD", "--ignore-submodules=all")
        git(source, "submodule", "update", "--init", "--recursive", "--jobs", "4")
    return source


if __name__ == "__main__":
    print(prepare(sys.argv[1]))
