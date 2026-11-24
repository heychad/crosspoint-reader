#!/usr/bin/env python3
"""Report upstream deltas without changing the working tree or merging code."""

import argparse
from collections import Counter
from datetime import datetime, timezone
import json
import re
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(
        ['git', *args], cwd=ROOT, text=True, stderr=subprocess.PIPE
    ).strip()


def release_order(tag):
    """Order stable and RC spellings used by both projects."""
    match = re.fullmatch(r'v?(\d+)\.(\d+)\.(\d+)(?:-?rc(?:[.-]?(\d+))?)?', tag, re.ASCII)
    if not match:
        return None
    major, minor, patch, sequence = match.groups()
    return int(major), int(minor), int(patch), 'rc' not in tag, int(sequence or 0)


def advertised_releases(remote, selected, selected_commit):
    """Query just this remote. Annotated tags compare by peeled commit."""
    tags = {}
    peeled = {}
    for line in git('ls-remote', '--tags', remote).splitlines():
        oid, ref = line.split('\t')
        tag = ref[len('refs/tags/'):]
        if tag.endswith('^{}'):
            peeled[tag[:-3]] = oid
        else:
            tags[tag] = oid
    tag = selected[len('refs/tags/'):] if selected.startswith('refs/tags/') else selected
    selected_order = release_order(tag)
    release_tags = (name for name in tags if release_order(name) is not None)
    newest = sorted(release_tags, key=lambda name: (release_order(name), name), reverse=True)
    target = peeled.get(tag, tags.get(tag))
    return {
        'latest_stable': next((name for name in newest if 'rc' not in name), None),
        'latest_rc': next((name for name in newest if 'rc' in name), None),
        'selected_tag_available': tag in tags,
        'selected_commit_matches': target == selected_commit if tag in tags else None,
        'newer_tags': [name for name in newest if selected_order and release_order(name) > selected_order],
    }


def revision(ref):
    return git('rev-parse', '--verify', ref + '^{commit}')


def upstream_revisions(remote, refs, fetch, allow_development):
    """Resolve selected tags in their owner's namespace, including the comparison base."""
    selections = []
    specs = []
    for ref in refs:
        name = ref[len('refs/tags/'):] if ref.startswith('refs/tags/') else ref
        if release_order(name) is not None or (allow_development and ref.startswith('refs/tags/')):
            source = 'refs/tags/' + name
            scoped = 'refs/crosspoint-audit/' + remote + '/tags/' + name
            if subprocess.run(['git', 'check-ref-format', source], cwd=ROOT).returncode:
                raise ValueError('Invalid tag: ' + ref)
            specs.append('+' + source + ':' + scoped)
            selections.append((source, scoped))
        elif allow_development:
            selections.append((ref, None))
        else:
            raise ValueError('Stable or RC tag required: ' + ref + '; use --allow-development-ref for an explicit audit')
    if fetch:
        options = ['fetch', '--no-tags', '--no-recurse-submodules', '--no-write-fetch-head', remote]
        if any(scoped is None for _, scoped in selections):
            git(*options)
        if specs:
            git(*options, *dict.fromkeys(specs))
    resolved = []
    for ref, scoped in selections:
        target = ref
        if scoped and (fetch or subprocess.run(
                ['git', 'show-ref', '--verify', '--quiet', scoped], cwd=ROOT).returncode == 0):
            target = scoped
        resolved.append((ref, revision(target)))
    return resolved


def paths(base, tip):
    # NUL records preserve whitespace and Unicode in repository filenames.
    raw = subprocess.check_output(
        ['git', 'diff', '--name-only', '-z', base, tip], cwd=ROOT
    )
    return set(raw.decode('utf-8', errors='replace').rstrip('\0').split('\0')) - {''}


def commits(base, tip):
    return int(git('rev-list', '--count', base + '..' + tip))


def settings_inventory(ref):
    """Detect source drift, including value/codec changes with unchanged keys."""
    path = ROOT / 'docs/downstream/RC2_SETTINGS_INVENTORY.json'
    inventory = json.loads(path.read_text())
    if inventory['schema_version'] != 1 or not inventory['source_blobs']:
        raise ValueError('Unsupported or empty settings inventory')
    delta = json.loads((path.parent / 'RC3_SETTINGS_DELTA.json').read_text())
    if delta['base_reference'] != inventory['reference']:
        raise ValueError('RC3 settings delta does not match the reviewed base')
    ancestor = subprocess.run(['git', 'merge-base', '--is-ancestor', delta['reference'], ref],
                              cwd=ROOT, capture_output=True)
    if ancestor.returncode == 0:
        inventory['reference'] = delta['reference']
        inventory['source_blobs'].update(delta['source_blobs'])
        inventory['groups'].extend(delta['groups'])
    elif ancestor.returncode != 1:
        # An RC2-only checkout can still audit its older tags.
        prior = subprocess.run(['git', 'merge-base', '--is-ancestor', ref, inventory['reference']],
                               cwd=ROOT, capture_output=True)
        if prior.returncode != 0:
            raise ValueError('Cannot resolve settings audit ancestry. Fetch both release references.')
    allowed = {'mapped', 'conditional', 'partial', 'open', 'later',
               'board-excluded', 'preserve-native', 'skip'}
    counts = Counter()
    seen = set()
    open_keys = []
    for group in inventory['groups']:
        handling = group['handling']
        if handling not in allowed or not group['keys'] or not group['behavior']:
            raise ValueError('Invalid settings inventory group')
        for key in group['keys']:
            if not isinstance(key, str) or not key or key in seen:
                raise ValueError('Invalid or duplicate settings inventory key')
            seen.add(key)
            counts[handling] += 1
            if handling == 'open':
                open_keys.append(key)
        if not group['evidence'] or any(not (ROOT / item).is_file() for item in group['evidence']):
            raise ValueError('Missing settings inventory evidence file')
    changed = []
    for source, expected in inventory['source_blobs'].items():
        # ls-tree returns no record when a reviewed file was removed upstream.
        record = git('ls-tree', ref, '--', source)
        actual = record.split()[2] if record else None
        if actual != expected:
            changed.append(source)
    return {
        'reviewed_commit': inventory['reference'],
        'catalog_and_save_keys': len(seen),
        'classifications': dict(sorted(counts.items())),
        'open_keys': sorted(open_keys),
        'source_review_required': bool(changed),
        'changed_sources': changed,
        'scope': 'Classification and source-drift audit; not migration or device validation',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fetch', action='store_true',
                        help='fetch both upstreams without merging or updating submodules')
    parser.add_argument('--crosspoint', default='refs/tags/2.0.0')
    parser.add_argument('--crossink', default='refs/tags/v2.0.0-rc3')
    parser.add_argument('--crossink-base', default='refs/tags/v1.6.1',
                        help='released CrossInk baseline; defaults to v1.6.1')
    parser.add_argument('--allow-development-ref', action='store_true',
                        help='explicitly inspect a non-release ref; does not authorize integration')
    parser.add_argument('--json', action='store_true', help='emit machine-readable output')
    args = parser.parse_args()
    (args.crosspoint, cp), = upstream_revisions(
        'upstream', [args.crosspoint], args.fetch, args.allow_development_ref)
    (args.crossink, ci), (args.crossink_base, ci_base) = upstream_revisions(
        'crossink', [args.crossink, args.crossink_base], args.fetch, args.allow_development_ref)
    head = revision('HEAD')
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
                       'fork_commits_beyond_tag': commits(cp, head),
                       'overlap_paths': sorted(local_paths & cp_paths)},
        'crossink': {'ref': args.crossink, 'tip': ci, 'baseline': ci_base,
                     'commits_since_baseline': commits(ci_base, ci),
                     'changed_paths': sorted(ci_paths),
                     'overlap_paths': sorted(local_paths & ci_paths),
                     'settings_inventory': settings_inventory(ci)},
    }
    if args.fetch:
        for project, remote in [('crosspoint', 'upstream'), ('crossink', 'crossink')]:
            item = report[project]
            item['advertised_releases'] = advertised_releases(remote, item['ref'], item['tip'])
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print('Upstream snapshot (' + ('fresh fetch' if args.fetch else 'local refs; freshness unknown') + ')')
        print('Branch: ' + report['branch'] + ' @ ' + head[:12])
        print(f'CrossPoint {args.crosspoint} ({cp[:12]}): {report["crosspoint"]["unmerged_commits"]} unmerged commits')
        print(f'Fork commits beyond that tag: {report["crosspoint"]["fork_commits_beyond_tag"]} (includes local features and prior integration)')
        print(f'CrossInk {args.crossink} ({ci[:12]}): {report["crossink"]["commits_since_baseline"]} commits since {ci_base[:12]}')
        for name in ('crosspoint', 'crossink'):
            tags = report[name].get('advertised_releases')
            if tags is not None:
                print(f'{name} remote tags: stable {tags["latest_stable"] or "none"}, RC {tags["latest_rc"] or "none"}')
                if not tags['selected_tag_available']:
                    print(f'{name}: the selected tag is absent from this remote.')
                elif not tags['selected_commit_matches']:
                    print(f'{name}: the remote tag points to a different commit.')
                if tags['newer_tags']:
                    print(f'{name} newer tags to review: ' + ', '.join(tags['newer_tags']))
            overlap = report[name]['overlap_paths']
            print(f'{name} overlapping paths ({len(overlap)}):')
            for path in overlap:
                print('  ' + path)
        print('Overlap is a review hint, not proof of a conflict or an unported feature.')
        audit = report['crossink']['settings_inventory']
        print(f'Settings inventory: {audit["catalog_and_save_keys"]} classified catalog/save keys')
        print('Open compatibility keys: ' + (', '.join(audit['open_keys']) or 'none recorded'))
        if audit['source_review_required']:
            print('Settings source review required:')
            for path in audit['changed_sources']:
                print('  ' + path)
        else:
            print('Reviewed settings sources match the pinned inventory.')
        print(audit['scope'])
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (subprocess.CalledProcessError, ValueError) as exc:
        detail = exc.stderr.strip() if isinstance(exc, subprocess.CalledProcessError) and exc.stderr else str(exc)
        print('Snapshot failed: ' + detail, file=sys.stderr)
        sys.exit(1)
