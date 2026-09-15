"""Exercise the deployed match CLI with real binary, TXT, and hybrid policies."""
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

solver = str(Path(sys.argv[1]).resolve())
evaluator = str(Path(sys.argv[2]).resolve())

def run(command, success=True):
    p = subprocess.run(command, capture_output=True, text=True, timeout=45)
    assert (p.returncode == 0) == success, (command, p.returncode, p.stdout, p.stderr)
    return p.stdout

def hashes(path):
    return {str(p.relative_to(path)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in path.rglob('*') if p.is_file()}

def normalized(output):
    result = json.loads(output)
    assert result['scope'] == 'deployed-adapter-matches'
    assert result['strategy_order'] == ['ordago-deprecated', 'ordago', 'ordago-score', 'baseline']
    assert sum(result['wins']) == result['matches']
    assert result['hands'] >= result['matches']
    assert result['seconds'] >= 0
    for player in [0, 1]:
        for dimension in ['phase', 'start-score', 'action']:
            assert sum(row['count'] for row in result['coverage']
                       if row['policy'] == player and row['dimension'] == dimension) == result['decisions'][player]
        assert sum(result['strategy_counts'][player]) == result['decisions'][player]
    result.pop('seconds')
    return result

with tempfile.TemporaryDirectory(prefix='mus-match-tests-') as tmp:
    root = Path(tmp); inputs = root/'inputs'; inputs.mkdir()
    for variant in ['ordago-deprecated', 'ordago']:
        raw = inputs/f'{variant}.cfr'; compressed = inputs/f'{variant}.cfr.zst'; text = inputs/f'{variant}.txt'
        common = [solver, '--format', variant, '--iterations', '3', '--seed', '7']
        run([*common, '--save', str(raw), '--export-strategy', str(text)])
        run([*common, '--save-compressed', str(compressed)])
        before = hashes(inputs)
        base = [evaluator, '--matches', '6', '--seed', '7', '--score', '35:35']
        raw_result = normalized(run([*base, '--load-a', str(raw)]))
        zstd_result = normalized(run([*base, '--load-compressed-a', str(compressed)]))
        assert raw_result == zstd_result
        for mode, path in [('--load-a', raw), ('--strategy-a', text)]:
            trace = root/f'{variant}-{path.suffix[1:]}.csv'
            command = [*base, mode, str(path), '--opponent', 'three']
            first = normalized(run([*command, '--trace', str(trace)]))
            assert first == normalized(run(command))
            rows = list(csv.DictReader(trace.open()))
            assert len(rows) == sum(first['decisions'])
            assert all(int(r['paid_mano']) >= int(r['start_mano']) and
                       int(r['paid_postre']) >= int(r['start_postre']) for r in rows)
            assert any(r['policy'] == '1' and r['action'] == 'raise' and r['amount'] == '3' for r in rows)
            for match in range(1, 7):
                first_row = next(r for r in rows if int(r['match']) == match)
                assert int(first_row['mano']) == (match - 1) % 2
            assert first['strategy_counts'][0][0 if variant == 'ordago-deprecated' else 1] > 0
        pair = normalized(run([*base, '--load-a', str(raw), '--load-b', str(raw), '--no-score-adjustment']))
        assert pair['policies'] == ['adapter', 'adapter']
        assert pair['strategy_counts'][0][3] == pair['strategy_counts'][1][3] == 0
        # The trace cannot truncate a policy or any other existing file.
        run([*base, '--load-a', str(raw), '--trace', str(raw)], False)
        assert hashes(inputs) == before

    # Actual hybrid selector: ready initial scores use the trained TXT;
    # excluded starts use the public generic for that whole hand.
    table = inputs/'scores'
    run([solver, '--format', 'ordago-score', '--score-region', '39:39', '--iterations', '2',
         '--evaluation-samples', '50', '--seed', '7', '--continuation', str(table),
         '--export-strategy', str(inputs/'score-temp.txt')])
    (inputs/'score-temp.txt').rename(table/'score-39-39.txt')
    before = hashes(inputs)
    hybrid = [evaluator, '--load-a', str(inputs/'ordago.cfr'), '--matches', '4', '--seed', '7',
              '--score-table-a', str(table/'values.txt'), '--score-strategies-a', str(table)]
    trained = normalized(run([*hybrid, '--score', '39:39']))
    assert trained['strategy_counts'][0][2] == trained['decisions'][0]
    untrained = normalized(run([*hybrid, '--score', '38:39']))
    assert untrained['strategy_counts'][0][1] == untrained['decisions'][0]
    pair = normalized(run([*hybrid, '--score', '39:39', '--load-b', str(inputs/'ordago.cfr'),
                           '--score-table-b', str(table/'values.txt'), '--score-strategies-b', str(table)]))
    assert pair['strategy_counts'][1][2] == pair['decisions'][1]
    assert hashes(inputs) == before

    # CLI counts follow the strict unsigned grammar. Invalid input and partial
    # match budgets must fail instead of reporting a biased win rate.
    base = [evaluator, '--load-a', str(inputs/'ordago.cfr'), '--matches', '1']
    for args in [['--matches', '2'], ['--score', '40:0'], ['--score', '-1:0'],
                 ['--seed', '-1'], ['--seed', '+1'], ['--seed', ' 1'], ['--seed', str(2**32)],
                 ['--max-hands', '0'], ['--max-hands', '-1'], ['--opponent', 'unknown'],
                 ['--score-table-a', str(table/'values.txt')],
                 ['--strategy-a', str(inputs/'ordago.txt')],
                 ['--load-b', str(inputs/'ordago.cfr'), '--opponent', 'three']]:
        run([*base, *args], False)
    # The passive opponent rejects every wager. From 0:0 no first hand can
    # reach forty through those dejes and intrinsic points.
    assert run([*base, '--max-hands', '1'], False) == ''
    foreign = inputs/'raises.cfr'
    run([solver, '--format', 'raises-to-eight', '--iterations', '1', '--save', str(foreign)])
    run([evaluator, '--load-a', str(foreign), '--matches', '1'], False)
    output = run([*base, '--score', '35:35'])
    normalized(output)
print('All deployed-match CLI checks passed.')
