"""Check simplified menus, persistence, score tables and bot backends."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

SOLVER = str(Path(sys.argv[1]).resolve())
BOT = str(Path(sys.argv[2]).resolve()) if len(sys.argv) > 2 else None


def run(variant, args, success=True):
    result = subprocess.run([SOLVER, '--format', variant, *map(str, args)],
                            capture_output=True, text=True, timeout=120)
    assert (result.returncode == 0) == success, (variant, args, result)
    if not success:
        assert result.returncode == 1 and 'incompatible game' in result.stderr, result
    return result.stdout


def check_export(path):
    lines = path.read_text().splitlines()
    assert 'simplified' in lines[0] and len(lines) > 1
    numeric_responses = 0
    for line in lines[1:]:
        fields = dict(token.split('=', 1) for token in line.split()[1:])
        raises = {int(key[8:]) for key in fields if key.startswith('envidar_')}
        assert raises <= {2, 4, 10}, line
        current = fields.get('actual', 'inicio')
        if current == 'ordago' or current == '40':
            assert not raises, line
        elif current.isdecimal() and int(current) > 0:
            covered = int(fields['cubierto'])
            increment = int(current) - (0 if covered == 1 else covered)
            assert all(amount > increment for amount in raises), line
            numeric_responses += 1
    return numeric_responses


def artifacts(path):
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in path.iterdir()
            if p.is_file() and p.name not in ('training.lock', 'recovery.txt')}


def score_args(path, extra=(), score='1:1', target=3):
    return ['--target', target, '--score', score, '--continuation', path,
            '--evaluation-samples', 20, *extra]


with tempfile.TemporaryDirectory(prefix='mus-simplified-') as temporary:
    root = Path(temporary)
    for base in ('ordago', 'ordago-four'):
        variant = base + '-simplified'
        first, resumed, whole = [root / (variant + name) for name in ('.cfr', '-resumed.cfr', '-whole.cfr')]
        compressed, exported = root / (variant + '.zst'), root / (variant + '.txt')
        run(variant, ['--iterations', 2, '--seed', 7, '--save', first,
                      '--export-strategy', exported])
        run(variant, ['--iterations', 2, '--seed', 7, '--save-compressed', compressed])
        assert check_export(exported) > 0
        run(variant, ['--load', first, '--iterations', 1, '--save', resumed])
        run(variant, ['--iterations', 3, '--seed', 7, '--save', whole])
        assert resumed.read_bytes() == whole.read_bytes()
        other_export = root / (variant + '-compressed.txt')
        run(variant, ['--load-compressed', compressed, '--export-strategy', other_export])
        assert exported.read_bytes() == other_export.read_bytes()
        evaluation = run(variant, ['--load', first, '--evaluate',
                                   *(['--evaluation-samples', 20] if base == 'ordago-four' else [])])
        assert 'evaluation ' in evaluation
        parallel_export = root / (variant + '-parallel.txt')
        run(variant, ['--iterations', 2, '--threads', 2, '--seed', 7,
                      '--export-strategy', parallel_export])
        check_export(parallel_export)
        run(base, ['--load', first, '--evaluate'], False)
        standard = root / (base + '.cfr')
        run(base, ['--iterations', 1, '--seed', 7, '--save', standard])
        run(variant, ['--load', standard, '--evaluate'], False)

    for base in ('ordago-score', 'ordago-score-four'):
        variant = base + '-simplified'
        scores = root / variant
        train = ['--iterations', 2, '--seed', 7]
        output = run(variant, score_args(scores, train))
        assert output.count('score-cell ') == 6, output
        lines = (scores / 'values.txt').read_text().splitlines()
        assert lines[0].startswith(f'mus.{variant}/v1 '), lines[0]
        assert 'simplified/v1-muses-' in lines[0]
        assert len(lines) == 7
        before = artifacts(scores)
        assert 'score-cell ' not in run(variant, score_args(scores, train))
        assert 'evaluation ' in run(variant, score_args(scores, ['--evaluate']))
        assert artifacts(scores) == before
        exported = root / (variant + '.txt')
        source = scores / 'score-01-01.cfr.zst'
        run(variant, score_args(scores, ['--load-compressed', source, '--export-strategy', exported]))
        check_export(exported)
        first, resumed, whole = [root / (variant + name) for name in ('.cfr', '-resumed.cfr', '-whole.cfr')]
        run(variant, score_args(scores, ['--load-compressed', source, '--iterations', 1, '--save', first]))
        run(variant, score_args(scores, ['--load', first, '--iterations', 1, '--save', resumed]))
        run(variant, score_args(scores, ['--load-compressed', source, '--iterations', 2, '--save', whole]))
        assert resumed.read_bytes() == whole.read_bytes()
        parallel = root / (variant + '-parallel')
        run(variant, score_args(parallel, [*train, '--threads', 3]))
        assert artifacts(parallel) == before
        region = root / (variant + '-region')
        run(variant, score_args(region, [*train, '--score-region', '1:1']))
        region_lines = (region / 'values.txt').read_text().splitlines()
        assert f'mus.{variant}-region/v1' in region_lines[0] and len(region_lines) == 5
        extension = root / (variant + '-extension')
        run(variant, score_args(extension, ['--iterations', 2, '--extend-continuation', region, '--score-region', '1:1']))
        assert all(line.split()[3] == '4' for line in (extension / 'values.txt').read_text().splitlines()[1:])
        standard = root / base
        run(base, score_args(standard, train))
        standard_before = artifacts(standard)
        run(base, score_args(scores, ['--evaluate']), False)
        run(variant, score_args(standard, ['--evaluate']), False)
        run(variant, score_args(scores, ['--load-compressed', standard / 'score-01-01.cfr.zst', '--evaluate']), False)
        run(base, score_args(standard, ['--load-compressed', source, '--evaluate']), False)
        run(variant, score_args(root / (variant + '-wrong-extension'), ['--iterations', 2, '--extend-continuation', standard]), False)
        assert artifacts(standard) == standard_before and artifacts(scores) == before

    # Test both transfer directions. Exact filtering is checked by the C fixtures.
    for prior, target in (('ordago', 'ordago-score-simplified'),
                          ('ordago-simplified', 'ordago-score-simplified'),
                          ('ordago-simplified', 'ordago-score')):
        output = run(target, score_args(root / ('prior-' + prior + '-' + target),
            ['--iterations', 2, '--seed', 7, '--preload-ordago', root / (prior + '.cfr')], score='2:2'))
        report = next(line for line in output.splitlines() if line.startswith('score-preload '))
        fields = dict(token.split('=', 1) for token in report.split()[1:])
        assert fields['mode'] == 'lazy-all-lances'
        assert int(fields['source_nodes']) > 0
        assert int(fields['expanded_action_nodes']) <= int(fields['imported_nodes'])
        if target.endswith('-simplified'):
            assert fields['expanded_action_nodes'] == '0'

    if BOT:
        query = dict(player=0, phase='grande', hand=[0]*7+[4],
                     hand_start_scores=[10, 12], match_scores=[10, 12],
                     pending_wager_total=0, previous_strengths=[], hand_scores_complete=False,
                     legal_actions=[dict(type='pass'), dict(type='ordago'),
                                    *[dict(type='raise', amount=n) for n in (2, 4, 10)]], history=[])

        def bot(flag, path, observation=query, extra=()):
            result = subprocess.run([BOT, flag, str(path), '--seed', '7', '--no-score-adjustment',
                                     *map(str, extra)], input=json.dumps(observation) + '\n',
                                    capture_output=True, text=True, timeout=120)
            assert result.returncode == 0 and not result.stderr, result
            decision = json.loads(result.stdout)
            assert decision['ok'], decision
            assert abs(sum(choice['probability'] for choice in decision['probabilities']) - 1) < 1e-12
            assert decision['action'] in observation['legal_actions'], decision
            assert all({key: value for key, value in choice.items() if key != 'probability'}
                       in observation['legal_actions'] for choice in decision['probabilities']), decision
            return decision

        decisions = [bot(flag, root / ('ordago-simplified' + suffix))
                     for flag, suffix in (('--load', '.cfr'), ('--load-compressed', '.zst'), ('--strategy', '.txt'))]
        assert decisions[0] == decisions[1] == decisions[2]
        assert decisions[0]['diagnostic']['strategy'] == 'ordago-simplified'

        def known_opening(export, scores, checkpoint=None):
            for row in export.read_text().splitlines()[1:]:
                if ' fase=grande muses=0 ' not in row or ' actual=inicio ' not in row:
                    continue
                fields = dict(token.split('=', 1) for token in row.split()[1:])
                cards = dict(token.split(':') for token in fields['cartas'].split(','))
                hand = [0] * 8
                hand[7] = int(cards['reyes'])
                if hand[7] != 4:
                    classes = ['pitos', 'cuatros', 'cincos', 'seises', 'sietes', 'sotas', 'caballos']
                    hand[classes.index(cards['siguiente'])] = 4 - hand[7]
                legal = [dict(type='pass'), dict(type='ordago'),
                         *[dict(type='raise', amount=n) for n in (2, 4, 10) if f'envidar_{n}' in fields]]
                observed = dict(query, hand=hand, hand_start_scores=scores, match_scores=scores,
                                mus_stop_player=int(fields['corte'] == 'postre'), legal_actions=legal)
                # Checkpoints skip nodes with zero average mass; TXT exports
                # represent those nodes as uniform. Compare a trained node.
                if checkpoint and bot('--load', checkpoint, observed)['diagnostic']['method'] != 'exact':
                    continue
                return observed, fields
            raise AssertionError('No trained opening found')

        exact_query, fields = known_opening(root / 'ordago-simplified.txt', [10, 12], root / 'ordago-simplified.cfr')
        exact = [bot(flag, root / ('ordago-simplified' + suffix), exact_query)
                 for flag, suffix in (('--load', '.cfr'), ('--load-compressed', '.zst'), ('--strategy', '.txt'))]
        assert exact[0] == exact[1] == exact[2], exact
        assert exact[0]['diagnostic']['method'] == 'exact'
        for choice in exact[0]['probabilities']:
            name = {'pass': 'pasar', 'ordago': 'ordago'}.get(choice['type'], f"envidar_{choice.get('amount')}")
            assert abs(choice['probability'] - float(fields[name])) < 1e-12
        scores = root / 'bot-score'
        strategies = root / 'bot-strategies'
        strategies.mkdir()
        run('ordago-score-simplified', score_args(scores, ['--iterations', 2, '--seed', 7], '39:39', 40))
        run('ordago-score-simplified', score_args(scores,
            ['--load-compressed', scores / 'score-39-39.cfr.zst',
             '--export-strategy', strategies / 'score-39-39.txt'], '39:39', 40))
        trained, _ = known_opening(strategies / 'score-39-39.txt', [39, 39])
        result = bot('--strategy', root / 'ordago-simplified.txt', trained,
                     ['--score-table', scores / 'values.txt', '--score-strategies', strategies])
        assert result['diagnostic']['strategy'] == 'ordago-score-simplified'
        assert result['diagnostic']['method'] == 'exact'

        # Hybrid policies can use different training abstractions. The real
        # engine's legal actions remain authoritative in both routing directions.
        original_export = root / 'ordago.txt'
        run('ordago', ['--load', root / 'ordago.cfr', '--export-strategy', original_export])
        hybrid = ['--score-table', scores / 'values.txt', '--score-strategies', strategies]
        result = bot('--strategy', original_export, trained, hybrid)
        assert result['diagnostic']['strategy'] == 'ordago-score-simplified'
        assert result['diagnostic']['method'] == 'exact'
        result = bot('--strategy', original_export, query, hybrid)
        assert result['diagnostic']['strategy'] == 'ordago'

        original_scores = root / 'bot-original-score'
        original_strategies = root / 'bot-original-strategies'
        original_strategies.mkdir()
        run('ordago-score', score_args(original_scores, ['--iterations', 2, '--seed', 7], '39:39', 40))
        original_cell = original_strategies / 'score-39-39.txt'
        run('ordago-score', score_args(original_scores,
            ['--load-compressed', original_scores / 'score-39-39.cfr.zst',
             '--export-strategy', original_cell], '39:39', 40))
        original_query, _ = known_opening(original_cell, [39, 39])
        hybrid = ['--score-table', original_scores / 'values.txt', '--score-strategies', original_strategies]
        result = bot('--strategy', root / 'ordago-simplified.txt', original_query, hybrid)
        assert result['diagnostic']['strategy'] == 'ordago-score'
        assert result['diagnostic']['method'] == 'exact'
        result = bot('--strategy', root / 'ordago-simplified.txt', exact_query, hybrid)
        assert result['diagnostic']['strategy'] == 'ordago-simplified'
        assert result['diagnostic']['method'] == 'exact'

print('All simplified CLI checks passed.')
