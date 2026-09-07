"""Measure coverage and latency; compare three policies on scripted grande lances.

This is a bounded diagnostic, not match win rate or an exploitability estimate.
Only the benchmark scorer sees the opponent's cards. Every query contains the
bot's own cards and public events. All methods share the same checkpoint.
"""
import argparse
import collections
import copy
import json
import random
import resource
import statistics
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary")
parser.add_argument("checkpoint")
parser.add_argument("--raw", action="store_true")
parser.add_argument("--text", action="store_true")
parser.add_argument("--deals", type=int, default=30)
args = parser.parse_args()
if args.deals <= 0:
    parser.error("--deals must be positive")
start = time.perf_counter()
process = subprocess.Popen([args.binary, "--strategy" if args.text else "--load" if args.raw else "--load-compressed",
                            args.checkpoint, "--seed", "17", "--no-score-adjustment"],
                           stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
latencies = []
coverage = collections.Counter()
cache = {}
first_response = None

def query(observation):
    global first_response
    key = json.dumps(observation, sort_keys=True)
    if key in cache:
        return cache[key]
    before = time.perf_counter()
    process.stdin.write(key + "\n")
    process.stdin.flush()
    line = process.stdout.readline()
    if not line:
        raise RuntimeError(f"bot exited: {process.poll()}")
    response = json.loads(line)
    after = time.perf_counter()
    if first_response is None:
        first_response = after - start
    else:
        latencies.append(1000 * (after - before))
    if not response["ok"]:
        raise RuntimeError((observation, response))
    coverage[response["diagnostic"]["method"]] += 1
    assert response["diagnostic"]["lookup_count"] <= 64
    assert abs(sum(a["probability"] for a in response["probabilities"]) - 1) < 1e-9
    for action in response["probabilities"]:
        assert {k: v for k, v in action.items() if k != "probability"} in observation["legal_actions"]
    cache[key] = response
    return response

response_legal = [{"type": "reject"}, {"type": "accept"}, {"type": "ordago"}] + [
    {"type": "raise", "amount": n} for n in range(2, 41)]
opening_legal = [{"type": "pass"}, {"type": "ordago"}] + [
    {"type": "raise", "amount": n} for n in range(2, 41)]

def base_observation(hand, player, history):
    return {"player": player, "phase": "grande", "hand": hand,
            "history": history, "legal_actions": response_legal,
            "hand_scores_complete": True}

def project_history(history, player, method):
    """Translate upward, or freeze at the last representable bot response."""
    projected, last = [], None
    total, previous_increment, count = 0, 0, 0
    used = [set(), set()]
    for event in history:
        event = event.copy()
        if event["type"] == "raise":
            allowed = [x for x in ([2, 4, 7, 10] if count == 0 else [2, 4, 10])
                       if x not in used[event["player"]] and x >= previous_increment
                       and total + x < 40 and count < 5]
            amount = event["amount"]
            if amount not in allowed:
                larger = [x for x in allowed if x >= amount]
                if method == "last_valid" or not larger:
                    break
                amount = min(larger)
            event["amount"] = amount
            total += amount
            previous_increment = amount
            count += 1
            used[event["player"]].add(amount)
        projected.append(event)
        if event["player"] != player and event["type"] != "pass":
            last = copy.deepcopy(projected)
    return last

# Check all lances and both seats in addition to the rollout positions.
for player in [0, 1]:
    for phase, hand in [("grande", [0]*7+[4]), ("chica", [4]+[0]*7),
                        ("pares", [2]+[0]*6+[2]), ("juego", [1]+[0]*6+[3]),
                        ("punto", [4]+[0]*7)]:
        previous = {"grande": 0, "chica": 1, "pares": 2, "juego": 3, "punto": 3}[phase]
        obs = {"player": player, "phase": phase, "hand": hand,
               "previous_strengths": [0]*previous, "legal_actions": opening_legal,
               "history": [] if player == 0 else [{"player": 0, "type": "pass"}]}
        query(obs)
    query({"player": player, "phase": "mus", "hand": [0]*7+[4],
           "legal_actions": [{"type": "mus"}, {"type": "no_mus"}]})
    query({"player": player, "phase": "discard", "hand": [0]*7+[4],
           "discard_counts": [2, 0] if player else [0, 0],
           "legal_actions": [{"type": "discard", "mask": n} for n in [1, 3, 7, 15]]})

# Include amounts outside the action abstraction and numeric/ordago distinctions.
for amount in [1, 3, 5, 6, 8, 9, 11, 40, 100]:
    history = [{"player": 0, "type": "pass"}, {"player": 1, "type": "raise", "amount": amount}]
    query(base_observation([1]+[0]*6+[3], 0, history))
query({"player": 0, "phase": "grande", "hand": [1]+[0]*6+[3],
       "history": [{"player": 0, "type": "pass"}, {"player": 1, "type": "ordago"}],
       "legal_actions": [{"type": "reject"}, {"type": "accept"}]})

rng = random.Random(41)
deck = [i for i, count in enumerate([8, 4, 4, 4, 4, 4, 4, 8]) for _ in range(count)]
deals = []
for i in range(args.deals):
    cards = rng.sample(deck, 8)
    deals.append(([cards[:4].count(j) for j in range(8)], cards[4:], i % 2))

scores = collections.defaultdict(list)
for method in ["mixture", "round_up", "last_valid"]:
    for script in ["accept", "repeat_2", "repeat_3"]:
        for deal_index, (hand, opponent_hand, player) in enumerate(deals):
            # Fix initial choices to reach the repeated-raise decision from the request.
            opponent = 1 - player
            history = ([{"player": 0, "type": "pass"}] if player == 0 else []) + [
                {"player": opponent, "type": "raise", "amount": 2},
                {"player": player, "type": "raise", "amount": 2},
                {"player": opponent, "type": "raise", "amount": 2}]
            own = sorted([i for i, count in enumerate(hand) for _ in range(count)], reverse=True)
            other = sorted(opponent_hand, reverse=True)
            wins = own > other or (own == other and player == 0)
            sample_rng = random.Random(1000 + deal_index)
            total, covered = 6, 4
            value = None
            for turn in range(20):
                queried_history = history if method == "mixture" else project_history(history, player, method)
                assert queried_history is not None
                obs = base_observation(hand, player, queried_history)
                policy = query(obs)["probabilities"]
                # Apply the same real amount cap to each baseline after its lookup.
                adjusted = []
                for action in policy:
                    action = action.copy()
                    if action["type"] == "raise":
                        if 40 - total < 2:
                            action["type"] = "accept"
                            action.pop("amount")
                        else:
                            action["amount"] = min(action["amount"], 40 - total)
                    adjusted.append(action)
                sample, cumulative = sample_rng.random(), 0
                chosen = adjusted[-1]
                for action in adjusted:
                    cumulative += action["probability"]
                    if sample < cumulative:
                        chosen = action
                        break
                if chosen["type"] == "reject":
                    value = -covered
                    break
                if chosen["type"] == "accept":
                    value = total if wins else -total
                    break
                if chosen["type"] == "ordago":
                    value = 40 if wins else -40  # Scripted opponents always accept an ordago.
                    break
                assert chosen["type"] == "raise"
                covered, total = total, total + chosen["amount"]
                history.append({"player": player, "type": "raise", "amount": chosen["amount"]})
                if script == "accept" or turn >= 4:
                    value = min(total, 40) * (1 if wins else -1)
                    break
                increment = 2 if script == "repeat_2" else 3
                covered, total = total, total + increment
                history.append({"player": opponent, "type": "raise", "amount": increment})
            assert value is not None
            scores[method, script].append(value)
bot_memory = {}
try:
    with open(f"/proc/{process.pid}/status") as status_file:
        for line in status_file:
            if line.startswith("VmHWM:"):
                bot_memory["bot_peak_rss_kib"] = int(line.split()[1])
            elif line.startswith("VmRSS:"):
                bot_memory["bot_resident_rss_kib"] = int(line.split()[1])
except FileNotFoundError:
    pass
process.stdin.close()
assert process.wait() == 0
result = {
    "scope": "scripted_grande_lances_from_fixed_repeated_raise_history",
    "source": "text" if args.text else "checkpoint",
    **bot_memory,
    "deals_per_script": args.deals,
    "load_and_first_query_seconds": first_response,
    "unique_queries": len(cache), "coverage": dict(coverage),
    "latency_ms_median": statistics.median(latencies),
    "latency_ms_max": max(latencies),
    "child_peak_rss_kib": resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss,
    "mean_points": {method: {script: statistics.mean(scores[method, script])
                              for script in ["accept", "repeat_2", "repeat_3"]}
                    for method in ["mixture", "round_up", "last_valid"]},
}
print(json.dumps(result, indent=2))
