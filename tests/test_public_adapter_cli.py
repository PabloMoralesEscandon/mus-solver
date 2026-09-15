"""Check public generic policies through every bot backend and hybrid routing."""
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile

bot, builder = sys.argv[1:]

def probability(result, kind, amount=None):
    return sum(a['probability'] for a in result['probabilities']
               if a['type'] == kind and (amount is None or a.get('mask', a.get('amount')) == amount))

with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    checkpoint = root / 'public.cfr'
    subprocess.run([builder, '--public-fixture', str(checkpoint)], check=True, stdout=subprocess.DEVNULL)
    paths = [checkpoint, pathlib.Path(str(checkpoint) + '.zst'), pathlib.Path(str(checkpoint) + '.txt')]
    original = [hashlib.sha256(p.read_bytes()).hexdigest() for p in paths]
    commands = [[bot, flag, str(path), '--seed', '123', '--no-score-adjustment']
                for flag, path in zip(['--load', '--load-compressed', '--strategy'], paths)]

    def run(queries, command=commands[0]):
        result = subprocess.run(command, input='\n'.join(map(json.dumps, queries)) + '\n',
                                text=True, capture_output=True, check=True)
        assert not result.stderr, result.stderr
        return [json.loads(line) for line in result.stdout.splitlines()]

    def observation(phase='grande', player=0):
        previous = {'grande': 0, 'chica': 1, 'pares': 2, 'juego': 3, 'punto': 3,
                    'mus': 0, 'discard': 0}[phase]
        q = dict(player=player, phase=phase, hand=[0]*7+[4], hand_start_scores=[10,12],
                 match_scores=[10 + (previous >= 2), 12 + 2*(previous == 3)],
                 pending_wager_total=2 if previous else 0,
                 previous_strengths=[3,1,3][:previous], hand_scores_complete=False,
                 legal_actions=[dict(type='pass'), dict(type='ordago'), dict(type='raise', amount=2)],
                 history=[dict(player=0, type='pass')] if player and phase not in ['mus','discard'] else [])
        if phase == 'punto': q['hand'] = [4]+[0]*7
        if phase == 'mus': q['legal_actions'] = [dict(type='mus'), dict(type='no_mus')]
        if phase == 'discard':
            q['legal_actions'] = [dict(type='discard', mask=1), dict(type='discard', mask=15)]
            q['discard_counts'] = [2,0] if player else [0,0]
        return q

    queries = [observation(phase, player) for player in [0,1]
               for phase in ['grande','chica','pares','juego','punto','mus','discard']]
    results = run(queries)
    for q, result in zip(queries, results):
        assert result['ok'] and result['diagnostic']['method'] == 'exact', (q, result)
        d = result['diagnostic']
        assert d['strategy'] == 'ordago' and not d['score_proxy'] and d['strategy_scores'] is None
        assert d['ordago_factor'] == 1
        kind = 'mus' if q['phase'] == 'mus' else 'discard' if q['phase'] == 'discard' else 'ordago'
        expected = .75 if kind == 'mus' else .25 if kind == 'discard' else .3
        assert abs(probability(result, kind, 1 if kind == 'discard' else None) - expected) < 1e-12
    for command in commands[1:]: assert run(queries, command) == results

    # Both payment components and pending wagers remain fixed in approximate lookup.
    absent = observation('pares')
    absent['match_scores'] = [10,13]
    changed_pending = observation('pares'); changed_pending['pending_wager_total'] = 3
    too_large = observation('pares'); too_large['pending_wager_total'] = 41
    for result in run([absent, changed_pending, too_large]):
        assert result['ok'] and result['diagnostic']['method'] == 'fallback'
        assert result['diagnostic']['sources'] == [] and not result['diagnostic']['score_proxy']

    offered = observation()
    offered['history'] = [dict(player=0,type='pass'),dict(player=1,type='raise',amount=3)]
    offered['legal_actions'] = [dict(type='reject'),dict(type='accept'),dict(type='ordago'),dict(type='raise',amount=4)]
    interpolated = run([offered])[0]
    assert interpolated['diagnostic']['method'] == 'interpolated'
    assert abs(probability(interpolated, 'accept') - .5) < 1e-12
    offered['history'][1]['amount'] = 2
    offered['history'] += [dict(player=0,type='raise',amount=2),dict(player=1,type='raise',amount=2)]
    nearest = run([offered])[0]
    assert nearest['diagnostic']['method'] == 'nearest'
    for command in commands[1:]: assert run([offered], command)[0] == nearest
    ordago = observation()
    ordago['history'] = [dict(player=0,type='pass'),dict(player=1,type='ordago')]
    ordago['legal_actions'] = [dict(type='reject'),dict(type='accept')]
    assert abs(probability(run([ordago])[0], 'accept') - .6) < 1e-12
    limited = observation('mus'); limited.update(mus_count=1, discard_counts=[2,3])
    assert run([limited])[0]['diagnostic']['method'] == 'mus_limit'

    invalid = []
    for field in ['hand_start_scores','pending_wager_total']:
        bad = observation(); del bad[field]; invalid.append(bad)
    for field, value in [('hand_start_scores',[11,12]), ('hand_start_scores',[-1,12]),
                         ('pending_wager_total',1), ('match_scores',[11,12])]:
        bad = observation(); bad[field] = value; invalid.append(bad)
    for command in commands:
        combined = run(invalid + [observation()], command)
        assert all(r.get('error') == 'invalid_observation' for r in combined[:-1]), combined
        assert combined[-1] == run([observation()], command)[0]  # Errors preserve RNG.

    scores = root/'scores'; scores.mkdir()
    subprocess.run([builder, str(root/'legacy.cfr'), str(scores)], check=True, stdout=subprocess.DEVNULL)
    hybrid = commands[2] + ['--score-table',str(scores/'values.txt'),'--score-strategies',str(scores)]
    switches = []
    for a,b in [(10,12),(30,30),(30,39),(10,12)]:
        q = observation(); q.update(hand_start_scores=[a,b], match_scores=[a,b]); switches.append(q)
    routed = run(switches, hybrid)
    assert [r['diagnostic']['strategy'] for r in routed] == ['ordago','ordago-score','ordago-score','ordago']
    assert all(abs(probability(r,'ordago')-p)<1e-12 for r,p in zip(routed,[.3,.25,.75,.3]))
    switches = [dict(q, phase='mus',legal_actions=[dict(type='mus'),dict(type='no_mus')]) for q in switches]
    assert [probability(r,'mus') for r in run(switches,hybrid)] == [.75,.25,.75,.75]

    # Public generic keeps the optional score bias; trained cells omit it.
    biased = observation(); biased.update(hand_start_scores=[38,10], match_scores=[38,10])
    for command in commands:
        result = run([biased], [arg for arg in command if arg != '--no-score-adjustment'])[0]
        assert result['diagnostic']['ordago_factor'] == .5
        assert abs(probability(result, 'ordago') - 3/17) < 1e-12
    trained = observation(); trained.update(hand_start_scores=[39,30], match_scores=[39,30])
    result = run([trained], [arg for arg in hybrid if arg != '--no-score-adjustment'])[0]
    assert result['diagnostic']['ordago_factor'] == 1 and probability(result, 'ordago') == .25

    text = paths[2].read_text()
    bad_path = root/'invalid.txt'
    for content in [text.splitlines(keepends=True)[0], text.replace('ordago-public/v1', 'ordago-public/v0', 1)]:
        bad_path.write_text(content)
        result = subprocess.run([bot,'--strategy',str(bad_path)],input='',text=True,capture_output=True)
        assert result.returncode == 1 and not result.stdout
    bad_path = root/'invalid.cfr'
    for content in [checkpoint.read_bytes()[:-4], checkpoint.read_bytes()+b'x']:
        bad_path.write_bytes(content)
        result = subprocess.run([bot,'--load',str(bad_path)],input='',text=True,capture_output=True)
        assert result.returncode == 1 and not result.stdout
    assert [hashlib.sha256(p.read_bytes()).hexdigest() for p in paths] == original
print('All public adapter CLI checks passed.')
