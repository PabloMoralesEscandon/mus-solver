"""Verify local sampling statistics, adaptive stopping, and saved-value identity."""
import hashlib
import math
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

solver = str(Path(sys.argv[1]).resolve())

def run(args, success=True):
    p = subprocess.run([solver, *args], capture_output=True, text=True, timeout=30)
    assert (p.returncode == 0) == success, (args, p.returncode, p.stdout, p.stderr)
    return p.stdout

def train(path, extra=(), score='0:0', samples=100):
    return ['--format', 'ordago-score', '--target', '3', '--score', score,
            '--iterations', '2', '--evaluation-samples', str(samples),
            '--continuation', str(path), *extra]

def evaluate(path, extra=()):
    return ['--format', 'ordago-score', '--target', '3', '--score', '0:0',
            '--continuation', str(path), '--evaluate', '--evaluation-samples', '100', *extra]

def rows(path):
    return {tuple(map(int, s[:2])): s[2:] for line in (path/'values.txt').read_text().splitlines()[1:]
            if (s := line.split())}

def hashes(path):
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in path.iterdir() if p.is_file()}

def fields(line):
    return dict(token.split('=', 1) for token in line.split()[1:])

adaptive = ['--evaluation-min-samples', '10', '--evaluation-target-se', '0.5']
with tempfile.TemporaryDirectory(prefix='mus-statistics-') as tmp:
    root = Path(tmp)
    # The cap reflects pending cells in a diagonal, including score and region.
    for label, score, extra, limit in [
        ('wide', '0:0', [], 3), ('top', '2:2', [], 1),
        ('region', '1:1', ['--score-region', '1:1'], 2),
    ]:
        path = root/('workers-' + label)
        output = run(train(path, ['--threads', '100', *extra], score=score, samples=2))
        workers = fields(next(line for line in output.splitlines() if line.startswith('score-workers ')))
        assert workers == {'requested': '100', 'cell_limit': str(limit), 'threads_per_cell': '1'}
        output = run(train(path, ['--threads', '100', *extra], score=score, samples=2))
        assert 'cell_limit=0 ' in output
    fixed = root/'fixed'
    output = run(train(fixed, ['--seed', '7']))
    assert (fixed/'values.txt').read_text().splitlines()[0].endswith('statistics=sample-variance-v1')
    for line in output.splitlines():
        if not line.startswith('score-cell '):
            continue
        data = fields(line)
        n = int(data['evaluation_samples'])
        variance = float(data['sample_variance'])
        error = float(data['standard_error'])
        assert n == 100 and 0 <= variance <= 2
        assert math.isclose(error, math.sqrt(variance/n), abs_tol=1e-15)
        assert float(data['win_probability_se']) == error/2
    assert all(len(row) == 6 and row[4] == '1' for row in rows(fixed).values())

    early = root/'early'
    output = run(train(early, [*adaptive, '--seed', '7']))
    assert output.count('stop=estimated-target') == 9
    assert all(row[2] == '10' and row[4] == '1' for row in rows(early).values())
    before = hashes(early)
    assert 'score-cell ' not in run(train(early, [*adaptive, '--seed', '7']))
    after = hashes(early)
    assert {k:v for k,v in before.items() if k != 'training.lock'} == {k:v for k,v in after.items() if k != 'training.lock'}
    # A target too small to reach consumes the full budget, with the same
    # policies, means and variances as fixed sampling.
    capped = root/'capped'
    strict = ['--evaluation-min-samples', '32', '--evaluation-target-se', '1e-12', '--seed', '7']
    output = run(train(capped, strict))
    assert 'stop=sample-limit' in output
    assert rows(capped) == rows(fixed)
    assert {k:v for k,v in hashes(capped).items() if k.endswith('.cfr.zst')} == {
        k:v for k,v in hashes(fixed).items() if k.endswith('.cfr.zst')}

    # Legacy variance is unknown. A fresh evaluation reports its own variance
    # without changing the stored table or its compatible checkpoint schemas.
    legacy = root/'legacy'; shutil.copytree(fixed, legacy)
    lines = (legacy/'values.txt').read_text().splitlines()
    lines[0] = lines[0].replace(' statistics=sample-variance-v1', '')
    (legacy/'values.txt').write_text(lines[0]+'\n'+'\n'.join(' '.join(line.split()[:6]) for line in lines[1:])+'\n')
    before = hashes(legacy)
    output = run(evaluate(legacy, adaptive))
    stored = next(line for line in output.splitlines() if line.startswith('score-table '))
    fresh = next(line for line in output.splitlines() if line.startswith('evaluation '))
    assert 'standard_error=unknown' in stored
    assert 'standard_error=unknown' not in fresh and 'samples=10 ' in fresh
    assert hashes(legacy) == before
    single = root/'single'
    assert 'standard_error=unknown' in run(train(single, samples=1))

    # Evaluating a standalone compatible checkpoint obeys the same limits.
    output = run(evaluate(fixed, [*adaptive, '--load-compressed', str(fixed/'score-00-00.cfr.zst')]))
    assert 'scope=score-checkpoint-average-policy' in output and 'samples=10 ' in output

    for extra in [['--evaluation-min-samples', '10'], ['--evaluation-target-se', '0.1'],
                  ['--evaluation-min-samples', '1', '--evaluation-target-se', '0.1'],
                  ['--evaluation-min-samples', '101', '--evaluation-target-se', '0.1'],
                  [*adaptive, '--evaluation-target-se', '0.2']]:
        run(train(root/'bad', extra), False)
    for value in ['0', '-1', 'nan', 'inf', 'abc', '1e9999']:
        run(train(root/'bad', ['--evaluation-min-samples', '10', '--evaluation-target-se', value]), False)
    run(['--format', 'ordago-public', '--iterations', '1', *adaptive], False)

    # The manifest retains adaptive settings even if two settings happen to
    # stop at the same count. Extension sources are never rewritten.
    extended = root/'extended'; before = hashes(fixed)
    extension = ['--extend-continuation', str(fixed), *adaptive]
    run(train(extended, extension))
    assert hashes(fixed) == before
    assert all(row[1] == '4' and row[2] == '10' for row in rows(extended).values())
    run(train(extended, extension))
    run(train(extended, ['--extend-continuation', str(fixed), '--evaluation-min-samples', '10',
                         '--evaluation-target-se', '0.4']), False)

    # Journal statistics survive a failure; changing the stopping setting
    # invalidates recovery even when the new run also uses ten samples.
    top = root/'top'; run(train(top, ['--seed', '7'], score='2:2'))
    for label, target, recovered in [('same', '0.5', True), ('changed', '0.4', False)]:
        path = root/label; shutil.copytree(top, path)
        obstacle = path/'score-02-01.cfr.zst'; obstacle.mkdir()
        run(train(path, [*adaptive, '--seed', '7']), False)
        checkpoint = path/'score-01-02.cfr.zst'
        before = (checkpoint.read_bytes(), checkpoint.stat().st_mtime_ns)
        obstacle.rmdir()
        output = run(train(path, ['--evaluation-min-samples', '10', '--evaluation-target-se', target, '--seed', '7']))
        assert ('score-recovered score=1:2 ' in output) == recovered
        if recovered:
            assert (checkpoint.read_bytes(), checkpoint.stat().st_mtime_ns) == before
        assert rows(path)[(1,2)][4] == '1'

    # New rows require both statistics fields, strict availability, finite
    # nonnegative variance, and two samples when the variance is known.
    original = (fixed/'values.txt').read_text()
    def change_row(column, value):
        lines = original.splitlines(); parts = lines[1].split(); parts[column] = value
        lines[1] = ' '.join(parts); return '\n'.join(lines)+'\n'
    malformed = [original.replace('sample-variance-v1', 'sample-variance-v9'),
                 change_row(6, '2'), change_row(6, '0'), change_row(7, '-1'),
                 change_row(7, 'nan'), change_row(7, '3'), change_row(4, '1'),
                 original.replace(' statistics=sample-variance-v1', '')]
    lines = original.splitlines(); lines[1] = ' '.join(lines[1].split()[:6]); malformed.append('\n'.join(lines)+'\n')
    for i, content in enumerate(malformed):
        path = root/f'malformed-{i}'; shutil.copytree(fixed, path)
        (path/'values.txt').write_text(content)
        run(evaluate(path), False)
print('All score statistics checks passed.')
