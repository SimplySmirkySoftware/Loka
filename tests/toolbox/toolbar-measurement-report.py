#!/usr/bin/env python3
"""Preserve build provenance and refuse incomplete/over-budget run journals."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shlex
import sys


def verdict(path):
    lines = Path(path).read_text().splitlines()
    summaries = [line for line in lines if line.startswith('toolbar_summary ')]
    environments = [line for line in lines if line.startswith('toolbar_environment ')]
    if len(environments) != 1:
        raise ValueError('missing or duplicate rig facts')
    environment = dict(item.split('=', 1) for item in environments[0].split()[1:])
    if int(environment['observed_resolution_us']) <= 0:
        raise ValueError('clock resolution not observed')
    if len(summaries) != 1:
        raise ValueError('missing or duplicate summary')
    fields = dict(item.split('=', 1) for item in summaries[0].split()[1:])
    draws = [dict(item.split('=', 1) for item in line.split()[1:])
             for line in lines if line.startswith('toolbar_draw ')]
    samples = [d for d in draws if d['verdict_sample'] == '1']
    if (fields['verdict'] != 'PASS' or fields['missing'] != '0' or fields['overflow'] != '0'
            or len(samples) != int(fields['count']) or int(fields['operations']) < 10
            or not samples or any(d['complete'] != '1' for d in draws)
            or max(int(d['erase_us']) for d in samples) != int(fields['max_us'])
            or int(fields['max_us']) > 17000):
        raise ValueError('registration STOP: incomplete, overflowed, or over-budget measurement')
    groups = {}
    for draw in draws:
        groups.setdefault((draw['operation'], draw['step']), []).append(draw)
    if len({(d['operation'], d['step']) for d in samples}) != int(fields['operations']):
        raise ValueError('operation count disagrees with measured draws')
    expected = {'1': 10, '2': 15, '3': 10, '4': 10}[environment['app']]
    if int(fields['operations']) != expected:
        raise ValueError('driver did not complete its value-changing operation range')
    for group in groups.values():
        if any(int(d['draws']) != len(group) or int(d['operation_total_us']) != sum(int(x['erase_us']) for x in group)
               for d in group):
            raise ValueError('per-operation diagnostics disagree with measured draws')
    print(summaries[0])


def rows(path):
    lines = Path(path).read_text().splitlines()
    def records(prefix):
        return [dict(item.split('=', 1) for item in line.split()[1:])
                for line in lines if line.startswith(prefix + ' ')]
    environments = records('toolbar_row_environment')
    if (len(environments) != 1 or environments[0]['clock'] != 'Microseconds'
            or int(environments[0]['observed_resolution_us']) <= 0):
        raise ValueError('missing Row clock facts')
    summaries = records('toolbar_row_summary')
    samples = records('toolbar_row')
    inputs = records('toolbar_row_inputs')
    names = {'k1', 'k2', 'k4', 'fit', 'floor', 'b-equals-s', 'aligned-title'}
    if (summaries != [{'complete': '1', 'cases': '7'}] or len(samples) != 7
            or {s['case'] for s in samples} != names or len(inputs) != 7
            or {s['case'] for s in inputs} != names):
        raise ValueError('incomplete Row measurement')
    for sample in samples:
        if (int(sample['repetitions']) < 30 or int(sample['median_us']) < 0
                or int(sample['maximum_us']) < int(sample['median_us'])
                or (sample['case'] not in ('b-equals-s', 'aligned-title') and sample['offer'] != sample['occupied'])
                or (sample['case'] == 'b-equals-s' and (sample['offer'], sample['occupied']) != ('27', '28'))):
            raise ValueError('invalid one-pass Row statistics')
    for row in inputs:
        expected_source = 'synthetic-unit' if row['case'] == 'b-equals-s' else 'toolbox'
        if row['source'] != expected_source:
            raise ValueError('incorrect source attribution')
        scans = int(row['scans'])
        if scans > int(row['k']) or (row['case'] in ('k1', 'fit', 'b-equals-s') and scans != 0):
            raise ValueError('invalid scan count')
        if row['case'] == 'k4' and scans < 2:
            raise ValueError('k4 did not exercise multiple scans')
    heights = records('toolbar_row_height')
    if len(heights) != 1 or heights[0]['case'] != 'aligned-title':
        raise ValueError('missing Title height comparison')
    height = heights[0]
    answered = height['answered'] == '1'
    equal = answered and int(height['query']) == int(height['leaf'])
    if (height['answered'] not in ('0', '1') or int(height['leaf']) <= 0
            or height['equal'] != str(int(equal)) or (answered and not equal)
            or (not answered and height['query'] != '-1')):
        raise ValueError('invalid Title height comparison')
    print('toolbar_row_height ' + ' '.join(f'{key}={value}' for key, value in height.items()))
    samples = {s['case']: s for s in samples}
    print('toolbar_row case=aligned-title ' + ' '.join(f'{key}={value}' for key, value in samples['aligned-title'].items() if key != 'case'))
    for name in ('k2', 'k4'):
        row = samples[name]
        print('toolbar_row_delta case=' + name, end=' ')
        print(' '.join(f'{stat}_over_{base}_us=' + str(int(row[stat + '_us']) - int(samples[base][stat + '_us']))
                       for base in ('fit', 'k1') for stat in ('median', 'maximum')))
    print('toolbar_row_summary complete=1 cases=7 (cost only; no heaviness verdict)')


def provenance(build, binary, destination):
    build, binary, destination = map(Path, (build, binary, destination))
    commands = json.loads((build / 'compile_commands.json').read_text())
    selected = [row for row in commands if ('ToolbarMeasurementMain.cpp' in row['file'] or 'ToolbarRowMeasurementMain.cpp' in row['file'])
                or 'LokaAppleToolboxTestCore.dir' in row.get('command', '')]
    if (not selected or any('LOKA_PROFILE_FUNC_TICKS=1' in row['command'] for row in selected)
            or any('-DTEST_BUILD' not in row['command'] for row in selected)):
        raise ValueError('missing build commands or function profiling enabled')
    boot = os.environ.get('MAME_HDA', '')
    if len(boot) > 2 and boot[1:3] == ':\\':
        boot = subprocess.check_output(['wslpath', '-u', boot], text=True).strip()
    if not Path(boot).is_file():
        raise ValueError('MAME_HDA must identify the System boot image')
    def digest(path):
        h = hashlib.sha256()
        with open(path, 'rb') as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                h.update(block)
        return h.hexdigest()
    (destination / 'build-and-system.json').write_text(json.dumps({
        'source_revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=Path(__file__).resolve().parents[2], text=True).strip(),
        'compiler': subprocess.check_output([shlex.split(selected[0]['command'])[0], '--version'], text=True),
        'machine': 'maciix', 'binary_sha256': digest(binary), 'system_disk_sha256': digest(boot),
        'build_commands': selected,
        'clock': 'Microseconds; observed resolution and empty-bracket cost in each run log',
        'screen_and_system': 'actual depth, size and Gestalt System version in each run log',
        'semantics': ('one Row consultation plus next for every seat; no drawing' if 'MeasureRows' in binary.name
                      else 'maximum erase-through-grow-box duration; no cost subtraction'),
    }, indent=2) + '\n')

if __name__ == '__main__':
    try:
        if sys.argv[1] == 'verdict':
            verdict(sys.argv[2])
        elif sys.argv[1] == 'rows':
            rows(sys.argv[2])
        elif sys.argv[1] == 'provenance':
            provenance(*sys.argv[2:])
        else:
            raise ValueError('expected verdict, rows or provenance')
    except (ValueError, KeyError, OSError) as error:
        sys.exit(str(error))
