#!/usr/bin/env python3
"""Derive a PIIR remote from recorded IR codes.

The input is one or more capture files in JSON Lines format, one recorded
code per line, with the features (settings) the code was sent for:

  {"features": {"mode": "HEAT", "temperature": "21"}, "frequency": 38000,
   "durations": [3500, 1750, 435, 435, 435, 1300, ...]}

"durations" alternate mark and space in microseconds, starting with a mark;
a trailing space may be left out. "frequency" is optional (default 38000).
Blank lines and lines starting with # are skipped. Every capture names the
same features.

The captures must include a base code (the first capture) and, for every
value of every feature, a code where only that feature differs from the base.
More captures, e.g. random combinations of features, are used to find
checksums and to verify the result.

The remote is derived in steps:
  1. mark and space durations are clustered into symbols; the two most
     common mark/space pairs become the bit symbols 0 and 1
  2. checksums are searched among bytes and nibbles: sums, negated sums and
     XORs of other bytes or nibbles, adjacent or not (see "Checksums" in
     README.md)
  3. each feature gets the bits that change with its values. A value that
     also sets bits of another feature (e.g. a fan mode forcing a
     temperature) sets those too, and its feature is placed after the other.
     Such overrides are found where a value changes the bits from the base
     code, or where the bits stay the same in every capture with that value
     although the other feature varies (this needs combinations of features).
     An override that only happens with some values of a third feature, e.g.
     a mode forcing the fan only while the power is on, becomes a conditional
     override ("overrides" in the remote)
  4. every capture is encoded again from the remote and compared

The result is written as a PIIR remote, with a sample of the captures as
test vectors. The exit code is 1 when some captures cannot be reproduced.
"""

import argparse
import json
import sys
from collections import Counter, OrderedDict

SEPARATOR = "-"
# Names of symbols other than the bits, in order of first appearance
SYMBOL_NAMES = "HWABCDEFGIJKLMNOPQRSTUVXYZ"


class Ir2PiirError(Exception):
    pass


# --------------------------------------------------------------- captures

def load_captures(paths):
    captures = []
    for path in paths:
        try:
            with open(path) as f:
                lines = f.readlines()
        except OSError as e:
            raise Ir2PiirError("cannot read %s: %s" % (path, e.strerror))
        for number, line in enumerate(lines, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            where = "%s:%d" % (path, number)
            try:
                data = json.loads(line)
            except ValueError as e:
                raise Ir2PiirError("%s: %s" % (where, e))
            if not isinstance(data, dict):
                raise Ir2PiirError("%s: not a JSON object" % where)
            features = data.get("features")
            durations = data.get("durations")
            if not isinstance(features, dict) or not features:
                raise Ir2PiirError("%s: no features" % where)
            if (not isinstance(durations, list) or len(durations) < 2 or
                    not all(isinstance(d, (int, float)) and d > 0 for d in durations)):
                raise Ir2PiirError("%s: durations must be positive numbers" % where)
            captures.append({
                "features": OrderedDict((str(k), str(v)) for k, v in features.items()),
                "durations": [int(round(d)) for d in durations],
                "frequency": int(data.get("frequency", 38000)),
                "where": where,
            })
    if not captures:
        raise Ir2PiirError("no captures")
    names = list(captures[0]["features"])
    for c in captures:
        if sorted(c["features"]) != sorted(names):
            raise Ir2PiirError("%s: features %s differ from the first capture's %s"
                               % (c["where"], sorted(c["features"]), sorted(names)))
    return captures


# ---------------------------------------------------------------- symbols

def cluster(values, tolerance):
    """Group durations that are within tolerance (relative) of the smallest
    duration of their group. Returns {value: group index} and the rounded
    mean of each group."""
    groups = []
    for v in sorted(set(values)):
        if groups and v <= groups[-1][0] * (1 + tolerance):
            groups[-1].append(v)
        else:
            groups.append([v])
    counts = Counter(values)
    index, means = {}, []
    for i, group in enumerate(groups):
        total = sum(v * counts[v] for v in group)
        means.append(int(round(total / sum(counts[v] for v in group))))
        for v in group:
            index[v] = i
    return index, means


def derive_symbols(captures, tolerance=0.2):
    """Turn the durations of every capture into a symbol string. Returns
    {symbol: (mark, space)} and the codes."""
    marks = [d for c in captures for d in c["durations"][0::2]]
    spaces = [d for c in captures for d in c["durations"][1::2]]
    mark_index, mark_means = cluster(marks, tolerance)
    space_index, space_means = cluster(spaces, tolerance)

    pair_lists = []
    for c in captures:
        d = c["durations"]
        pairs = [(mark_index[d[i]], space_index[d[i + 1]] if i + 1 < len(d) else None)
                 for i in range(0, len(d), 2)]
        pair_lists.append(pairs)

    counts = Counter(p for pairs in pair_lists for p in pairs if p[1] is not None)
    if len(counts) < 2:
        raise Ir2PiirError("the codes need at least two kinds of mark/space pairs")
    (a, _), (b, _) = counts.most_common(2)
    duration = lambda p: (mark_means[p[0]], space_means[p[1]])
    if a[0] == b[0]:     # pulse distance: the shorter space is 0
        zero, one = sorted([a, b], key=lambda p: space_means[p[1]])
    elif a[1] == b[1]:   # pulse width: the shorter mark is 0
        zero, one = sorted([a, b], key=lambda p: mark_means[p[0]])
    else:
        zero, one = sorted([a, b], key=lambda p: sum(duration(p)))
    names = {zero: "0", one: "1"}
    symbols = OrderedDict([("0", duration(zero)), ("1", duration(one))])

    free = iter(SYMBOL_NAMES)
    for pairs in pair_lists:
        for p in pairs:
            if p[1] is not None and p not in names:
                name = next(free)
                names[p] = name
                symbols[name] = duration(p)

    def lone_mark(mark):
        # A final mark without its space, e.g. a stop bit: any symbol with
        # that mark sends it, the trailing space is not transmitted
        for p, name in names.items():
            if p[0] == mark:
                return name
        name = next(free)
        names[(mark, None)] = name
        symbols[name] = (mark_means[mark], 0)
        return name

    codes = []
    for pairs in pair_lists:
        code = "".join(names[p] if p[1] is not None else lone_mark(p[0]) for p in pairs)
        codes.append(code)
    return symbols, codes


def add_separators(code):
    """Insert a separator after every byte of bits and between other symbols
    and bits, for readability. Separators are not transmitted."""
    out, bits = [], 0
    for i, s in enumerate(code):
        if out and out[-1] != SEPARATOR and (s in "01") != (code[i - 1] in "01"):
            out.append(SEPARATOR)
        out.append(s)
        if s in "01":
            bits += 1
            if bits % 8 == 0 and i + 1 < len(code):
                out.append(SEPARATOR)
    return "".join(out)


def check_structure(captures, codes):
    """All codes must have the same symbols apart from the bits"""
    shape = lambda code: "".join("b" if s in "01" else s for s in code)
    first = shape(codes[0])
    for c, code in zip(captures, codes):
        if shape(code) != first:
            raise Ir2PiirError("%s has another structure than the first capture "
                               "(codes of different lengths are not supported):\n  %s\n  %s"
                               % (c["where"], codes[0], code))


# -------------------------------------------------------------- checksums

def bit_positions(code, bits="01"):
    return [i for i, s in enumerate(code) if s in bits]


def unit_bits(code, width, bits="01", restart=""):
    """Positions of the bits read as units, as src/checksum.c: the bits of a
    partial unit before a restart symbol are skipped"""
    positions, fill = [], 0
    for i, s in enumerate(code):
        if s in bits:
            positions.append(i)
            fill = (fill + 1) % width
        elif s in restart and fill:
            del positions[-fill:]
            fill = 0
    return positions


def read_units(code, positions, width, msb):
    values = []
    for start in range(0, len(positions) - width + 1, width):
        v = 0
        for i in range(width):
            if code[positions[start + i]] == "1":
                v |= 1 << (width - 1 - i if msb else i)
        values.append(v)
    return values


def apply_checksums(code, checksums):
    """Apply checksums like PIIR does (src/checksum.c)"""
    code = list(code)
    for spec in checksums:
        width = 4 if spec.get("unit") == "nibble" else 8
        msb = spec.get("bitorder") == "msb"
        bits = spec.get("bits", "01")
        mask = (1 << width) - 1
        positions = unit_bits(code, width, bits, spec.get("restart", ""))
        units = read_units("".join("1" if s == bits[1] else "0" for s in code),
                           positions, width, msb)
        covered = spec["data"] if "data" in spec else range(spec["from"], spec["to"] + 1)
        value = spec.get("init", 0)
        for u in covered:
            value = value + units[u] if spec["type"] == "sum" else value ^ units[u]
        if spec.get("invert"):
            value = ~value
        if spec.get("negate"):
            value = -value
        value &= mask
        at = spec["at"]
        for i in range(width):
            bit = value >> (width - 1 - i if msb else i) & 1
            code[positions[at * width + i]] = bits[bit]
    return "".join(code)


def unit_positions(code, spec, unit=None):
    """The symbols of a checksum's unit (by default the one it writes)"""
    width = 4 if spec.get("unit") == "nibble" else 8
    positions = unit_bits(code, width, spec.get("bits", "01"), spec.get("restart", ""))
    unit = spec["at"] if unit is None else unit
    return positions[unit * width:(unit + 1) * width]


# Units a sum checksum may or may not cover that its lowest bits cannot tell
# apart are tried both ways, up to this many
MAX_UNDECIDED = 8


def _solve_gf2(rows, variables):
    """Solve linear equations over GF(2): rows are (mask, rhs) with bit j of
    mask the coefficient of variable j. Yields solutions, the free variables
    tried both ways when there are at most MAX_UNDECIDED of them (otherwise 0).
    Stops reading rows once 64 in a row add nothing; the caller verifies."""
    basis = {}
    idle = 0
    for mask, rhs in rows:
        while mask:
            lead = mask.bit_length() - 1
            if lead not in basis:
                basis[lead] = (mask, rhs)
                idle = 0
                break
            bm, br = basis[lead]
            mask ^= bm
            rhs ^= br
        else:
            if rhs:
                return
            idle += 1
            if idle >= 64:
                break
    free = [j for j in range(variables) if j not in basis]
    if len(free) > MAX_UNDECIDED:
        free = []
    for choice in range(1 << len(free)):
        x = 0
        for i, j in enumerate(free):
            if choice >> i & 1:
                x |= 1 << j
        for lead in sorted(basis):
            bm, br = basis[lead]
            if br ^ (bin(bm & x & ~(1 << lead)).count("1") & 1):
                x |= 1 << lead
        yield x


def _covered(spec):
    if "data" in spec:
        return list(spec["data"])
    return list(range(spec["from"], spec["to"] + 1))


def _shape(spec, covered, at, init, kind, unit_lists, varying, mask):
    """Write the covered units as a range when they are contiguous, extended
    over constant units when that makes the start value 0, else as a list"""
    lo, hi = min(covered), max(covered)
    inside = [u for u in range(lo, hi + 1) if u in varying or u == at]
    if set(inside) <= set(covered):
        first = unit_lists[0]
        combine = (lambda a, b: (a - b) & mask) if kind == "sum" else (lambda a, b: a ^ b)
        lo_min, hi_max = lo, hi
        while lo_min > 0 and lo_min - 1 not in varying and lo_min - 1 != at:
            lo_min -= 1
        while hi_max < len(first) - 1 and hi_max + 1 not in varying and hi_max + 1 != at:
            hi_max += 1
        # The range also covers the constant units between the covered ones
        for u in range(lo, hi + 1):
            if u not in covered:
                init = combine(init, first[u])
        best = (lo, hi, init)
        for a in (range(lo, lo_min - 1, -1) if init else ()):
            for b in range(hi, hi_max + 1):
                value = init
                for u in list(range(a, lo)) + list(range(hi + 1, b + 1)):
                    value = combine(value, first[u])
                if value == 0 and (best[2] != 0 or b - a > best[1] - best[0]):
                    best = (a, b, value)
        spec["from"], spec["to"] = best[0], best[1]
        init = best[2]
    else:
        spec["data"] = sorted(covered)
    spec["at"] = at
    return init


def _search(unit_lists, width, msb, taken):
    """Find checksums among the units of every capture: for each unit, the
    other units whose sum or XOR it is. The covered units are found by
    solving the lowest bits over GF(2) (for XOR every bit), then verified on
    every capture. Returns candidates sorted by preference."""
    mask = (1 << width) - 1
    n = len(unit_lists[0])
    varying = [u for u in range(n) if len({units[u] for units in unit_lists}) > 1]
    # Units equal in every capture, e.g. a frame sent twice, are one unknown:
    # otherwise a solution may count a unit and its copy, which cancel in the
    # lowest bits but not in a sum
    first_of = {}
    for u in varying:
        first_of.setdefault(tuple(units[u] for units in unit_lists), u)
    representative = {u: first_of[tuple(units[u] for units in unit_lists)] for u in varying}
    found = []
    for at in varying:
        if at in taken:
            continue
        twins = [u for u in varying if u != at and representative[u] == representative[at]]
        others = [u for u in varying if u != at and representative[u] == u]
        if twins and twins[0] not in others:
            others = sorted(others + twins[:1])
        m = len(others)
        for kind in ("xor", "sum"):
            planes = range(width) if kind == "xor" else range(1)
            rows = []
            for units in unit_lists:
                for k in planes:
                    row = 1 << (m + k)   # the start value's bit k
                    for j, u in enumerate(others):
                        if units[u] >> k & 1:
                            row |= 1 << j
                    rows.append((row, units[at] >> k & 1))
            tried = set()
            for x in _solve_gf2(rows, m):
                covered = tuple(u for j, u in enumerate(others) if x >> j & 1)
                if not covered or covered in tried:
                    continue
                tried.add(covered)
                for negate in ((False,) if kind == "xor" else (False, True)):
                    init = None
                    for units in unit_lists:
                        if kind == "xor":
                            need = units[at]
                            for u in covered:
                                need ^= units[u]
                        else:
                            need = (-units[at] if negate else units[at]) - sum(units[u] for u in covered)
                        need &= mask
                        if init is None:
                            init = need
                        elif need != init:
                            break
                    else:
                        spec = OrderedDict(type=kind)
                        if width == 4:
                            spec["unit"] = "nibble"
                        init = _shape(spec, covered, at, init, kind, unit_lists, set(varying), mask)
                        if kind == "xor" and init == mask:
                            spec["invert"] = True
                        elif init:
                            spec["init"] = init
                        if negate:
                            spec["negate"] = True
                        if msb:
                            spec["bitorder"] = "msb"
                        found.append(spec)
    found.sort(key=_preference)
    return found


def _preference(spec):
    """Prefer no start value, a checksum after its data, no negation, ranges
    and wide ones"""
    covered = _covered(spec)
    return ("init" in spec, spec["at"] < max(covered), "negate" in spec or "invert" in spec,
            "data" in spec, -len(covered))


def _covers(code, a, b):
    """Checksum a covers the unit checksum b writes"""
    covered = set()
    for u in _covered(a):
        covered.update(unit_positions(code, a, u))
    return bool(covered & set(unit_positions(code, b)))


def _circular(code, spec, other):
    """Each covers the other's checksum, e.g. byte 4 = sum of bytes 0-3 and
    byte 0 = negated sum of bytes 1-4: the same relation the other way"""
    return _covers(code, spec, other) and _covers(code, other, spec)


def find_checksums(codes):
    """Search byte and then nibble checksums until no more are found"""
    code = codes[0]   # all codes have the same structure
    checksums = []
    for width in (8, 4):
        # Count units from the start of each frame when a frame ends with a
        # partial unit
        restart = "".join(sorted({s for s in code if s not in "01" and s != SEPARATOR}))
        positions = unit_bits(code, width)
        restarted = unit_bits(code, width, restart=restart)
        if restarted == positions[:len(restarted)] and len(positions) // width == len(restarted) // width:
            restart = ""
        else:
            positions = restarted
        while True:
            # Units already holding a checksum
            written = set()
            for spec in checksums:
                written.update(unit_positions(code, spec))
            taken = {u for u in range(len(positions) // width)
                     if written & set(positions[u * width:(u + 1) * width])}
            candidates = []
            for msb in (False, True):
                unit_lists = [read_units(c, positions, width, msb) for c in codes]
                for spec in _search(unit_lists, width, msb, taken):
                    if restart:
                        spec["restart"] = restart
                    candidates.append(spec)
            candidates = [c for c in candidates
                          if not any(_circular(code, c, other) for other in checksums)]
            if not candidates:
                break
            candidates.sort(key=_preference)   # stable: lowest bit first wins ties
            checksums.append(candidates[0])
    # A checksum covering the unit of another is applied after it
    ordered = []
    for spec in checksums:
        index = len(ordered)
        for i, other in enumerate(ordered):
            if _covers(code, other, spec):
                index = min(index, i)
        ordered.insert(index, spec)
    return ordered


# --------------------------------------------------------------- features

def derive_features(captures, codes, excluded):
    """Returns the feature names in the order to apply them, the features
    {name: {value: overlay}} and the template (the base code)."""
    names = list(captures[0]["features"])
    base = captures[0]["features"]
    base_code = codes[0]

    values = OrderedDict((n, []) for n in names)
    for c in captures:
        for n, v in c["features"].items():
            if v not in values[n]:
                values[n].append(v)

    single = {n: {base[n]: base_code} for n in names}
    for c, code in zip(captures, codes):
        changed = [n for n in names if c["features"][n] != base[n]]
        if len(changed) == 1:
            n = changed[0]
            known = single[n].setdefault(c["features"][n], code)
            if known != code:
                raise Ir2PiirError("%s: two different codes for %s=%s with the other "
                                   "features as in the first capture"
                                   % (c["where"], n, c["features"][n]))
    missing = ["%s=%s" % (n, v) for n in names for v in values[n] if v not in single[n]]
    if missing:
        raise Ir2PiirError("no capture with only one feature changed from the first "
                           "capture for: " + ", ".join(missing))

    diff = {}
    owner_counts = {}
    for n in names:
        for v, code in single[n].items():
            d = {p for p, (a, b) in enumerate(zip(code, base_code)) if a != b and p not in excluded}
            diff[(n, v)] = d
            for p in d:
                owner_counts.setdefault(p, Counter())[n] += 1
    # A bit belongs to the feature with the most values changing it. On a
    # tie, the first feature gets it, unless that makes features override
    # each other (below).
    owner, tied = {}, {}
    for p, counts in owner_counts.items():
        best = max(counts.values())
        candidates = [n for n in names if counts[n] == best]
        owner[p] = candidates[0]
        if len(candidates) > 1:
            tied[p] = candidates

    groups = {(n, v): [i for i, c in enumerate(captures) if c["features"][n] == v]
              for n in names for v in values[n]}

    def assign(owner):
        """The features to apply before each feature, and the bits each
        value sets, for an assignment of bits to features"""
        owned = {n: {p for p, o in owner.items() if o == n} for n in names}
        # A value overrides bits of another feature when it changes them from the
        # base. It also does when the bits stay the same in all captures with
        # this value although the other feature's value there sets them otherwise,
        # which finds overrides that happen to match the base. Such a candidate is
        # only accepted when some of its captures are not explained by another
        # feature's candidate or accepted override: a value may coincide with
        # another one that really forces the bits.
        after = {n: set() for n in names}   # features to apply before n
        positions = {}                       # (feature, value): bits it sets
        overrides = {}                       # bit: [(feature, value)] setting it
        for n in names:
            for v in values[n]:
                positions[(n, v)] = owned[n] | diff[(n, v)]
                for p in diff[(n, v)]:
                    if owner[p] != n:
                        after[n].add(owner[p])
                        overrides.setdefault(p, []).append((n, v))

        def overridden(i, p, n):
            """Bit p of capture i is set by an accepted override of another feature"""
            return any(captures[i]["features"][m] == w for m, w in overrides.get(p, []) if m != n)

        # Repeated, since an accepted override can hide another: e.g. a fan mode
        # forcing a temperature is only seen in captures without an option that
        # forces the temperature again
        while True:
            candidates = []
            for n in names:
                for v in values[n]:
                    for g in names:
                        if g == n:
                            continue
                        for p in sorted(owned[g] - positions[(n, v)]):
                            group = [i for i in groups[(n, v)] if not overridden(i, p, n)]
                            found = {codes[i][p] for i in group}
                            if len(found) != 1:
                                continue
                            constant = found.pop()
                            contra = [i for i in group
                                      if single[g][captures[i]["features"][g]][p] != constant]
                            if contra:
                                candidates.append((len(contra), n, v, g, p, contra))
            if not candidates:
                break
            candidates.sort(key=lambda c: -c[0])
            # A candidate whose captures all hold a value of another feature that
            # is also a candidate (or accepted) for the bit is a coincidence
            others = {}
            for _, n, v, g, p, contra in candidates:
                others.setdefault(p, []).append((n, v))
            def covered(i, p, n):
                return overridden(i, p, n) or any(
                    captures[i]["features"][m] == w for m, w in others[p] if m != n)
            accept = [c for c in candidates if not all(covered(i, c[4], c[1]) for i in c[5])]
            for _, n, v, g, p, contra in accept or candidates[:1]:
                positions[(n, v)].add(p)
                overrides.setdefault(p, []).append((n, v))
                after[n].add(g)

        # A value that changes bits of another feature from the base code may
        # only do so together with a third feature's values, e.g. a mode
        # forcing the fan speed only while the power is on. Then the bits
        # vary among its captures: they become conditional overrides.
        conditional = {}   # (feature, value, other feature, its value): {bit: symbol}
        for n in names:
            for v in values[n]:
                for p in sorted(diff[(n, v)]):
                    if owner[p] == n:
                        continue
                    group = [i for i in groups[(n, v)] if not overridden(i, p, n)]
                    if len({codes[i][p] for i in group}) <= 1:
                        continue
                    condition = _condition(captures, codes, names, values, group, p,
                                           (n, owner[p]))
                    if not condition:
                        continue
                    m, when = condition
                    positions[(n, v)].discard(p)
                    for w, symbol in when.items():
                        conditional.setdefault((n, v, m, w), {})[p] = symbol
        # Under the same condition, the other bits of a feature it forces may
        # be forced to the base's symbols, which changes nothing from the base
        for (n, v, m, w), bits in conditional.items():
            group = [i for i in groups[(n, v)] if captures[i]["features"][m] == w]
            for g in {owner[p] for p in bits}:
                for p in sorted(owned[g] - set(bits) - positions[(n, v)]):
                    sub = [i for i in group if not overridden(i, p, n)]
                    found = {codes[i][p] for i in sub}
                    if len(found) == 1:
                        symbol = found.pop()
                        if any(single[g][captures[i]["features"][g]][p] != symbol for i in sub):
                            bits[p] = symbol
        return after, positions, conditional

    after, positions, conditional = assign(owner)
    cycle = _cycle(names, after)
    # Two features each changing a bit once, e.g. a mode bit that is also set
    # by power off: give the bit to the other feature if that breaks a cycle
    for p in sorted(tied):
        if not cycle:
            break
        for other in tied[p]:
            if other == owner[p] or other not in cycle or owner[p] not in cycle:
                continue
            trial = dict(owner)
            trial[p] = other
            trial_after, trial_positions, trial_conditional = assign(trial)
            trial_cycle = _cycle(names, trial_after)
            if len(trial_cycle) < len(cycle):
                owner, after, positions, conditional, cycle = (
                    trial, trial_after, trial_positions, trial_conditional, trial_cycle)
                break

    features = OrderedDict()
    for n in names:
        features[n] = OrderedDict()
        for v in values[n]:
            code = single[n][v]
            features[n][v] = "".join(code[p] if p in positions[(n, v)] else " "
                                     for p in range(len(code)))

    # Stable topological order
    if cycle:
        print("warning: features override each other: %s" % ", ".join(cycle), file=sys.stderr)
    order, placed = [], set()
    while len(order) < len(names):
        ready = [n for n in names if n not in placed and after[n] <= placed]
        if not ready:
            ready = [n for n in names if n not in placed][:1]
        order.append(ready[0])
        placed.add(ready[0])

    override_list = []
    for (n, v, m, w), bits in conditional.items():
        override_list.append(OrderedDict([
            ("when", OrderedDict([(n, v), (m, w)])),
            ("code", "".join(bits.get(p, " ") for p in range(len(base_code))).rstrip())]))
    return OrderedDict((n, features[n]) for n in order), base_code, override_list


def _condition(captures, codes, names, values, group, p, exclude):
    """A feature whose values decide whether bit p is forced in the captures
    of group: returns it and {value: forced symbol} for the values where p
    is the same in all those captures, or None"""
    for m in names:
        if m in exclude:
            continue
        forced, free = {}, False
        for w in values[m]:
            found = {codes[i][p] for i in group if captures[i]["features"][m] == w}
            if len(found) == 1:
                forced[w] = found.pop()
            elif found:
                free = True
        if forced and free:
            return m, forced
    return None


def _cycle(names, after):
    """The features left when ordering them by "after" gets stuck"""
    placed = set()
    while True:
        ready = [n for n in names if n not in placed and after[n] <= placed]
        if not ready:
            return [n for n in names if n not in placed]
        placed.update(ready)


def encode(template, features, checksums, wanted, overrides=()):
    """Encode like PIIR: overlay the features in their order, then the
    overrides whose features are all wanted, then checksums"""
    code = list(template)
    overlays = [values.get(wanted.get(name)) for name, values in features.items()]
    overlays += [o["code"] for o in overrides
                 if all(wanted.get(n) == v for n, v in o["when"].items())]
    for overlay in overlays:
        if overlay is None:
            continue
        for i, s in enumerate(overlay[:len(code)]):
            if s != " ":
                code[i] = s
    return apply_checksums("".join(code), checksums)


def pulses(code, symboldefinitions):
    """The pulses and spaces PIIR sends for a code (src/lirc.c)"""
    values = []
    for s in code:
        for is_mark, d in ((True, symboldefinitions[s]["mark"]),
                           (False, symboldefinitions[s]["space"])):
            if d == 0 or (not values and not is_mark):
                continue
            if values and (len(values) % 2 == 1) == is_mark:
                values[-1] += d
            else:
                values.append(d)
    if len(values) % 2 == 0:
        values.pop()   # the signal ends with a pulse
    return values


def check_remote(remote, captures, tolerance=0.2):
    """The captures the remote does not send within tolerance"""
    features = remote["features"]
    failures = []
    for c in captures:
        unknown = [n for n, v in c["features"].items() if v not in features.get(n, {})]
        sent = pulses(encode(remote["template"], features, remote.get("checksums", []),
                             c["features"], remote.get("overrides", [])),
                      remote["symboldefinitions"])
        recorded = c["durations"][:len(c["durations"]) // 2 * 2 + 1]  # without a final space
        if unknown or len(sent) != len(recorded) or any(
                abs(a - b) > tolerance * max(a, b) for a, b in zip(sent, recorded)):
            failures.append(c)
    return failures


# ----------------------------------------------------------------- output

def to_json(obj, indent=0):
    """JSON with objects and lists of plain values on one line"""
    pad = "    " * (indent + 1)
    plain = lambda v: not isinstance(v, (dict, list))
    if isinstance(obj, dict) and obj and not all(plain(v) for v in obj.values()):
        items = ["%s%s: %s" % (pad, json.dumps(k), to_json(v, indent + 1)) for k, v in obj.items()]
        return "{\n" + ",\n".join(items) + "\n" + "    " * indent + "}"
    if isinstance(obj, list) and obj and not all(plain(v) for v in obj):
        items = [pad + to_json(v, indent + 1) for v in obj]
        return "[\n" + ",\n".join(items) + "\n" + "    " * indent + "]"
    return json.dumps(obj)


def build_remote(captures, description, dutycycle=0.5, outpin=17, tolerance=0.2,
                 test_count=30, separators=True):
    """Returns the remote and the captures it does not reproduce"""
    symbols, codes = derive_symbols(captures, tolerance)
    if separators:
        codes = [add_separators(code) for code in codes]
    check_structure(captures, codes)

    checksums = find_checksums(codes)
    excluded = set()
    for spec in checksums:
        excluded.update(unit_positions(codes[0], spec))
    features, template, overrides = derive_features(captures, codes, excluded)

    failures = []
    for c, code in zip(captures, codes):
        got = encode(template, features, checksums, c["features"], overrides)
        if got != code:
            failures.append((c, code, got))

    frequency = Counter(c["frequency"] for c in captures).most_common(1)[0][0]
    symboldefinitions = OrderedDict()
    if separators:
        symboldefinitions[SEPARATOR] = OrderedDict(mark=0, space=0)
    for name, (mark, space) in symbols.items():
        symboldefinitions[name] = OrderedDict(mark=mark, space=space)

    step = max(1, len(captures) // test_count) if test_count else 0
    tests = []
    if step:
        for i in range(0, len(captures), step):
            if len(tests) == test_count:
                break
            if any(f[0] is captures[i] for f in failures):
                continue
            tests.append(OrderedDict([
                ("description", "Capture %d" % (i + 1)),
                ("features", captures[i]["features"]),
                ("outcome", codes[i])]))

    remote = OrderedDict()
    remote["description"] = description
    remote["frequency"] = frequency
    remote["dutycycle"] = dutycycle
    remote["transmitter"] = OrderedDict(outpin=outpin)
    remote["symboldefinitions"] = symboldefinitions
    if checksums:
        remote["checksums"] = checksums
    remote["template"] = template
    remote["features"] = features
    if overrides:
        remote["overrides"] = overrides
    if tests:
        remote["tests"] = tests
    return remote, failures


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                epilog="See the module documentation for the capture format.")
    p.add_argument("captures", nargs="+", help="capture files (JSON Lines)")
    p.add_argument("-o", "--output", help="remote file to write (default: standard output)")
    p.add_argument("--description", default="Remote derived from captures by ir2piir")
    p.add_argument("--dutycycle", type=float, default=0.5)
    p.add_argument("--outpin", type=int, default=17)
    p.add_argument("--tolerance", type=float, default=0.2,
                   help="relative difference of durations still counted as the same (default 0.2)")
    p.add_argument("--tests", type=int, default=30,
                   help="number of captures to store as test vectors (default 30)")
    p.add_argument("--check", metavar="FILE", action="append", default=[],
                   help="also check the remote against these captures, which are not "
                        "used to derive it")
    p.add_argument("--no-separators", dest="separators", action="store_false",
                   help="do not insert '-' between bytes")
    args = p.parse_args(argv)

    try:
        captures = load_captures(args.captures)
        remote, failures = build_remote(captures, args.description, args.dutycycle,
                                        args.outpin, args.tolerance, args.tests,
                                        args.separators)
        held_out = load_captures(args.check) if args.check else []
    except Ir2PiirError as e:
        print("ir2piir: %s" % e, file=sys.stderr)
        return 2

    text = to_json(remote) + "\n"
    if args.output:
        with open(args.output, "w") as f:
            f.write(text)
    else:
        sys.stdout.write(text)

    checksums = remote.get("checksums", [])
    print("%d captures, %d symbols, %d checksums, %d features: %s" % (
        len(captures), len(remote["symboldefinitions"]), len(checksums),
        len(remote["features"]), ", ".join(remote["features"])), file=sys.stderr)
    for spec in checksums:
        print("  checksum %s" % json.dumps(spec), file=sys.stderr)
    result = 0
    if failures:
        print("%d of %d captures are not reproduced:" % (len(failures), len(captures)),
              file=sys.stderr)
        for c, code, got in failures[:10]:
            print("  %s %s\n    expected %s\n    got      %s" % (
                c["where"], dict(c["features"]), code, got), file=sys.stderr)
        result = 1
    else:
        print("all captures reproduced", file=sys.stderr)
    if held_out:
        wrong = check_remote(remote, held_out, args.tolerance)
        print("%d of %d checked captures are not sent by the remote%s" % (
            len(wrong), len(held_out), ":" if wrong else ""), file=sys.stderr)
        for c in wrong[:10]:
            print("  %s %s" % (c["where"], dict(c["features"])), file=sys.stderr)
        if wrong:
            result = 1
    return result


if __name__ == "__main__":
    sys.exit(main())
