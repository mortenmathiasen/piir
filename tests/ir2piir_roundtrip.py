#!/usr/bin/env python3
"""Round trip test of tools/ir2piir.py with an existing remote.

1. encode feature sets with the remote using PIIR itself (encode_remote):
   a base, every value of every feature changed alone, and random
   combinations
2. turn the codes into the pulses and spaces PIIR would transmit: these are
   the captures, as if recorded from a real remote
3. derive a remote from the captures with ir2piir.py
4. encode all feature sets, and as many random ones not used as captures,
   with the derived remote using PIIR again: every pulse train must be the
   same as the original's
5. check the derived remote, including its test vectors, with test_remotes

usage: ir2piir_roundtrip.py --encoder encode_remote --test-remotes test_remotes
           --ir2piir tools/ir2piir.py --out DIR [--expect-checksums JSON] REMOTE.json
"""

import argparse
import json
import os
import random
import subprocess
import sys
from collections import OrderedDict


def encode(encoder, remote, feature_sets):
    text = "".join(" ".join("%s=%s" % kv for kv in fs.items()) + "\n" for fs in feature_sets)
    out = subprocess.run([encoder, remote], input=text, capture_output=True, text=True)
    if out.returncode:
        sys.exit("encode_remote failed for %s: %s" % (remote, out.stderr))
    codes = out.stdout.split("\n")[:-1]
    assert len(codes) == len(feature_sets)
    return codes


def pulses(code, symbols):
    """Pulses and spaces as src/lirc.c builds them"""
    values = []
    for s in code:
        for is_mark, d in ((True, symbols[s]["mark"]), (False, symbols[s]["space"])):
            if d == 0 or (not values and not is_mark):
                continue
            if values and (len(values) % 2 == 1) == is_mark:
                values[-1] += d
            else:
                values.append(d)
    if len(values) % 2 == 0:
        values.pop()   # the signal ends with a pulse
    return values


def main():
    p = argparse.ArgumentParser()
    p.add_argument("remote")
    p.add_argument("--encoder", required=True)
    p.add_argument("--test-remotes", required=True)
    p.add_argument("--ir2piir", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--random", type=int, default=300)
    p.add_argument("--holdout", type=int, default=1000,
                   help="random feature sets checked but not used as captures")
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--expect-checksums")
    args = p.parse_args()

    with open(args.remote) as f:
        remote = json.load(f, object_pairs_hook=OrderedDict)
    values = OrderedDict((n, list(v)) for n, v in remote["features"].items())
    tests = remote.get("tests")
    if tests:
        base = OrderedDict((n, tests[0]["features"][n]) for n in values)
    else:
        base = OrderedDict((n, v[0]) for n, v in values.items())

    feature_sets = [base]
    for n, vs in values.items():
        for v in vs:
            if v != base[n]:
                fs = OrderedDict(base)
                fs[n] = v
                feature_sets.append(fs)
    rng = random.Random(args.seed)
    for _ in range(args.random):
        feature_sets.append(OrderedDict((n, rng.choice(vs)) for n, vs in values.items()))

    holdout = [OrderedDict((n, rng.choice(vs)) for n, vs in values.items())
               for _ in range(args.holdout)]

    os.makedirs(args.out, exist_ok=True)
    symbols = remote["symboldefinitions"]
    original = [pulses(code, symbols)
                for code in encode(args.encoder, args.remote, feature_sets + holdout)]
    captures = os.path.join(args.out, "captures.jsonl")
    with open(captures, "w") as f:
        for fs, durations in zip(feature_sets, original):  # holdout left out
            f.write(json.dumps({"features": fs, "frequency": remote["frequency"],
                                "durations": durations}) + "\n")
    print("%d captures written to %s" % (len(feature_sets), captures))

    derived = os.path.join(args.out, "derived.json")
    rc = subprocess.run([sys.executable, "-I", args.ir2piir, captures, "-o", derived,
                         "--dutycycle", str(remote.get("dutycycle", 0.5)),
                         "--description", "Derived from %s by ir2piir"
                         % os.path.basename(args.remote)]).returncode
    if rc:
        sys.exit("ir2piir failed with exit code %d" % rc)

    with open(derived) as f:
        result = json.load(f, object_pairs_hook=OrderedDict)
    failures = 0
    if args.expect_checksums is not None:
        expected = json.loads(args.expect_checksums)
        if result.get("checksums", []) != expected:
            print("FAIL checksums: expected %s, got %s"
                  % (expected, result.get("checksums")))
            failures += 1

    checked = feature_sets + holdout
    again = [pulses(code, result["symboldefinitions"])
             for code in encode(args.encoder, derived, checked)]
    differ = [i for i in range(len(checked)) if again[i] != original[i]]
    for i in differ[:5]:
        print("FAIL %s:\n  original %s\n  derived  %s" % (dict(checked[i]), original[i], again[i]))
    if differ:
        print("FAIL %d of %d feature sets (%d of %d held out) send other pulses with the "
              "derived remote" % (len(differ), len(checked),
                                  sum(i >= len(feature_sets) for i in differ), len(holdout)))
        failures += 1
    else:
        print("all %d feature sets (%d held out) send the same pulses with the derived remote"
              % (len(checked), len(holdout)))

    rc = subprocess.run([args.test_remotes, derived]).returncode
    if rc:
        print("FAIL test_remotes %s" % derived)
        failures += 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
