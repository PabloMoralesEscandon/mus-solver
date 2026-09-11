"""Check persisted closed score regions, continuation, recovery, and bot routing."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

solver=str(Path(sys.argv[1]).resolve())
bot=str(Path(sys.argv[2]).resolve()) if len(sys.argv)>2 else None

def run(args,success=True):
    p=subprocess.run([solver,*args],capture_output=True,text=True,timeout=30)
    assert (p.returncode==0)==success,(args,p.returncode,p.stdout,p.stderr)
    return p.stdout

def train(path,region=None,score=None,extension=None,target=3):
    args=['--format','ordago-score','--target',str(target),'--iterations','2',
          '--evaluation-samples','50','--continuation',str(path)]
    args+=['--seed','7'] if extension is None else ['--extend-continuation',str(extension)]
    if region is not None:args+=['--score-region',region]
    if score is not None:args+=['--score',score]
    return args

def read(path,score=None,extra=(),target=3):
    args=['--format','ordago-score','--target',str(target),'--continuation',str(path),
          '--evaluation-samples','2',*extra]
    if score is not None:args+=['--score',score]
    return args

def rows(path):
    return {tuple(map(int,line.split()[:2])):line.split()[2:]
            for line in (path/'values.txt').read_text().splitlines()[1:]}

def hashes(path):return {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in path.iterdir() if p.is_file()}

with tempfile.TemporaryDirectory(prefix='mus-region-tests-') as tmp:
    root=Path(tmp);full=root/'full';region=root/'region'
    run(train(full,score='0:0'))
    output=run(train(region,region='1:1'))
    assert output.count('score-cell ')==4
    expected={(1,1),(1,2),(2,1),(2,2)}
    assert set(rows(region))==expected
    assert rows(region)=={key:value for key,value in rows(full).items() if key in expected}
    assert (full/'values.txt').read_text().startswith('mus.ordago-score/v4 ')
    assert (region/'values.txt').read_text().startswith('mus.ordago-score-region/v1 ')
    # Persisted limits are inferred when omitted; values and checkpoints stay intact.
    before=hashes(region);before.pop('training.lock');before.pop('recovery.txt')
    assert 'score-cell ' not in run(train(region))
    after=hashes(region);after.pop('training.lock');after.pop('recovery.txt');assert before==after
    assert 'score-table score=1:1 ' in run(read(region,extra=('--evaluate',)))
    run(read(region,'0:2',('--evaluate',)),False)
    run(train(region,region='0:2'),False)
    run(train(full,region='0:0'),False)
    run(['--format','ordago-public','--iterations','1','--score-region','1:1'],False)
    for bad in ['-1:0','40:0','1:3','1:1:1',' 1:1','1:-1']:
        run(train(root/'invalid',region=bad),False)

    # Both orientations share one canonical region and their declared coverage.
    asymmetric=root/'asymmetric';run(train(asymmetric,region='2:1'))
    assert set(rows(asymmetric))=={(1,2),(2,1),(2,2)}
    assert ' region=1:2' in (asymmetric/'values.txt').read_text().splitlines()[0]
    run(train(asymmetric,region='1:2'))

    # Bounds survive a failure before even the first checkpoint is published.
    first=root/'first-cell-failure';first.mkdir()
    obstacle=first/'score-02-02.cfr.zst';obstacle.mkdir()
    run(train(first,region='2:1'),False)
    assert not rows(first)
    assert (first/'values.txt').read_text().splitlines()[0].endswith('region=1:2')
    obstacle.rmdir()
    assert 'score-table score=1:2 ' in run(train(first))
    assert rows(first)==rows(asymmetric)

    # A restricted source can extend a covered destination, including a smaller
    # region. An uncovered source cannot supply missing destination checkpoints.
    for label,source,bounds in [('full-to-region',full,'1:1'),('region-to-region',region,'1:1'),
                                 ('smaller',region,'2:2')]:
        destination=root/label;before=hashes(source)
        run(train(destination,region=bounds,extension=source))
        assert hashes(source)==before
        assert all(int(value[1])==4 for value in rows(destination).values())
        run(train(destination,extension=source))
    run(train(root/'too-large',score='1:1',extension=region),False)

    # Individual checkpoint loading needs only higher values in the region.
    exported=root/'individual.txt'
    run(read(region,'1:1',('--load-compressed',str(region/'score-01-01.cfr.zst'),
                          '--export-strategy',str(exported))))
    assert 'mus.ordago-score-region/v1' in exported.read_text().splitlines()[0]
    run(read(region,'1:1',('--load-compressed',str(full/'score-01-01.cfr.zst'),
                          '--export-strategy',str(root/'wrong.txt'))),False)

    # Recovery uses the region schema and still skips a completed cell.
    recovery=root/'recovery';run(train(recovery,region='1:1',score='2:2'))
    obstacle=recovery/'score-02-01.cfr.zst';obstacle.mkdir()
    run(train(recovery,score='1:1'),False);obstacle.rmdir()
    checkpoint=recovery/'score-01-02.cfr.zst';before=(checkpoint.read_bytes(),checkpoint.stat().st_mtime_ns)
    assert 'score-recovered score=1:2 ' in run(train(recovery))
    assert (checkpoint.read_bytes(),checkpoint.stat().st_mtime_ns)==before
    assert rows(recovery)==rows(region)

    # Outside rows, swapped bounds, and unknown coverage versions are rejected.
    for name,mutate in [
        ('outside',lambda s:s+'0 2 0 2 50 9\n'),
        ('order',lambda s:s.replace('region=1:2','region=2:1')),
        ('version',lambda s:s.replace('score-region/v1','score-region/v9')),
    ]:
        path=root/name;shutil.copytree(asymmetric,path)
        (path/'values.txt').write_text(mutate((path/'values.txt').read_text()))
        run(read(path,'1:2',('--evaluate',)),False)

    if bot:
        # Train only the 25 cells of 35:35 and export those exact schemas. The
        # deployed selector must use the generic policy for excluded scores.
        scores=root/'bot-scores';run(train(scores,region='35:35',target=40))
        assert len(rows(scores))==25
        for a,b in rows(scores):
            run(read(scores,f'{a}:{b}',('--export-strategy',str(scores/f'score-{a:02d}-{b:02d}.txt')),target=40))
        generic=root/'public.cfr'
        run(['--format','ordago-public','--iterations','1','--seed','7','--save',str(generic)])
        queries=[]
        for a,b in [(35,35),(39,35),(35,39),(34,39),(39,34),(35,35)]:
            queries.append(dict(player=0,phase='grande',hand=[0]*7+[4],mus_stop_player=0,
                match_scores=[a,b],hand_start_scores=[a,b],pending_wager_total=0,
                legal_actions=[dict(type='pass'),dict(type='raise',amount=2),dict(type='ordago')]))
        before=hashes(scores)
        p=subprocess.run([bot,'--load',str(generic),'--score-table',str(scores/'values.txt'),
                          '--score-strategies',str(scores),'--seed','7'],
                         input='\n'.join(map(json.dumps,queries))+'\n',capture_output=True,text=True,timeout=20)
        assert p.returncode==0,p.stderr
        results=[json.loads(line) for line in p.stdout.splitlines()]
        assert len(results)==len(queries)
        for query,result in zip(queries,results):
            assert result['ok'],result
            a,b=query['hand_start_scores'];covered=a>=35 and b>=35
            assert result['diagnostic']['strategy']==('ordago-score' if covered else 'ordago-public'),result
        assert hashes(scores)==before
print('All closed-region checks passed.')
