"""Check score routing and policy preservation with small exported strategies."""
import copy
import json
import pathlib
import subprocess
import sys
import tempfile

bot, fixture_builder = sys.argv[1:]


def probability(result, action, amount=None):
    return sum(p['probability'] for p in result['probabilities']
               if p['type'] == action and (amount is None or p.get('amount') == amount))


with tempfile.TemporaryDirectory(prefix='mus-hybrid-') as directory:
    root = pathlib.Path(directory)
    checkpoint = root / 'generic.cfr'
    scores = root / 'scores'
    scores.mkdir()
    subprocess.run([fixture_builder, str(checkpoint), str(scores)], check=True,
                   stdout=subprocess.DEVNULL)
    generic = [bot, '--strategy', str(checkpoint) + '.txt', '--seed', '123']
    hybrid = generic + ['--score-table', str(scores / 'values.txt'),
                        '--score-strategies', str(scores)]

    def run(queries, command=hybrid):
        result = subprocess.run(command, input='\n'.join(map(json.dumps, queries)) + '\n',
                                text=True, capture_output=True, check=True)
        assert not result.stderr, result.stderr
        return [json.loads(line) for line in result.stdout.splitlines()]

    def observation(a, b):
        return dict(player=0, phase='grande', hand=[0] * 7 + [4], mus_stop_player=0,
                    match_scores=[a, b], hand_start_scores=[a, b], pending_wager_total=0,
                    legal_actions=[dict(type='pass'), dict(type='raise', amount=2),
                                   dict(type='raise', amount=10), dict(type='ordago')])

    # Coverage follows the published cells, including 39:21 and exactly 30:30.
    queries = [observation(a, b) for a, b in [(29, 30), (30, 30), (39, 21),
                                             (21, 39), (39, 20), (39, 30), (30, 39)]]
    results = run(queries)
    for q, result in zip(queries, results):
        assert result['ok'], result
        diagnostic = result['diagnostic']
        covered = sum(q['hand_start_scores']) >= 60
        assert diagnostic['strategy'] == ('ordago-score' if covered else 'ordago')
        assert diagnostic['strategy_scores'] == (q['hand_start_scores'] if covered else None)
        assert diagnostic['strategy_reason'] == ('trained_start_score' if covered else 'untrained_start_score')
        if covered:
            expected = .25 if q['match_scores'][0] >= q['match_scores'][1] else .75
            assert abs(probability(result, 'ordago') - expected) < 1e-12
            assert diagnostic['ordago_factor'] == 1 and not diagnostic['score_proxy']
            assert diagnostic['method'] == 'exact'
        else:
            legacy = run([q], generic)[0]
            assert result['probabilities'] == legacy['probabilities']
            assert diagnostic['ordago_factor'] == legacy['diagnostic']['ordago_factor']
    # Disabling the generic bias does not change policies in trained cells.
    assert [r['probabilities'] for r in run(queries[1:4], hybrid + ['--no-score-adjustment'])] == [r['probabilities'] for r in results[1:4]]

    # The same exact mus key occurs in every file. Switching files must not reuse its cache.
    mus_queries = []
    for a, b in [(39, 30), (30, 39), (39, 30), (29, 30), (30, 39)]:
        q = observation(a, b)
        q.update(phase='mus', legal_actions=[dict(type='mus'), dict(type='no_mus')])
        q.pop('pending_wager_total')
        mus_queries.append(q)
    assert [probability(r, 'mus') for r in run(mus_queries)] == [.25, .75, .25, .75, .75]
    # Both positions and discards use the score backend as well.
    q = observation(39, 30)
    q.update(player=1, history=[dict(player=0, type='pass')])
    result = run([q])[0]
    assert abs(probability(result, 'ordago') - .75) < 1e-12
    q.update(phase='discard', history=[], discard_counts=[2, 0],
             legal_actions=[dict(type='discard', mask=1), dict(type='discard', mask=15)])
    result = run([q])[0]
    assert result['ok'] and result['diagnostic']['strategy'] == 'ordago-score'
    assert result['probabilities'][0]['probability'] == .75
    q.update(phase='mus', mus_count=1, discard_counts=[2, 2],
             legal_actions=[dict(type='mus'), dict(type='no_mus')])
    result = run([q])[0]
    assert result['diagnostic']['method'] == 'mus_limit' and probability(result, 'no_mus') == 1

    # A direct ordago response must also avoid the score multiplier.
    q = observation(39, 30)
    q.update(player=1, history=[dict(player=0, type='ordago')],
             legal_actions=[dict(type='reject'), dict(type='accept')])
    result = run([q])[0]
    assert result['diagnostic']['ordago_factor'] == 1
    assert probability(result, 'accept') == .75

    # Collected points change the key within 30:30, not the strategy file.
    paid = observation(30, 30)
    paid.update(phase='chica', match_scores=[34, 30], known_hand_scores=[4, 0],
                prior_wager_total=4, previous_strengths=[3])
    pending = copy.deepcopy(paid)
    pending.update(match_scores=[30, 30], known_hand_scores=[0, 0], pending_wager_total=4)
    result_paid, result_pending = run([paid, pending])
    for result in [result_paid, result_pending]:
        assert result['diagnostic']['strategy_scores'] == [30, 30]
        assert result['diagnostic']['method'] == 'exact'
        assert not result['diagnostic']['score_proxy']
    assert abs(probability(result_paid, 'ordago') - .7) < 1e-12
    assert abs(probability(result_pending, 'ordago') - .2) < 1e-12
    assert result_paid['diagnostic']['sources'][0]['prior_total'] == 0
    assert result_paid['diagnostic']['sources'][0]['settled_score'] == 34
    assert result_pending['diagnostic']['sources'][0]['prior_total'] == 4
    crossing = copy.deepcopy(paid)
    crossing.update(hand_start_scores=[29, 30], match_scores=[30, 30],
                    known_hand_scores=[1, 0], prior_wager_total=1)
    assert run([crossing])[0]['diagnostic']['strategy'] == 'ordago'
    assert run([observation(30, 30)])[0]['diagnostic']['strategy'] == 'ordago-score'

    # Arbitrary real wagers still interpolate or use nearby betting histories.
    q = observation(30, 30)
    q.update(history=[dict(player=0, type='pass'), dict(player=1, type='raise', amount=3)],
             legal_actions=[dict(type='reject'), dict(type='accept'), dict(type='ordago')])
    result = run([q])[0]
    assert result['diagnostic']['method'] == 'interpolated'
    assert abs(probability(result, 'accept') - .5) < 1e-12
    q['history'] = [dict(player=0, type='pass'), dict(player=1, type='raise', amount=2),
                    dict(player=0, type='raise', amount=2), dict(player=1, type='raise', amount=2)]
    result = run([q])[0]
    assert result['diagnostic']['method'] == 'nearest'
    assert result['diagnostic']['strategy'] == 'ordago-score'
    assert result['diagnostic']['lookup_count'] <= 64 and result['diagnostic']['ordago_factor'] == 1
    # Never substitute a node with other public scores or pending contracts.
    missing = copy.deepcopy(pending)
    missing['pending_wager_total'] = 2
    result = run([missing])[0]
    assert result['diagnostic']['method'] == 'fallback'
    assert result['diagnostic']['strategy'] == 'ordago-score'
    assert result['diagnostic']['sources'] == []
    missing = copy.deepcopy(paid)
    missing.update(match_scores=[33, 30], known_hand_scores=[3, 0], prior_wager_total=3)
    result = run([missing])[0]
    assert result['diagnostic']['method'] == 'fallback'
    assert result['diagnostic']['sources'] == []
    # Missing score nodes keep the documented fallback, not the generic policy.
    missing_mus = copy.deepcopy(mus_queries[0])
    missing_mus['hand'] = [4] + [0] * 7
    result = run([missing_mus])[0]
    assert result['diagnostic']['method'] == 'fallback'
    assert probability(result, 'no_mus') == 1
    assert probability(run([missing_mus], generic)[0], 'mus') == .75

    # Trained policies use the real legal list without the legacy amount cap.
    projected = observation(35, 35)
    projected['mus_stop_player'] = 1
    projected['legal_actions'] = [dict(type='pass'), dict(type='raise', amount=10), dict(type='ordago')]
    result = run([projected])[0]
    assert result['diagnostic']['method'] == 'exact'
    assert abs(probability(result, 'raise', 10) - .6) < 1e-12

    invalid = []
    for key, value in [('hand_start_scores', [40, 0]), ('hand_start_scores', [31, 30]),
                       ('hand_start_scores', [True, 30]), ('pending_wager_total', -1),
                       ('pending_wager_total', True), ('pending_wager_total', 1)]:
        bad = observation(30, 30)
        bad[key] = value
        invalid.append(bad)
    for field in ['hand_start_scores', 'pending_wager_total']:
        bad = observation(30, 30)
        del bad[field]
        invalid.append(bad)
    good = observation(30, 30)
    combined = run(invalid + [good])
    assert all(r.get('error') == 'invalid_observation' for r in combined[:-1]), combined
    assert combined[-1] == run([good])[0]  # Errors do not advance the random stream.
    for extra in [['--score-table', str(scores / 'values.txt')],
                  ['--score-strategies', str(scores)]]:
        assert subprocess.run(generic + extra, capture_output=True).returncode == 2

    # Refuse missing files, wrong cells, old versions, changed continuation values,
    # and mismatched iteration counts instead of silently using the generic policy.
    path = scores / 'score-30-30.txt'
    original = path.read_text()
    for replacement in [None, (scores / 'score-31-30.txt').read_text(),
                        original.splitlines(keepends=True)[0],
                        original.replace(' information_sets=', ' ', 1),
                        original.replace('ordago-score/v4/', 'ordago-score/v3/', 1),
                        original.replace('training_iterations=50000000', 'training_iterations=10000000', 1)]:
        if replacement is None:
            path.unlink()
        else:
            path.write_text(replacement)
        result = subprocess.run(hybrid, text=True, capture_output=True)
        assert result.returncode == 1 and not result.stdout, result
        path.write_text(original)
    table_path = scores / 'values.txt'
    original_table = table_path.read_text()
    lines = original_table.splitlines()
    for index, line in enumerate(lines):
        if line.startswith('39 39 '):
            fields = line.split()
            fields[2] = '.5'
            lines[index] = ' '.join(fields)
    table_path.write_text('\n'.join(lines) + '\n')
    assert subprocess.run(hybrid, capture_output=True).returncode == 1
    table_path.write_text(original_table)
    damaged = original.replace('ordago=0.25', 'ordago=nan')
    assert damaged != original
    path.write_text(damaged)
    result = run([good])[0]
    assert result['error'] == 'invalid_strategy_data', result
print('All hybrid adapter CLI checks passed.')
