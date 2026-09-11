"""Check process locks, interrupted writers, and immutable extension sources."""
import fcntl
import contextlib
import hashlib
import os
import pathlib
import signal
import shutil
import subprocess
import sys
import tempfile
import time

solver = sys.argv[1]
children = []

def command(directory, iterations=1):
    return [solver, '--format', 'ordago-score', '--target', '3', '--score', '2:2',
            '--iterations', str(iterations), '--evaluation-samples', '1', '--seed', '7',
            '--continuation', str(directory)]

def run(args, success=True):
    p = subprocess.run(args, input='', text=True, capture_output=True, timeout=15)
    assert (p.returncode == 0) == success, (args, p.returncode, p.stderr)
    return p

def start(args):
    p = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.PIPE, text=True,
                         preexec_fn=lambda: signal.signal(signal.SIGINT, signal.SIG_DFL))
    children.append(p)
    return p

@contextlib.contextmanager
def directory_lock(directory, kind):
    descriptor = os.open(directory, os.O_RDONLY | os.O_DIRECTORY)
    try:
        fcntl.flock(descriptor, kind | fcntl.LOCK_NB)
        yield descriptor
    finally:
        os.close(descriptor)

def held(path):
    try:
        with directory_lock(path.parent, fcntl.LOCK_EX): pass
    except BlockingIOError:
        return True
    except FileNotFoundError:
        pass
    return False

def wait_locked(p, path):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        try: metadata = path.read_text()
        except FileNotFoundError: metadata = ''
        if metadata.startswith(f'pid={p.pid} '):
            assert held(path)
            return
        assert p.poll() is None, (p.returncode, p.stderr.read())
        time.sleep(.01)
    raise AssertionError(f'writer did not lock {path}')

def stop(p, sig):
    p.send_signal(sig)
    assert p.wait(timeout=10) == -sig, (p.returncode, p.stderr.read())

try:
    with tempfile.TemporaryDirectory(prefix='mus-score-lock-tests-') as tmp:
        root = pathlib.Path(tmp)
        for sig in [signal.SIGTERM, signal.SIGINT, signal.SIGKILL]:
            directory = root/f'signal-{sig}'
            path = directory/'training.lock'
            writer = start(command(directory, 100000000))
            wait_locked(writer, path)
            inode = path.stat().st_ino
            # A denied writer must not replace, truncate or unlink the held inode.
            # The owner writes metadata just after acquisition, so wait for its flush.
            deadline = time.monotonic() + 5
            while not path.read_bytes() and time.monotonic() < deadline: time.sleep(.01)
            metadata = path.read_bytes(); assert metadata.startswith(b'pid=')
            denied = run(command(directory), False)
            assert 'bloqueo' in denied.stderr
            assert path.stat().st_ino == inode and path.read_bytes() == metadata
            stop(writer, sig)
            assert path.exists() and not held(path)
            run(command(directory))
            assert path.stat().st_ino == inode and not held(path)

        # The file's existence and its recorded PID do not own a lock.
        orphan = root/'orphan'; orphan.mkdir()
        path = orphan/'training.lock'; path.write_text('pid=999999 host=old started=unknown\n')
        inode = path.stat().st_ino
        run(command(orphan))
        assert path.stat().st_ino == inode and not held(path)

        # Shared source locks coexist with readers and exclude all writers for
        # the entire extension, including while a destination cell trains.
        source = root/'source'; run(command(source))
        source_lock = source/'training.lock'
        original = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source.iterdir()}
        destination = root/'extended'
        args = command(destination, 100000000)
        seed = args.index('--seed'); del args[seed:seed+2]
        args += ['--extend-continuation', str(source)]
        extension = start(args)
        wait_locked(extension, destination/'training.lock')
        assert held(source_lock)
        with directory_lock(source, fcntl.LOCK_SH):
            run(command(source), False)
        stop(extension, signal.SIGTERM)
        assert not held(source_lock) and not held(destination/'training.lock')
        assert {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source.iterdir()} == original

        # An active source writer also excludes new extensions.
        with directory_lock(source, fcntl.LOCK_EX):
            run(args, False)
            # Evaluation/export and individual checkpoint continuations also
            # take shared directory locks and must not read an active writer.
            read_args = [solver,'--format','ordago-score','--target','3','--score','2:2',
                         '--continuation',str(source),'--evaluate','--evaluation-samples','1']
            run(read_args, False)
            run(read_args + ['--load-compressed',str(source/'score-02-02.cfr.zst')], False)
        assert not held(source_lock)

        # An archived pre-change source has no lock file and may be read-only.
        archived = root/'archived'
        shutil.copytree(source, archived, ignore=shutil.ignore_patterns('training.lock'))
        preserved = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in archived.iterdir()}
        archived.chmod(0o555)
        try:
            archived_args = command(root/'from-archive')
            seed = archived_args.index('--seed'); del archived_args[seed:seed+2]
            run(archived_args + ['--extend-continuation',str(archived)])
            assert {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in archived.iterdir()} == preserved
            assert not (archived/'training.lock').exists()
        finally:
            archived.chmod(0o755)

        # Setup errors release both descriptors; their permanent files remain.
        blocked = root/'not-a-directory'; blocked.write_text('keep\n')
        bad = command(blocked)
        seed = bad.index('--seed'); del bad[seed:seed+2]
        run(bad + ['--extend-continuation', str(source)], False)
        assert not held(source_lock) and blocked.read_text() == 'keep\n'
        invalid = root/'invalid'; invalid.mkdir(); (invalid/'values.txt').write_text('invalid\n')
        run(command(invalid), False)
        assert (invalid/'training.lock').exists() and not held(invalid/'training.lock')
finally:
    for child in children:
        if child.poll() is None: child.kill()
        child.wait(timeout=10)
        child.stderr.close()
print('All score process-lock checks passed.')
