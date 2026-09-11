# J08 recovery parser recheck (Claude)

Verdict: **APPROVE**, conditional on the announced final build, tests and paired
measurements passing after this change. No new logs exist yet: app/score_recovery.c and
tests/test_score_recovery.py changed at 21:16:10, after the last recovery logs. My two
earlier code approvals stand for everything else.

## `read_record_line` (app/score_recovery.c:119-131)
- **One physical line per call.** It always reads to `'\n'` or EOF, even after marking the
  line invalid, so a damaged line can never consume the next record. This fixes the old
  `fgets` + `strchr('\n')` bug, where an embedded NUL looked like a missing newline and the
  drain loop swallowed the following valid record.
- **Bounds.** It stores at most `capacity - 1` bytes, always terminates the string, and
  marks oversized lines invalid while still consuming them.
- **NUL bytes.** Any 0 byte marks the line invalid. Because the stored text is later handled
  with `strlen`, `strrchr` and `sscanf`, rejecting NULs keeps the checksummed text identical
  to what those functions see.
- **Unterminated tail.** EOF before a newline marks the line invalid, but it is still
  returned once (`length != 0`), so it is skipped; the next call returns false and the loop
  ends. An empty line (the journal's leading separators) is valid but has no tab, so it is
  skipped.
- **Errors.** A read error makes `getc` return EOF. The helper then returns false, or
  returns the partial line marked invalid and returns false on the next call, so there is
  no infinite loop. `ferror(stream)` after the loop still reports IO_ERROR, and `fclose` and
  the error paths are unchanged.
- **Locking.** `getc_unlocked` (available under `_POSIX_C_SOURCE 200809L`) is safe here: the
  `FILE` is local to `score_recovery_open`, which runs single-threaded before any worker
  starts.
- **Record checks.** Checksum, index and ownership checks and `strdup`/`free` are unchanged
  and still correct.

## Tests (tests/test_score_recovery.py:72-80)
- Both damaged lines (`b'broken\0record\n'` and an 8,192-byte line) are inserted directly
  **between** the separator newline and cell 1:2's valid record (`\n` + prefix + `1 2 …`).
  With the old parser the NUL case would have drained exactly that record, so the test would
  fail on the old code.
- Each case requires `score-recovered score=1:2` and final policies identical to a complete
  run.
- The oversized case exceeds the 2048-byte capacity and guards the new bounded path.

## Non-blocking
- An I/O error in the middle of the journal makes the whole run fail with IO_ERROR rather
  than skipping only the damaged record. That is conservative and matches the documented
  "other read errors stop the work".
- Rerun the normal, ASan/UBSan and two-mus recovery suites and the paired benchmarks on
  this exact revision, and record them before committing.
