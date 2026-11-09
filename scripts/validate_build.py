#!/usr/bin/env python3
"""Build a copied project; never overwrite the tracked .pio artifacts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('phase')
parser.add_argument('--environment', default='esp32dev')
parser.add_argument('--core-dir')
parser.add_argument('--target')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
work = Path(tempfile.gettempdir()) / 'pantalla-emt-validation' / 'working'
work.mkdir(parents=True, exist_ok=True)
for name in ('src', 'include', 'lib', 'data'):
    if (work / name).exists():
        shutil.rmtree(work / name)
    shutil.copytree(root / name, work / name)
for name in ('platformio.ini', 'custom_partitions.csv'):
    shutil.copy2(root / name, work / name)
if not (work / '.pio/libdeps').exists():
    shutil.copytree(root / '.pio/libdeps', work / '.pio/libdeps')
if not (work / '.pio/libdeps' / args.environment).exists():
    shutil.copytree(root / '.pio/libdeps/esp32dev', work / '.pio/libdeps' / args.environment)
output = root / 'validation' / '2026-11-09'
output.mkdir(parents=True, exist_ok=True)
sources = [p for name in ('src', 'include', 'lib', 'data') for p in (root / name).rglob('*') if p.is_file()]
sources += [root / 'platformio.ini', root / 'custom_partitions.csv']
manifest = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(sources)}
(output / f'{args.phase}-sources.json').write_text(json.dumps(manifest, indent=2) + '\n')
env = os.environ.copy()
if args.core_dir:
    env['PLATFORMIO_CORE_DIR'] = args.core_dir
log = output / f'{args.phase}-build.log'
with log.open('w') as f:
    command = ['pio', 'run', '-d', str(work), '-e', args.environment]
    if args.target: command += ['-t', args.target]
    result = subprocess.run(command, env=env, stdout=f, stderr=subprocess.STDOUT)
print('\n'.join(log.read_text().splitlines()[-16:]))
raise SystemExit(result.returncode)
