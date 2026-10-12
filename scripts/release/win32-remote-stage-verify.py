#!/usr/bin/env python3
"""Verify a returned Windows stage, then publish it with rollback on I/O failure."""
import hashlib
import json
import pathlib
import shutil
import stat
import sys
import zipfile


def record(path, relative):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return {'path': relative, 'size': path.stat().st_size, 'sha256': digest.hexdigest()}


def verify(scratch):
    inventory = json.loads((scratch / 'inventory.json').read_text(encoding='utf-8-sig'))
    incoming = scratch / 'incoming'
    incoming.mkdir()
    with zipfile.ZipFile(scratch / 'stage.zip') as archive:
        seen = set()
        for member in archive.infolist():
            # ZipFile on older .NET uses backslashes. Inventory always uses '/'.
            name = member.filename.replace('\\', '/')
            path = pathlib.PurePosixPath(name)
            if (not name or path.is_absolute() or '..' in path.parts
                    or ':' in name or path.as_posix() != name.rstrip('/')
                    or name.casefold().rstrip('/') in seen
                    or stat.S_ISLNK(member.external_attr >> 16)):
                raise ValueError('unsafe or duplicate zip member: ' + name)
            seen.add(name.casefold().rstrip('/'))
            target = incoming.joinpath(*path.parts)
            if member.is_dir() or name.endswith('/'):
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(member) as source, target.open('wb') as output:
                    shutil.copyfileobj(source, output)
    actual = sorted((record(p, p.relative_to(incoming).as_posix())
                     for p in incoming.rglob('*') if p.is_file()), key=lambda r: r['path'])
    expected = sorted(inventory['files'], key=lambda r: r['path'])
    if not actual or actual != expected:
        raise ValueError('stage file set, size or SHA-256 mismatch')
    if record(scratch / 'toolchain.txt', 'toolchain.txt') != inventory['toolchain']:
        raise ValueError('toolchain size or SHA-256 mismatch')
    return incoming


def publish(scratch, incoming, destination):
    # Backups and new files share a filesystem. Restore both old names on any
    # rename failure. This is a single-writer transaction, not crash atomicity.
    pairs = [(incoming, destination),
             (scratch / 'toolchain.txt', destination.with_name(destination.name + '.toolchain.txt'))]
    backups = []
    installed = []
    try:
        for index, (_, target) in enumerate(pairs):
            if target.exists() or target.is_symlink():
                backup = scratch / ('previous-' + str(index))
                target.rename(backup)
                backups.append((backup, target))
        for source, target in pairs:
            source.rename(target)
            installed.append(target)
    except OSError:
        for target in reversed(installed):
            if target.is_dir() and not target.is_symlink():
                shutil.rmtree(target)
            else:
                target.unlink()
        for backup, target in reversed(backups):
            backup.rename(target)
        raise


def main():
    scratch, destination = map(pathlib.Path, sys.argv[1:])
    try:
        incoming = verify(scratch)
        publish(scratch, incoming, destination)
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
        print('win32 stage verification/publication refused: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
