"""Compact score policies must decide exactly like the text exports.

Builds the shared adapter fixture, converts every published cell, and compares
mus-bot with --score-strategies against --score-compact on the same queries.
"""
import json
import pathlib
import subprocess
import sys
import tempfile

bot, fixture_builder, converter = sys.argv[1:]
TOLERANCE = 3 / 65535  # Two quantized policies, renormalized over legal actions.


def observation(a, b, phase='grande', legal=None):
    return dict(player=0, phase=phase, hand=[0] * 7 + [4], mus_stop_player=0,
                match_scores=[a, b], hand_start_scores=[a, b], pending_wager_total=0,
                legal_actions=legal or [dict(type='pass'), dict(type='raise', amount=2),
                                        dict(type='raise', amount=10), dict(type='ordago')])


with tempfile.TemporaryDirectory(prefix='mus-compact-') as directory:
    root = pathlib.Path(directory)
    checkpoint = root / 'generic.cfr'
    scores = root / 'scores'
    compact = root / 'compact'
    scores.mkdir()
    compact.mkdir()
    subprocess.run([fixture_builder, str(checkpoint), str(scores)], check=True,
                   stdout=subprocess.DEVNULL)

    conversion = subprocess.run(
        [converter, '--table', str(scores / 'values.txt'), '--checkpoints', str(scores),
         '--output', str(compact)], capture_output=True, text=True, check=True)
    converted = [json.loads(line) for line in conversion.stdout.splitlines()]
    assert converted, conversion.stderr
    assert all(entry['bytes'] > 0 for entry in converted), converted
    published = sum(1 for line in (scores / 'values.txt').read_text().splitlines()[1:] if line)
    assert len(converted) == published, (len(converted), published)

    for entry in converted:
        a, b = entry['cell'].split(':')
        name = f'score-{int(a):02d}-{int(b):02d}'
        check = subprocess.run([converter, '--verify', str(scores / f'{name}.cfr.zst'),
                                str(compact / f'{name}.mscz')], capture_output=True, text=True)
        assert check.returncode == 0, check.stderr
        assert json.loads(check.stdout)['mismatched_nodes'] == 0, check.stdout

    base = [bot, '--strategy', str(checkpoint) + '.txt', '--seed', '123',
            '--score-table', str(scores / 'values.txt')]
    text_command = base + ['--score-strategies', str(scores)]
    compact_command = base + ['--score-compact', str(compact)]

    def run(queries, command):
        result = subprocess.run(command, input='\n'.join(map(json.dumps, queries)) + '\n',
                                text=True, capture_output=True, check=True)
        assert not result.stderr, result.stderr
        return [json.loads(line) for line in result.stdout.splitlines()]

    queries = [observation(a, b) for a, b in [(30, 30), (35, 35), (39, 21), (21, 39), (39, 30)]]
    queries.append(observation(30, 30, 'mus', [dict(type='mus'), dict(type='no_mus')]))
    queries.append(observation(30, 30, 'grande', [dict(type='pass'), dict(type='raise', amount=3),
                                                  dict(type='raise', amount=7)]))
    queries.append(observation(29, 30))  # Outside the fixture's coverage.

    text_results = run(queries, text_command)
    compact_results = run(queries, compact_command)
    assert len(text_results) == len(compact_results) == len(queries)
    checked = 0
    for query, text, small in zip(queries, text_results, compact_results):
        assert text['ok'] and small['ok'], (text, small)
        trained = text['diagnostic']['strategy'] == 'ordago-score'
        assert small['diagnostic']['strategy'] == text['diagnostic']['strategy']
        assert small['diagnostic']['strategy_reason'] == text['diagnostic']['strategy_reason']
        assert small['diagnostic']['method'] == text['diagnostic']['method'], (text, small)
        # The generic score bias never applies once a table is enabled.
        assert small['diagnostic']['ordago_factor'] == 1, small
        if trained:
            assert not small['diagnostic']['score_proxy']
        expected = {(p['type'], p.get('amount')): p['probability'] for p in text['probabilities']}
        actual = {(p['type'], p.get('amount')): p['probability'] for p in small['probabilities']}
        assert expected.keys() == actual.keys(), (expected, actual)
        for action, probability in expected.items():
            assert abs(probability - actual[action]) <= TOLERANCE, (action, probability,
                                                                    actual[action])
        checked += 1

    # A missing or corrupt compact file is an error, never a silent fallback.
    (compact / 'score-30-30.mscz').unlink()
    missing = subprocess.run(compact_command, input='\n', text=True, capture_output=True)
    assert missing.returncode != 0 and 'error' in missing.stderr, missing

print(json.dumps({'tests': 'passed', 'cells': len(converted), 'queries': checked}))
