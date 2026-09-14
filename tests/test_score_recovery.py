"""Recover completed, unpublished score cells without rewriting their checkpoints."""
import hashlib
import os
from pathlib import Path
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import struct
import zlib

solver = str(Path(sys.argv[1]).resolve())
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def policies(path):
    return {p.name:digest(p) for p in path.iterdir() if p.name=='values.txt' or p.name.endswith('.cfr.zst')}

def args(path,position='1:1',seed=7,iterations=10,samples=100,extra=()):
    return ['--format','ordago-score','--target','3','--score',position,
            '--iterations',str(iterations),'--evaluation-samples',str(samples),
            '--continuation',str(path),'--seed',str(seed),*extra]

def run(command,success=True,timeout=30):
    p=subprocess.run([solver,*command],capture_output=True,text=True,timeout=timeout)
    assert (p.returncode==0)==success,(command,p.returncode,p.stdout,p.stderr)
    return p.stdout

with tempfile.TemporaryDirectory(prefix='mus-recovery-tests-') as tmp:
    root=Path(tmp)
    top=root/'top';run(args(top,'2:2'))
    complete=root/'complete';run(args(complete))

    def interrupted(name,extra=()):
        path=root/name;shutil.copytree(top,path)
        obstacle=path/'score-02-01.cfr.zst';obstacle.mkdir()
        output=run(args(path,extra=extra),False)
        assert 'score-cell score=1:2' in output
        assert policies(top)['values.txt']==digest(path/'values.txt')
        obstacle.rmdir()
        return path

    path=interrupted('recovered')
    checkpoint=path/'score-01-02.cfr.zst';before=(digest(checkpoint),checkpoint.stat().st_mtime_ns)
    output=run(args(path))
    assert 'score-recovered score=1:2' in output and 'score-cell score=1:2' not in output
    assert (digest(checkpoint),checkpoint.stat().st_mtime_ns)==before
    assert policies(path)==policies(complete)

    # Incomplete records and incompatible settings require fresh training.
    for name,mutation,extra in [
        ('truncated',lambda p:(p/'recovery.txt').write_bytes((p/'recovery.txt').read_bytes()[:-12]),()),
        ('checksum',lambda p:(p/'recovery.txt').write_bytes((p/'recovery.txt').read_bytes().replace(b'weight=',b'Weight=')),()),
        ('checkpoint',lambda p:(p/'score-01-02.cfr.zst').write_bytes(b'incomplete checkpoint'),()),
        ('threads',lambda p:None,('--threads','2')),
        ('reserve',lambda p:None,('--reserve-information-sets','50')),
    ]:
        path=interrupted(name);mutation(path)
        output=run(args(path,extra=extra))
        assert 'score-recovered score=1:2' not in output,(name,output)
        assert 'score-cell score=1:2' in output
        assert policies(path)==policies(complete)

    # A partial tail cannot swallow a later valid record appended by a retry.
    path=interrupted('partial-tail')
    with (path/'recovery.txt').open('ab') as f:f.write(b'1 2 interrupted')
    # Recover the old complete record, then publish the remaining cells.
    assert 'score-recovered score=1:2' in run(args(path))
    assert policies(path)==policies(complete)

    # A binary or oversized damaged line must not consume the next valid line.
    for name,prefix in [('nul-line',b'broken\0record\n'),('long-line',b'x'*8192+b'\n')]:
        path=interrupted(name)
        journal=path/'recovery.txt'
        data=journal.read_bytes()
        data=data.replace(b'\n1 2 ',b'\n'+prefix+b'1 2 ')
        journal.write_bytes(data)
        assert 'score-recovered score=1:2' in run(args(path))
        assert policies(path)==policies(complete)

    # Same prior path with changed contents is incompatible, even if its size
    # and timestamp are restored. All source files remain read-only to recovery.
    prior=root/'prior.cfr'
    run(['--format','ordago-public','--iterations','1','--seed','1','--save',str(prior)])
    path=interrupted('prior',('--preload-ordago',str(prior)))
    old_stat=prior.stat()
    # Change only the saved RNG state, retaining a valid v1 raw checkpoint,
    # the policy, file length, and original timestamps.
    data=bytearray(prior.read_bytes())
    assert data[:8]==b'CFRCKPT\0' and struct.unpack_from('<I',data,8)[0]==1
    assert zlib.crc32(data[:-4])==struct.unpack_from('<I',data,len(data)-4)[0]
    schema_length=struct.unpack_from('<I',data,12)[0]
    data[16+schema_length+4]^=1
    struct.pack_into('<I',data,len(data)-4,zlib.crc32(data[:-4]))
    prior.write_bytes(data)
    assert prior.stat().st_size==old_stat.st_size
    os.utime(prior,ns=(old_stat.st_atime_ns,old_stat.st_mtime_ns))
    prior_digest=digest(prior)
    output=run(args(path,extra=('--preload-ordago',str(prior))))
    assert 'score-recovered score=1:2' not in output and digest(prior)==prior_digest

    # A source extension recovers a completed destination cell, preserving
    # source bytes. Extension options omit --seed as required by the CLI.
    source=root/'source';run(args(source,'0:0'))
    def extension_args(destination,position):
        command=args(destination,position)
        index=command.index('--seed');del command[index:index+2]
        return [*command,'--extend-continuation',str(source)]
    destination=root/'extension';run(extension_args(destination,'2:2'))
    obstacle=destination/'score-02-01.cfr.zst';obstacle.mkdir()
    run(extension_args(destination,'1:1'),False);obstacle.rmdir()
    source_before={p.name:digest(p) for p in source.iterdir()}
    checkpoint=destination/'score-01-02.cfr.zst';before=(digest(checkpoint),checkpoint.stat().st_mtime_ns)
    output=run(extension_args(destination,'1:1'))
    assert 'score-recovered score=1:2' in output
    assert (digest(checkpoint),checkpoint.stat().st_mtime_ns)==before
    assert {p.name:digest(p) for p in source.iterdir()}==source_before

    # Real process death after the first published cell. The next cell has
    # enough work that the reader can kill the child before the diagonal ends.
    killed=root/'killed'
    # Two muses generate substantially more work per iteration in debug.
    signal_iterations=100 if 'muses-2' in (top/'values.txt').read_text() else 500
    command=args(killed,'1:1',iterations=signal_iterations,samples=1000)
    child=subprocess.Popen([solver,*command],stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                           bufsize=0,start_new_session=True)
    try:
        deadline=time.monotonic()+30
        while time.monotonic()<deadline:
            if select.select([child.stdout],[],[],1)[0]:
                line=child.stdout.readline()
                if b'score-cell score=1:2' in line:
                    os.kill(child.pid,signal.SIGKILL);break
                assert line or child.poll() is None
        else:raise AssertionError('cell did not finish')
        child.wait(timeout=10)
        assert child.returncode==-signal.SIGKILL
    finally:
        if child.poll() is None:child.kill();child.wait()
        child.stdout.close();child.stderr.close()
    checkpoint=killed/'score-01-02.cfr.zst';before=(digest(checkpoint),checkpoint.stat().st_mtime_ns)
    # Completing the deliberately long interrupted run is much slower under
    # ASan/TSan than the small recovery fixtures above.
    output=run(command,timeout=180)
    assert 'score-recovered score=1:2' in output
    assert (digest(checkpoint),checkpoint.stat().st_mtime_ns)==before
print('All completed-cell recovery checks passed.')
