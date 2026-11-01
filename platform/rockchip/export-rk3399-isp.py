#!/usr/bin/env python3
"""Run on the NanoPC-T4: export installed legacy RKISP libraries, not IQ files."""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import subprocess
import tarfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', help='New .tar.gz archive; existing files are not overwritten')
    args = parser.parse_args()
    if platform.machine() != 'aarch64':
        parser.error('Run on the AArch64 target device')
    output = pathlib.Path(args.output)
    if output.exists():
        parser.error('Output already exists: ' + str(output))
    version = subprocess.check_output(
        ['dpkg-query', '-W', '-f=${Version}', 'camera-engine-rkisp'], text=True).strip()
    if version != '2.3.0':
        parser.error('Expected validated camera-engine-rkisp 2.3.0, found ' + version)
    roots = [pathlib.Path('/usr/lib/librkisp.so'), pathlib.Path('/usr/lib/librkisp_api.so')]
    roots += sorted(pathlib.Path('/usr/lib/rkisp').rglob('*.so*'))
    cache = subprocess.check_output(['ldconfig', '-p'], text=True)
    names = {}
    for line in cache.splitlines():
        match = re.match(r'\s*(\S+) \(.*\) => (\S+)', line)
        if match and pathlib.Path(match[2]).exists():
            names.setdefault(match[1], pathlib.Path(match[2]))
    paths = set()
    pending = list(roots)
    while pending:
        path = pending.pop()
        if path in paths:
            continue
        if not path.exists():
            raise RuntimeError('Missing dependency: ' + str(path))
        # Preserve every symlink and its complete chain, including absolute targets.
        paths.add(path)
        if path.is_symlink():
            target = pathlib.Path(os.readlink(path))
            pending.append(target if target.is_absolute() else path.parent / target)
            continue
        dynamic = subprocess.check_output(['readelf', '-d', str(path)], text=True)
        for name in re.findall(r'\(NEEDED\).*\[(.*?)\]', dynamic):
            if name not in names:
                raise RuntimeError('Cannot resolve dependency: ' + name)
            pending.append(names[name])
    manifest = {'package': 'camera-engine-rkisp', 'version': version,
                'uname': platform.uname()._asdict(), 'files': []}
    for path in sorted(paths):
        entry = {'path': str(path)}
        if path.is_symlink():
            entry['symlink'] = os.readlink(path)
        else:
            entry['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
        manifest['files'].append(entry)
    import io
    with tarfile.open(output, 'w:gz', dereference=False) as archive:
        for path in sorted(paths):
            archive.add(path, arcname='rootfs/' + str(path).lstrip('/'), recursive=False)
        data = json.dumps(manifest, ensure_ascii=False, indent=2).encode()
        entry = tarfile.TarInfo('manifest.json')
        entry.size = len(data)
        archive.addfile(entry, io.BytesIO(data))
    print('Exported', len(paths), 'files to', output, '(' + str(output.stat().st_size) + ' bytes)')


if __name__ == '__main__':
    main()
