"""Rebind copied PlatformIO SDK links to this checkout before library discovery."""
import json
import os
from pathlib import Path
import tempfile


def repair_links(project_dir, libdeps_dir, environment):
    project=Path(project_dir).resolve()
    if not environment or environment in ('.','..') or any(c in environment for c in '/\\'):
        raise RuntimeError('Invalid PlatformIO environment')
    raw_folder=Path(libdeps_dir)/environment;folder=raw_folder.resolve()
    if not folder.is_relative_to(project) or raw_folder.is_symlink():
        raise RuntimeError('Local SDK links must be inside this checkout')
    if not folder.exists():return 0
    changed=0
    for path in sorted(folder.glob('*.pio-link')):
        if path.is_symlink() or path.stat().st_size>65536:
            raise RuntimeError('Unsafe PlatformIO library link: '+path.name)
        value=json.loads(path.read_text())
        spec=value.get('spec',{}) if isinstance(value,dict) else {}
        uri=spec.get('uri','') if isinstance(spec,dict) else ''
        if not isinstance(uri,str) or not uri.startswith('symlink://freeink-sdk/'):continue
        relative=Path(uri.removeprefix('symlink://'))
        target=(project/relative).resolve();sdk=(project/'freeink-sdk').resolve()
        if ('..' in relative.parts or not target.is_dir() or not target.is_relative_to(sdk)
                or not sdk.is_relative_to(project) or (project/'freeink-sdk').is_symlink()):
            raise RuntimeError('Invalid local SDK library path: '+path.name)
        if value.get('cwd')==str(project):continue
        value['cwd']=str(project)
        temporary=None
        try:
            with tempfile.NamedTemporaryFile('w',dir=folder,prefix='.sdk-link-',delete=False) as stream:
                temporary=Path(stream.name);json.dump(value,stream);stream.write('\n')
            os.replace(temporary,path);changed+=1
        finally:
            if temporary is not None:temporary.unlink(missing_ok=True)
    return changed


if 'Import' in globals():
    Import('env')  # noqa: F821 -- supplied by SCons
    count=repair_links(env['PROJECT_DIR'],env['PROJECT_LIBDEPS_DIR'],env['PIOENV'])  # noqa: F821
    if count:print(f'Rebound {count} local SDK dependency links to this checkout')
