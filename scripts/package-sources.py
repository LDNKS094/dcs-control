"""Package checked-out source snapshots and the manager; exclude build/cache data."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import zipfile

from product_version import product_version

root = Path(__file__).resolve().parent.parent
destination = root / f'artifacts/release/DcsControl-{product_version()}-sources.zip'
destination.parent.mkdir(parents=True, exist_ok=True)
revisions = {}
added = set()

with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    def add(path, name):
        # General-purpose SDK/build executables have their own vendor distribution.
        # Obtain them from the pinned official checkouts; keep all modified source here.
        if name.startswith('upstream/') and path.suffix.lower() in {'.exe','.dll','.lib','.pdb'}:
            return
        if path.is_file() and name not in added:
            archive.write(path, name)
            added.add(name)

    def repository(path, prefix):
        revision = subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()
        revisions[prefix] = revision
        files = subprocess.check_output(['git', '-C', str(path), 'ls-files', '-z', '--cached', '--others', '--exclude-standard']).decode().split('\0')
        for relative in filter(None, files):
            full = path / relative
            if full.is_dir() and (full / '.git').exists():
                repository(full, prefix + '/' + relative)
            else:
                add(full, prefix + '/' + relative)

    def pinned_repository(path, prefix, commit):
        # These checkouts can be on a development branch. Read the exact base
        # and its submodule commits; the manager supplies the matching patches.
        revisions[prefix] = commit
        git = ['git', '-C', str(path)]
        entries = subprocess.check_output(git + ['ls-tree', '-rz', commit]).decode().split('\0')
        with subprocess.Popen(git + ['archive', '--format=tar', commit], stdout=subprocess.PIPE) as process:
            with tarfile.open(fileobj=process.stdout, mode='r|') as snapshot:
                for entry in snapshot:
                    if entry.isfile() and Path(entry.name).suffix.lower() not in {'.exe', '.dll', '.lib', '.pdb'}:
                        name = prefix + '/' + entry.name
                        archive.writestr(name, snapshot.extractfile(entry).read())
                        added.add(name)
            process.stdout.read()
            if process.wait():
                raise RuntimeError(f'Could not archive {path} at {commit}')
        for item in filter(None, entries):
            header, relative = item.split('\t', 1)
            mode, _, object_id = header.split()
            if mode == '160000':
                pinned_repository(path / relative, prefix + '/' + relative, object_id)

    files = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z', '--cached', '--others', '--exclude-standard']).decode().split('\0')
    for relative in filter(None, files):
        add(root / relative, 'manager/' + relative)
    for name in ['ofxr', 'ofxr-djules75', 'cheeky', 'quadviews', 'sboys']:
        repository(root / 'external' / name, 'upstream/' + name)
    backends = json.loads((root / 'scripts/integration-backends.json').read_text(encoding='utf-8'))
    pinned_repository(root / 'external/cheeky-vdxr/source', 'upstream/cheeky-vdxr', backends['cheeky'])
    # The build initializes the pinned submodules in this worktree.
    opti = subprocess.check_output([sys.executable, root / 'scripts/prepare-render-source.py', 'optiscaler-nr'], text=True).strip()
    pinned_repository(Path(opti), 'upstream/optiscaler-nr', backends['optiScalerNr'])
    # OFXR's build dependency is a separate checkout, not an upstream gitlink.
    repository(root / 'external/ofxr/external/FidelityFX-SDK-v1.1.4', 'upstream/ofxr/external/FidelityFX-SDK-v1.1.4')
    archive.writestr('UPSTREAM_REVISIONS.json', json.dumps(revisions, indent=2))
    archive.writestr('SOURCE_README.txt', 'Build instructions: manager/README.md, manager/docs/THIRD_PARTY.md and manager/docs/INTEGRATION_BUILD.md.\nUPSTREAM_REVISIONS.json lists the source revisions. For VDXR and Desktop, upstream/cheeky-vdxr and upstream/optiscaler-nr contain the original sources. The corresponding patches and build scripts are under manager/.\nThe Cheeky focus adapter, Quad Views settings patch and FidelityFX build fixes are included. The shipped OFXR layer is upstream/ofxr-djules75 (tag 0.2.9.1) plus manager/patches/ofxr-djules75/*.patch.\nObtain MSVC, Windows SDK and FidelityFX SDK build tools from their pinned official distributions. Vendor packages retain their own terms.\n')

digest = hashlib.sha256(destination.read_bytes()).hexdigest()
destination.with_suffix('.zip.sha256').write_text(digest + '\n', encoding='ascii')
print(f'{destination}: {len(added)} source files, {destination.stat().st_size:,} bytes')
