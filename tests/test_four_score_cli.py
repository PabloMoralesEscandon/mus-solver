"""Check four-seat score training, persistence, and isolation from old formats."""
import hashlib
import math
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

SOLVER = str(Path(sys.argv[1]).resolve())
BOT = str(Path(sys.argv[2]).resolve()) if len(sys.argv) > 2 else None
FORMAT = 'ordago-score-four'


def run(args, success=True):
    result = subprocess.run([SOLVER, *map(str, args)], capture_output=True,
                            text=True, timeout=120)
    assert (result.returncode == 0) == success, (
        args, result.returncode, result.stdout, result.stderr)
    return result.stdout


def train(path, extra=(), score='1:1', variant=FORMAT):
    return ['--format', variant, '--target', '3', '--score', score,
            '--iterations', '2', '--evaluation-samples', '20',
            '--continuation', path, *extra]


def read(path, extra=(), score='1:1', variant=FORMAT):
    return ['--format', variant, '--target', '3', '--score', score,
            '--continuation', path, *extra]


def rows(path):
    return {tuple(map(int, fields[:2])): fields[2:]
            for line in (path / 'values.txt').read_text().splitlines()[1:]
            if (fields := line.split())}


def artifacts(path):
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in path.iterdir()
            if p.is_file() and p.name not in ('training.lock', 'recovery.txt')}


with tempfile.TemporaryDirectory(prefix='mus-four-cli-') as temporary:
    root = Path(temporary)
    scores = root / 'scores'
    output = run(train(scores, ['--seed', '7', '--report-every', '1']))
    assert output.count('score-cell ') == 6, output
    assert 'players=4 teams=0+2:1+3' in output, output
    assert all('players=4 teams=0+2:1+3' in line for line in output.splitlines()
               if line.startswith(('score-report ', 'score-table ')))
    header = (scores / 'values.txt').read_text().splitlines()[0]
    assert 'four' in header and 'statistics=sample-variance-v1' in header, header
    assert len(rows(scores)) == 6
    for row in rows(scores).values():
        assert -1 <= float(row[0]) <= 1
        assert int(row[1]) == 2 and int(row[2]) == 20
        assert row[4] == '1' and math.isfinite(float(row[5]))

    before = artifacts(scores)
    assert 'score-cell ' not in run(train(scores, ['--seed', '7']))
    assert artifacts(scores) == before
    output = run(read(scores, ['--evaluate', '--evaluation-samples', '20']))
    assert 'evaluation ' in output and artifacts(scores) == before
    evaluation = next(line for line in output.splitlines() if line.startswith('evaluation '))
    fields = dict(token.split('=', 1) for token in evaluation.split()[1:])
    utilities = [float(fields[f'profile_value_player_{seat}']) for seat in range(4)]
    assert utilities[0] == utilities[2] == -utilities[1] == -utilities[3], evaluation
    output = run(read(scores, ['--evaluate', '--evaluation-samples', '20',
                              '--evaluation-min-samples', '4',
                              '--evaluation-target-se', '2']))
    assert 'samples=4 ' in output and artifacts(scores) == before, output

    # Independent cell workers retain deterministic sequential cell streams.
    parallel = root / 'parallel'
    run(train(parallel, ['--seed', '7', '--threads', '3']))
    assert artifacts(parallel) == before
    shared = root / 'shared'
    run(train(shared, ['--seed', '7', '--threads', '3', '--threads-per-cell', '2']))
    assert all(int(row[1]) == 2 for row in rows(shared).values())

    # Checkpoint continuation must reproduce an uninterrupted single thread.
    source = scores / 'score-01-01.cfr.zst'
    first, resumed, whole = (root / name for name in ('first.cfr', 'resumed.cfr', 'whole.cfr'))
    run(read(scores, ['--load-compressed', source, '--iterations', '1', '--save', first]))
    run(read(scores, ['--load', first, '--iterations', '1', '--save', resumed]))
    run(read(scores, ['--load-compressed', source, '--iterations', '2', '--save', whole]))
    assert resumed.read_bytes() == whole.read_bytes()
    exported = root / 'strategy.txt'
    run(read(scores, ['--load', resumed, '--export-strategy', exported]))
    assert 'four' in exported.read_text().splitlines()[0]
    assert len(exported.read_text().splitlines()) > 1

    # Regions and extensions retain the same two TEAM score dimensions.
    region = root / 'region'
    run(train(region, ['--score-region', '1:1', '--seed', '7']))
    assert set(rows(region)) == {(1, 1), (1, 2), (2, 1), (2, 2)}
    assert rows(region) == {key: value for key, value in rows(scores).items()
                           if key in rows(region)}
    run(read(region, ['--evaluate'], score='0:2'), False)
    extension = root / 'extension'
    run(train(extension, ['--score-region', '1:1', '--extend-continuation', region]))
    assert all(int(row[1]) == 4 for row in rows(extension).values())
    run(train(extension, ['--extend-continuation', region]))

    # Recover a saved cell after a later cell prevents diagonal publication.
    recovery = root / 'recovery'
    run(train(recovery, ['--seed', '7'], score='2:2'))
    obstacle = recovery / 'score-02-01.cfr.zst'
    obstacle.mkdir()
    run(train(recovery, ['--seed', '7']), False)
    saved = recovery / 'score-01-02.cfr.zst'
    stamp = (saved.read_bytes(), saved.stat().st_mtime_ns)
    obstacle.rmdir()
    output = run(train(recovery, ['--seed', '7']))
    assert 'score-recovered score=1:2 ' in output, output
    assert stamp == (saved.read_bytes(), saved.stat().st_mtime_ns)
    assert rows(recovery) == rows(scores)

    # No table, checkpoint, extension, or prior can cross the format boundary.
    old = root / 'old'
    run(train(old, ['--seed', '7'], variant='ordago-score'))
    old_before = artifacts(old)
    orphan = root / 'orphan'
    orphan.mkdir()
    shutil.copy(old / 'score-02-02.cfr.zst', orphan / 'score-02-02.cfr.zst')
    orphan_before = artifacts(orphan)
    run(train(orphan, ['--seed', '7'], score='2:2'), False)
    assert artifacts(orphan) == orphan_before
    run(read(old, ['--evaluate']), False)
    run(read(scores, ['--evaluate'], variant='ordago-score'), False)
    run(read(scores, ['--load-compressed', old / 'score-01-01.cfr.zst',
                     '--evaluate']), False)
    run(read(old, ['--load-compressed', source, '--evaluate'], variant='ordago-score'), False)
    run(train(root / 'wrong-extension', ['--extend-continuation', old]), False)
    generic = root / 'generic.cfr'
    run(['--format', 'ordago-public', '--iterations', '1', '--seed', '7', '--save', generic])
    run(train(root / 'wrong-prior', ['--preload-ordago', generic]), False)
    assert artifacts(old) == old_before and artifacts(scores) == before

    # The default target remains 40. The two-player bot rejects this table.
    top = root / 'target40'
    run(['--format', FORMAT, '--score', '39:39', '--iterations', '1',
         '--evaluation-samples', '10', '--continuation', top])
    assert set(rows(top)) == {(39, 39)}
    if BOT:
        result = subprocess.run([BOT, '--load', str(generic), '--score-table',
                                 str(top / 'values.txt'), '--score-strategies', str(top)],
                                input='', capture_output=True, text=True, timeout=30)
        assert result.returncode != 0, result.stdout

print('All four-player score CLI checks passed.')
