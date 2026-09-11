"""Exercise the process protocol with a small checkpoint of known policies."""
import copy
import json
import pathlib
import re
import subprocess
import sys
import tempfile

bot, fixture_builder = sys.argv[1:]
base = {
    "id": "one", "player": 0, "phase": "grande", "hand": [0] * 7 + [4],
    "legal_actions": [{"type": "pass"}, {"type": "raise", "amount": 8},
                      {"type": "raise", "amount": 10}, {"type": "ordago"}],
    "match_scores": [32, 32],
}
with tempfile.TemporaryDirectory(prefix="mus-adapter-") as directory:
    checkpoint = pathlib.Path(directory) / "fixture.cfr"
    subprocess.run([fixture_builder, str(checkpoint)], check=True, stdout=subprocess.DEVNULL)
    command = [bot, "--load", str(checkpoint), "--seed", "123"]
    repeated = copy.deepcopy(base)
    repeated.update(id="repeated", match_scores=[0, 0], history=[
        {"player": 0, "type": "pass"}, {"player": 1, "type": "raise", "amount": 2},
        {"player": 0, "type": "raise", "amount": 2}, {"player": 1, "type": "raise", "amount": 2}],
        legal_actions=[{"type": "reject"}, {"type": "accept"}, {"type": "raise", "amount": 4}])
    invalid_cases = []
    for field, value in [("hand", [4] * 8), ("player", True), ("phase", "grande\0x"),
                         ("match_scores", [40, 0]), ("known_hand_scores", [0.1, 0]),
                         ("opponent_hand", [0] * 7 + [4]), ("prior_wager_total", 2**64),
                         ("legal_actions", []), ("hand_scores_complete", "false")]:
        bad = copy.deepcopy(base)
        bad[field] = value
        invalid_cases.append(json.dumps(bad))
    for action in [{"type": "pass", "amount": 0}, {"type": "raise", "amount": 0},
                   {"type": "raise", "amount": 2.5}, {"type": "raise", "amount": 2**64},
                   {"type": "accept"}, {"type": "discard", "mask": 15},
                   {"type": "pass", "unknown": 1}]:
        bad = copy.deepcopy(base)
        bad["legal_actions"] = [action]
        invalid_cases.append(json.dumps(bad))
    invalid_cases += ['{"player":', 'null', '[]', 'true', '{}{}', '{} trailing', '{/*comment*/}',
                      '{"x":NaN}', '\0{}', ' ' * (1024 * 1024 + 1)]
    lines = [json.dumps(base), json.dumps(repeated)] + invalid_cases + [json.dumps(base)]
    output = subprocess.run(command, input="\n".join(lines) + "\n", text=True,
                            capture_output=True, check=True)
    results = [json.loads(line) for line in output.stdout.splitlines()]
    assert len(results) == len(lines), output.stdout
    assert not output.stderr, output.stderr
    first, repeat = results[:2]
    assert first["ok"] and first["id"] == "one"
    assert first["diagnostic"]["method"] == "exact"
    assert {p.get("amount") for p in first["probabilities"] if p["type"] == "raise"} == {8}
    assert repeat["diagnostic"]["method"] == "nearest"
    assert repeat["diagnostic"]["real_total"] == 6 and repeat["diagnostic"]["real_covered"] == 4
    assert len(repeat["diagnostic"]["sources"]) == 2
    assert all(isinstance(s["key"], str) for s in repeat["diagnostic"]["sources"])
    assert all(not r["ok"] for r in results[2:-1]), results
    assert results[-1]["ok"], results[-1]
    assert subprocess.run(command, input=json.dumps(base) + "\n", text=True, capture_output=True,
                          check=True).stdout.splitlines()[0] == output.stdout.splitlines()[0]
    # Each occurrence is validated; the last valid seed controls the sequence.
    seed_input = (json.dumps(base) + "\n") * 64
    seed_output = subprocess.run(command, input=seed_input, text=True,
                                 capture_output=True, check=True).stdout
    duplicate_seed = [bot, "--load", str(checkpoint), "--seed", "999", "--seed", "123"]
    assert subprocess.run(duplicate_seed, input=seed_input, text=True,
                          capture_output=True, check=True).stdout == seed_output
    assert subprocess.run(command[:-1] + ["999"], input=seed_input, text=True,
                          capture_output=True, check=True).stdout != seed_output
    assert subprocess.run([bot, "--load", str(checkpoint), "--seed", "bad", "--seed", "123"],
                          input="", text=True, capture_output=True).returncode != 0
    compressed = pathlib.Path(directory) / "fixture.cfr.zst"
    with compressed.open("wb") as stream:
        subprocess.run(["zstd", "-q", "-c", str(checkpoint)], stdout=stream, check=True)
    zipped = subprocess.run([bot, "--load-compressed", str(compressed), "--seed", "123"],
                            input=json.dumps(base) + "\n", text=True, capture_output=True, check=True)
    assert zipped.stdout.splitlines()[0] == output.stdout.splitlines()[0]

    text_path = pathlib.Path(str(checkpoint) + ".txt")
    text_command = [bot, "--strategy", str(text_path), "--seed", "123"]
    from_text = subprocess.run(text_command, input="\n".join(lines) + "\n", text=True,
                               capture_output=True, check=True)
    assert from_text.stdout == output.stdout, (from_text.stdout, output.stdout)
    assert not from_text.stderr
    # Compare both backends across every exact hand, both seats, and discards.
    import itertools
    queries = []
    for cards in itertools.combinations_with_replacement(range(8), 4):
        hand = [cards.count(i) for i in range(8)]
        for player in [0, 1]:
            queries.append({"player": player, "phase": "mus", "hand": hand,
                            "legal_actions": [{"type": "mus"}, {"type": "no_mus"}]})
        queries.append({"player": 0, "phase": "discard", "hand": hand,
                        "legal_actions": [{"type": "discard", "mask": 1}, {"type": "discard", "mask": 15}]})
    payload = "\n".join(map(json.dumps, queries)) + "\n"
    binary_policies = subprocess.run(command, input=payload, text=True, capture_output=True, check=True)
    text_policies = subprocess.run(text_command, input=payload, text=True, capture_output=True, check=True)
    assert binary_policies.stdout == text_policies.stdout
    assert all(json.loads(line)["diagnostic"]["method"] == "exact" for line in text_policies.stdout.splitlines())
    # Detect malformed headers, incomplete rows, and damaged queried probabilities.
    original_text = text_path.read_text()
    header = original_text.splitlines(keepends=True)[0]
    invalid_headers = [
        header,  # Every row is missing despite a positive information-set count.
        header.split(' variant=')[0] + '\n',
        original_text.replace(' information_sets=', ' ', 1),
        original_text.replace('training_iterations=', 'training_iterations=-', 1),
        original_text.replace('information_sets=', 'information_sets=-', 1),
        original_text.replace('variant=mccfr-external', 'variant=unknown', 1),
        original_text.replace(header, header.rstrip('\n') + ' extra=1\n', 1),
        re.sub(r'information_sets=\d+', 'information_sets=184467440737095516160', original_text, count=1),
        re.sub(r'information_sets=\d+', 'information_sets=0', original_text, count=1),
    ]
    for contents in ["garbage\n", original_text[:-1], original_text.replace("mus-strategy version=1", "mus-strategy version=2", 1)] + invalid_headers:
        invalid_text = pathlib.Path(directory) / "invalid.txt"
        invalid_text.write_text(contents)
        assert subprocess.run([bot, "--strategy", str(invalid_text)], capture_output=True).returncode != 0
    # An explicitly empty store is valid and uses the normal missing-data path.
    empty_text = pathlib.Path(directory) / 'empty.txt'
    empty_text.write_text(re.sub(r'information_sets=\d+', 'information_sets=0', header))
    empty = subprocess.run([bot, '--strategy', str(empty_text)], input=json.dumps(base) + '\n',
                           text=True, capture_output=True, check=True)
    assert json.loads(empty.stdout)['diagnostic']['method'] == 'fallback'
    invalid_text.write_text(original_text.replace("schema=mus.", "schema=bad.", 1))
    failure = subprocess.run([bot, "--strategy", str(invalid_text)], text=True, capture_output=True)
    assert failure.returncode != 0 and "incompatible_checkpoint" in failure.stderr
    damaged = original_text.splitlines()
    for index, row in enumerate(damaged):
        if row.startswith("infoset key=0 "):
            damaged[index] = row.replace("mus=0.75", "mus=nan", 1)
            break
    invalid_text.write_text("\n".join(damaged) + "\n")
    failure = subprocess.run([bot, "--strategy", str(invalid_text)], input=json.dumps({
        "player": 0, "phase": "mus", "hand": [0]*7+[4],
        "legal_actions": [{"type":"mus"}, {"type":"no_mus"}]}) + "\n",
        text=True, capture_output=True, check=True)
    assert json.loads(failure.stdout)["error"] == "invalid_strategy_data"
    for content in [b"invalid", checkpoint.read_bytes()[:-10],
                    checkpoint.read_bytes()[:-4] + b"bad!", checkpoint.read_bytes() + b"x"]:
        corrupt = pathlib.Path(directory) / "corrupt.cfr"
        corrupt.write_bytes(content)
        bad = subprocess.run([bot, "--load", str(corrupt)], text=True, capture_output=True)
        assert bad.returncode != 0 and not bad.stdout and "invalid_checkpoint" in bad.stderr
    # Change the schema without changing its length. Schema validation precedes CRC validation.
    incompatible = bytearray(checkpoint.read_bytes())
    incompatible[16] ^= 1
    corrupt.write_bytes(incompatible)
    bad = subprocess.run([bot, "--load", str(corrupt)], text=True, capture_output=True)
    assert bad.returncode != 0 and "incompatible_checkpoint" in bad.stderr
    for seed in ["-1", " -1", "+1", " +1", " 1", "1 ", "", "x", "18446744073709551616"]:
        assert subprocess.run(command[:-1] + [seed], input=b'', capture_output=True).returncode != 0
    for seed in ["0", "01", "18446744073709551615"]:
        assert subprocess.run(command[:-1] + [seed], input=b'', capture_output=True).returncode == 0
print("All adapter CLI checks passed.")
