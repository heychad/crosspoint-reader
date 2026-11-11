#!/usr/bin/env python3
"""Report upstream deltas without changing the working tree or merging code."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(
        ['git', *args], cwd=ROOT, text=True, stderr=subprocess.PIPE
    ).strip()


def revision(ref):
    return git('rev-parse', '--verify', ref + '^{commit}')


def paths(base, tip):
    # NUL records preserve whitespace and Unicode in repository filenames.
    raw = subprocess.check_output(
        ['git', 'diff', '--name-only', '-z', base, tip], cwd=ROOT
    )
    return set(raw.decode('utf-8', errors='replace').rstrip('\0').split('\0')) - {''}


def commits(base, tip):
    return int(git('rev-list', '--count', base + '..' + tip))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fetch', action='store_true',
                        help='fetch both upstreams without merging or updating submodules')
    parser.add_argument('--crosspoint', default='upstream/develop')
    parser.add_argument('--crossink', default='crossink/development')
    parser.add_argument('--crossink-base', default='b8feaec4d',
                        help='last inspected CrossInk baseline; defaults to v2.0.0-rc')
    parser.add_argument('--json', action='store_true', help='emit machine-readable output')
    args = parser.parse_args()
    if args.fetch:
        for remote in ('upstream', 'crossink'):
            subprocess.run(['git', 'fetch', '--no-recurse-submodules', remote],
                           cwd=ROOT, check=True)
    head = revision('HEAD')
    cp = revision(args.crosspoint)
    ci = revision(args.crossink)
    ci_base = revision(args.crossink_base)
    if subprocess.run(['git', 'merge-base', '--is-ancestor', ci_base, ci],
                      cwd=ROOT, capture_output=True).returncode != 0:
        raise ValueError('CrossInk baseline is not an ancestor of its tip; choose a valid --crossink-base')
    common = git('merge-base', head, cp)
    local_paths = paths(common, head)
    cp_paths = paths(common, cp)
    ci_paths = paths(ci_base, ci)
    report = {
        'observed_at_utc': datetime.now(timezone.utc).isoformat(),
        'refs_refreshed': args.fetch,
        'branch': git('branch', '--show-current'),
        'head': head,
        'crosspoint': {'ref': args.crosspoint, 'tip': cp, 'common_base': common,
                       'unmerged_commits': commits(head, cp),
                       'overlap_paths': sorted(local_paths & cp_paths)},
        'crossink': {'ref': args.crossink, 'tip': ci, 'baseline': ci_base,
                     'commits_since_baseline': commits(ci_base, ci),
                     'changed_paths': sorted(ci_paths),
                     'overlap_paths': sorted(local_paths & ci_paths)},
    }
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print('Upstream snapshot (' + ('fresh fetch' if args.fetch else 'local refs; freshness unknown') + ')')
        print('Branch: ' + report['branch'] + ' @ ' + head[:12])
        print(f'CrossPoint {cp[:12]}: {report["crosspoint"]["unmerged_commits"]} unmerged commits')
        print(f'CrossInk {ci[:12]}: {report["crossink"]["commits_since_baseline"]} commits since {ci_base[:12]}')
        for name in ('crosspoint', 'crossink'):
            overlap = report[name]['overlap_paths']
            print(f'{name} overlapping paths ({len(overlap)}):')
            for path in overlap:
                print('  ' + path)
        print('Overlap is a review hint, not proof of a conflict or an unported feature.')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (subprocess.CalledProcessError, ValueError) as exc:
        detail = exc.stderr.strip() if isinstance(exc, subprocess.CalledProcessError) and exc.stderr else str(exc)
        print('Snapshot failed: ' + detail, file=sys.stderr)
        sys.exit(1)
