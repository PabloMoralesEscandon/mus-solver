"""Check single-hand partnership training, evaluation, and checkpoint isolation."""
import math
from pathlib import Path
import subprocess
import sys
import tempfile

SOLVER = str(Path(sys.argv[1]).resolve())
SCHEMA = 'mus.four-partnership-single-hand.ordago/v1-muses-'


def run(args, success=True, format='ordago-four'):
    result = subprocess.run([SOLVER, '--format', format, *map(str, args)],
                            capture_output=True, text=True, timeout=120)
    assert (result.returncode == 0) == success, (
        args, result.returncode, result.stdout, result.stderr)
    return result


def evaluation(output, samples):
    line = next(line for line in output.splitlines() if line.startswith('evaluation '))
    fields = dict(token.split('=', 1) for token in line.split()[1:])
    assert fields['scope'] == 'single-hand-sampled' and fields['utility'] == 'points'
    assert fields['players'] == '4' and fields['teams'] == '0+2:1+3'
    assert int(fields['samples']) == samples
    values = [float(fields[f'profile_value_player_{p}']) for p in range(4)]
    assert all(math.isfinite(v) and -40 <= v <= 40 for v in values)
    assert values[0] == values[2] == -values[1] == -values[3]
    assert 'exploitability' not in fields and 'win_probability' not in fields
    assert fields['variance_known'] == ('1' if samples >= 2 else '0')
    variance = float(fields['sample_variance'])
    assert math.isfinite(variance) and variance >= 0
    assert math.isclose(float(fields['standard_error']), math.sqrt(variance / samples))
    return fields


with tempfile.TemporaryDirectory(prefix='mus-four-single-') as temporary:
    root = Path(temporary)
    first, resumed, whole = (root / name for name in ('first.cfr', 'resumed.cfr', 'whole.cfr'))
    compressed, exported = root / 'four.cfr.zst', root / 'four.txt'
    assert 'ordago-four' in run(['--help']).stdout
    output = run(['--iterations', '2', '--seed', '7', '--save', first,
                  '--export-strategy', exported]).stdout
    assert 'iterations=2 ' in output and 'players=4 teams=0+2:1+3' in output
    lines = exported.read_text().splitlines()
    assert SCHEMA in lines[0] and 'training_iterations=2 ' in lines[0]
    assert len(lines) > 1 and any('ultima_respuesta=ninguna' not in line for line in lines[1:])
    for line in lines[1:]:
        fields = dict(token.split('=', 1) for token in line.split()[1:])
        assert int(fields['equipo']) == int(fields['jugador']) % 2
        assert int(fields['jugador']) in range(4)

    run(['--load', first, '--iterations', '2', '--save', resumed])
    run(['--iterations', '4', '--seed', '7', '--save', whole])
    assert resumed.read_bytes() == whole.read_bytes()
    run(['--iterations', '4', '--seed', '7', '--save-compressed', compressed])
    exported_plain, exported_compressed = root / 'plain.txt', root / 'compressed.txt'
    run(['--load', whole, '--export-strategy', exported_plain])
    run(['--load-compressed', compressed, '--export-strategy', exported_compressed])
    assert exported_plain.read_bytes() == exported_compressed.read_bytes()

    # Evaluation reads the average strategy without changing the checkpoint.
    before = compressed.read_bytes()
    args = ['--load-compressed', compressed, '--evaluate', '--evaluation-samples', '100']
    output = run(args).stdout
    evaluation(output, 100)
    assert run(args).stdout == output
    assert compressed.read_bytes() == before
    evaluation(run(args + ['--evaluation-min-samples', '4',
                           '--evaluation-target-se', '100']).stdout, 4)
    evaluation(run(['--evaluate', '--evaluation-samples', '1']).stdout, 1)
    parallel = run(['--iterations', '4', '--seed', '7', '--threads', '2',
                    '--evaluate', '--evaluation-samples', '20']).stdout
    assert 'iterations=4 threads=2 ' in parallel
    evaluation(parallel, 20)

    # A points policy cannot load as a match-value policy or a heads-up policy.
    result = run(['--load', whole, '--export-strategy', root / 'wrong.txt'],
                 False, format='ordago')
    assert 'incompatible game' in result.stderr
    heads_up = root / 'heads-up.cfr'
    run(['--iterations', '1', '--seed', '7', '--save', heads_up], format='ordago')
    result = run(['--load', heads_up, '--export-strategy', root / 'wrong.txt'], False)
    assert 'incompatible game' in result.stderr
    scores = root / 'scores'
    run(['--target', '1', '--score', '0:0', '--continuation', scores,
         '--iterations', '1', '--evaluation-samples', '2'], format='ordago-score-four')
    score_checkpoint = scores / 'score-00-00.cfr.zst'
    result = run(['--load-compressed', score_checkpoint, '--export-strategy', root / 'wrong.txt'], False)
    assert 'incompatible game' in result.stderr
    result = run(['--target', '1', '--score', '0:0', '--continuation', scores,
                  '--load', whole, '--export-strategy', root / 'wrong.txt'],
                 False, format='ordago-score-four')
    assert 'incompatible game' in result.stderr

    for options in (['--score', '0:0'], ['--target', '40'], ['--continuation', scores],
                    ['--extend-continuation', scores], ['--score-region', '0:0'],
                    ['--threads-per-cell', '2'], ['--preload-ordago', heads_up],
                    ['--preload-weight', '1'], ['--evaluation-samples', '2']):
        run(['--iterations', '1', *options], False)

print('All four-player single-hand CLI checks passed.')
