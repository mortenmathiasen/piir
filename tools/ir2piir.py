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
  2. checksums are searched among bytes and nibbles: sums, negated sums,
     XORs and counts of set bits of other bytes or nibbles, adjacent or not,
     and CRCs of the units before them (see "Checksums" in README.md)
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
import itertools
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


def _at(spec):
    """The units a checksum writes its result into"""
    return spec["at"] if isinstance(spec["at"], list) else [spec["at"]]


def _reflect(value, width):
    return int(format(value, "0%db" % width)[::-1], 2)


def checksum_value(spec, values, width):
    """The result of a checksum over the unit values, as src/checksum.c"""
    result_width = width * len(_at(spec))
    mask = (1 << result_width) - 1
    kind = spec["type"]
    value = spec.get("init", 0)
    if kind == "crc":
        reflected = spec.get("reflect", False)
        poly = _reflect(spec["poly"], result_width) if reflected else spec["poly"]
        if reflected:
            value = _reflect(value, result_width)
        for u in values:
            for i in range(width):
                if reflected:
                    bit = (value ^ (u >> i)) & 1
                    value >>= 1
                else:
                    bit = (value >> (result_width - 1) ^ u >> (width - 1 - i)) & 1
                    value = (value << 1) & mask
                if bit:
                    value ^= poly
    else:
        for u in values:
            if kind == "sum":
                value += u
            elif kind == "xor":
                value ^= u
            else:
                value += bin(u).count("1")
    if spec.get("invert"):
        value = ~value
    if spec.get("negate"):
        value = -value
    return (value ^ spec.get("xorout", 0)) & mask


def apply_checksums(code, checksums):
    """Apply checksums like PIIR does (src/checksum.c)"""
    code = list(code)
    for spec in checksums:
        width = 4 if spec.get("unit") == "nibble" else 8
        msb = spec.get("bitorder") == "msb"
        bits = spec.get("bits", "01")
        positions = unit_bits(code, width, bits, spec.get("restart", ""))
        units = read_units("".join("1" if s == bits[1] else "0" for s in code),
                           positions, width, msb)
        value = checksum_value(spec, [units[u] for u in _covered(spec)], width)
        at = _at(spec)
        for k, unit in enumerate(at):
            part = value >> width * (len(at) - 1 - k if msb else k)
            for i in range(width):
                bit = part >> (width - 1 - i if msb else i) & 1
                code[positions[unit * width + i]] = bits[bit]
    return "".join(code)


def unit_positions(code, spec, unit=None):
    """The symbols of a checksum's unit (by default the ones it writes)"""
    width = 4 if spec.get("unit") == "nibble" else 8
    positions = unit_bits(code, width, spec.get("bits", "01"), spec.get("restart", ""))
    units = _at(spec) if unit is None else [unit]
    return [p for u in units for p in positions[u * width:(u + 1) * width]]


# A count of set bits covers this many changing units at least: over fewer,
# e.g. the nibbles of one feature, it often holds by chance
MIN_POPCOUNT_UNITS = 3
# and its result takes this many values at least: any relation with two
# values holds for some count
MIN_POPCOUNT_RESULTS = 4

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


def _popcount(value):
    return bin(value).count("1")


def _shape(spec, covered, at, init, kind, unit_lists, varying, mask, goals=(0,)):
    """Write the covered units as a range when they are contiguous, extended
    over constant units when that makes the start value 0 (or another of
    goals), else as a list. A count of set bits may also cover the constant
    units on both sides of its result, as a list."""
    at_units = at if isinstance(at, list) else [at]
    first = unit_lists[0]
    if kind == "sum":
        combine = lambda a, b: (a - b) & mask
    elif kind == "popcount":
        combine = lambda a, b: (a - _popcount(b)) & mask
    else:
        combine = lambda a, b: a ^ b

    def extend(lo, hi, init, skip):
        """The widest range from lo to hi or beyond over constant units (and
        skip) that reaches a goal, as (from, to, init), or None"""
        lo_min, hi_max = lo, hi
        while lo_min > 0 and lo_min - 1 not in varying and (
                lo_min - 1 not in at_units or lo_min - 1 in skip):
            lo_min -= 1
        while hi_max < len(first) - 1 and hi_max + 1 not in varying and (
                hi_max + 1 not in at_units or hi_max + 1 in skip):
            hi_max += 1
        best = None
        for a in range(lo, lo_min - 1, -1):
            for b in range(hi, hi_max + 1):
                value = init
                for u in list(range(a, lo)) + list(range(hi + 1, b + 1)):
                    if u not in skip:
                        value = combine(value, first[u])
                if value in goals and (best is None or b - a > best[1] - best[0]):
                    best = (a, b, value)
        return best

    lo, hi = min(covered), max(covered)
    inside = [u for u in range(lo, hi + 1) if u in varying or u in at_units]
    if set(inside) <= set(covered):
        # The range also covers the constant units between the covered ones
        for u in range(lo, hi + 1):
            if u not in covered:
                init = combine(init, first[u])
        best = (lo, hi, init) if init in goals else extend(lo, hi, init, ()) or (lo, hi, init)
        if kind == "popcount" and best[2] not in goals:
            span_lo, span_hi = min(lo, *at_units), max(hi, *at_units)
            if all(u in covered or u in at_units or u not in varying
                   for u in range(span_lo, span_hi + 1)):
                start = init
                for u in list(range(span_lo, lo)) + list(range(hi + 1, span_hi + 1)):
                    if u not in at_units:
                        start = combine(start, first[u])
                crossing = extend(span_lo, span_hi, start, at_units)
                if crossing:
                    spec["data"] = [u for u in range(crossing[0], crossing[1] + 1)
                                    if u not in at_units]
                    spec["at"] = at
                    return crossing[2]
        spec["from"], spec["to"] = best[0], best[1]
        init = best[2]
    else:
        spec["data"] = sorted(covered)
    spec["at"] = at
    return init


def _result(units, at, width, msb):
    """The value of the result units at, read as one number"""
    value = 0
    for k, u in enumerate(at):
        value |= units[u] << width * (len(at) - 1 - k if msb else k)
    return value


def _search(unit_lists, width, msb, taken):
    """Find checksums among the units of every capture: for each unit, the
    other units whose sum, XOR or count of set bits it is (for a count also
    for two adjacent units, read as one number). The covered units are found
    by solving the lowest bits over GF(2) (for XOR every bit, for a count the
    parity of each unit), then verified on every capture. Returns candidates
    sorted by preference."""
    n = len(unit_lists[0])
    varying = [u for u in range(n) if len({units[u] for units in unit_lists}) > 1]
    # Units equal in every capture, e.g. a frame sent twice, are one unknown:
    # otherwise a solution may count a unit and its copy, which cancel in the
    # lowest bits but not in a sum
    first_of = {}
    for u in varying:
        first_of.setdefault(tuple(units[u] for units in unit_lists), u)
    representative = {u: first_of[tuple(units[u] for units in unit_lists)] for u in varying}
    varying_set = set(varying)
    targets = [([at], kind) for at in varying for kind in ("xor", "sum", "popcount")]
    targets += [([at, at + 1], "popcount") for at in varying if at + 1 in varying]
    counts = [[_popcount(v) for v in units] for units in unit_lists]
    found = []
    for at, kind in targets:
        if any(u in taken for u in at):
            continue
        mask = (1 << width * len(at)) - 1
        results = ([units[at[0]] for units in unit_lists] if len(at) == 1 else
                   [_result(units, at, width, msb) for units in unit_lists])
        if kind == "popcount" and len(set(results)) < MIN_POPCOUNT_RESULTS:
            continue
        values = counts if kind == "popcount" else unit_lists
        # A copy points to an earlier twin (its representative), never to a
        # later one: in a frame sent twice the second frame is the copy
        others = [u for u in varying if u not in at and representative[u] == u]
        m = len(others)
        planes = range(width) if kind == "xor" else range(1)
        rows = []
        for units, result in zip(values, results):
            for k in planes:
                row = 1 << (m + k)   # the start value's bit k
                for j, u in enumerate(others):
                    if units[u] >> k & 1:
                        row |= 1 << j
                rows.append((row, result >> k & 1))
        tried = set()
        for x in _solve_gf2(rows, m):
            covered = tuple(u for j, u in enumerate(others) if x >> j & 1)
            if not covered or covered in tried:
                continue
            tried.add(covered)
            if kind == "popcount" and len(covered) < MIN_POPCOUNT_UNITS:
                continue
            if kind == "popcount":
                # A count may cover any copy of a unit: the one nearest the result
                covered = tuple(sorted(min(
                    (v for v in varying if v not in at and representative[v] == representative[u]),
                    key=lambda v: (abs(v - at[0]), v)) for u in covered))
            for negate in ((False,) if kind == "xor" else (False, True)):
                init = None
                for units, result in zip(values, results):
                    if kind == "xor":
                        need = result
                        for u in covered:
                            need ^= units[u]
                    else:
                        need = (-result if negate else result) - sum(units[u] for u in covered)
                    need &= mask
                    if init is None:
                        init = need
                    elif need != init:
                        break
                else:
                    spec = _Spec(type=kind)
                    spec.varying = sum(1 for u in covered if u in varying_set)
                    if width == 4:
                        spec["unit"] = "nibble"
                    goals = (0, 1) if kind == "popcount" and negate else (0,)
                    init = _shape(spec, covered, at if len(at) > 1 else at[0], init, kind,
                                  unit_lists, varying_set, mask, goals)
                    if kind == "xor" and init == mask:
                        spec["invert"] = True
                    elif kind == "popcount" and negate and init == 1:
                        spec["invert"] = True   # ~x is 0 - (1 + x)
                        negate = False
                    elif init:
                        spec["init"] = init
                    if negate:
                        spec["negate"] = True
                    if msb:
                        spec["bitorder"] = "msb"
                    found.append(spec)
    explained = {u for spec in found for u in _at(spec)}
    found += _search_crc(unit_lists, width, msb, taken, varying, explained)
    found.sort(key=_preference)
    return found


def _crc_columns(poly, width, unit_width, reflected, count):
    """The linear part of a CRC (start value 0): columns[d][k] is the
    register after a unit with only bit k set followed by d units of 0"""
    mask = (1 << width) - 1
    rpoly = _reflect(poly, width) if reflected else poly

    def feed(crc, u):
        for i in range(unit_width):
            if reflected:
                bit = (crc ^ (u >> i)) & 1
                crc >>= 1
            else:
                bit = (crc >> (width - 1) ^ u >> (unit_width - 1 - i)) & 1
                crc = (crc << 1) & mask
            if bit:
                crc ^= rpoly
        return crc
    row = [feed(0, 1 << k) for k in range(unit_width)]
    columns = [row]
    for _ in range(count - 1):
        row = [feed(c, 0) for c in row]
        columns.append(row)
    return columns


def _affine(unit_lists, at, sources, width):
    """Whether every bit of unit at is an XOR of bits of the source units
    plus a constant in every capture (a CRC or XOR is)"""
    m = len(sources) * width
    for k in range(width):
        rows = []
        for units in unit_lists:
            row = 1 << m
            for j, u in enumerate(sources):
                row |= units[u] << j * width
            rows.append((row, units[at] >> k & 1))
        if next(_solve_gf2(rows, m), None) is None:
            return False
    return True


def _search_crc(unit_lists, width, msb, taken, varying, skip):
    """Find CRCs of width bits over the units up to and including the one
    before unit at, for the units at not in skip. The units at must be an
    affine function of the earlier units; every polynomial is then tried on
    a few captures, and those left are verified on every capture."""
    mask = (1 << width) - 1
    found = []
    first = unit_lists[0]
    sample = unit_lists[:16]
    for at in varying:
        if at in taken or at in skip or at == 0:
            continue
        sources = [u for u in varying if u < at]
        if not sources or not _affine(unit_lists, at, sources, width):
            continue
        for end in sorted({at - 1, sources[-1]}, reverse=True):
            # Diffs of the bits from the first capture, as (unit, bit) lists
            diffs = [[(u, k) for u in range(end + 1) for k in range(width)
                      if (units[u] ^ first[u]) >> k & 1] for units in unit_lists]
            for reflected in (False, True):
                for poly in range(1, mask + 1):
                    columns = _crc_columns(poly, width, width, reflected, end + 1)
                    for start in sorted({0} | {u for u in sources if u <= end}):
                        # The linear part must give the change of the result
                        if any(_xor_columns(columns, end, diff, start) != units[at] ^ first[at]
                               for diff, units in zip(diffs[:16], sample)):
                            continue
                        if any(_xor_columns(columns, end, diff, start) != units[at] ^ first[at]
                               for diff, units in zip(diffs, unit_lists)):
                            continue
                        found.append(_crc_spec(poly, reflected, start, end, at, width, msb,
                                               first, sum(1 for u in sources if u >= start)))
    return found


def _xor_columns(columns, end, diff, start):
    value = 0
    for u, k in diff:
        if u >= start:
            value ^= columns[end - u][k]
    return value


def _crc_spec(poly, reflected, start, end, at, width, msb, first, varying):
    """A CRC found, with a start value making the final XOR 0 if there is
    one (preferring both all ones), else a final XOR"""
    mask = (1 << width) - 1
    spec = _Spec(type="crc")
    spec.varying = varying
    if width == 4:
        spec["unit"] = "nibble"
    spec["poly"] = poly
    spec["from"], spec["to"], spec["at"] = start, end, at
    values = [first[u] for u in range(start, end + 1)]
    sends = lambda init, xorout: checksum_value(
        dict(spec, init=init, xorout=xorout, reflect=reflected), values, width) == first[at]
    init = next((i for i in range(mask + 1) if sends(i, 0)), None)
    if init and sends(mask, mask):
        spec["init"] = spec["xorout"] = mask   # e.g. CRC-8/SAE-J1850
    elif init:
        spec["init"] = init
    elif init is None:
        spec["xorout"] = checksum_value(dict(spec, reflect=reflected), values, width) ^ first[at]
    if reflected:
        spec["reflect"] = True
    if msb:
        spec["bitorder"] = "msb"
    return spec


class _Spec(OrderedDict):
    """A checksum found, remembering how many changing units it covers"""
    varying = 0


def _preference(spec):
    """Prefer other checksums to CRCs, no start value or final XOR, results
    of more units, no reflected CRC, sums and XORs to counts of set bits, a
    checksum after its data, no negation, ranges, and the
    fewest changing units covered: a relation over more units may hold only
    through another checksum (e.g. a copy of a byte also equals the XOR of
    the bytes around it and their checksum)"""
    covered = _covered(spec)
    return (spec["type"] == "crc", "init" in spec or "xorout" in spec, -len(_at(spec)),
            "reflect" in spec, spec["type"] == "popcount",
            min(_at(spec)) < max(covered), "negate" in spec or "invert" in spec,
            "data" in spec, getattr(spec, "varying", len(covered)))


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
        # A conditional override can also match the base code in all its
        # bits, e.g. a fan mode forcing the base temperature only while the
        # power is on. Then the bits are the same in all captures with both
        # values, although the other feature's values there set them
        # otherwise. Bits set by accepted overrides are not counted.
        def condition_holds(i, key):
            n2, v2, m2, w2 = key
            f = captures[i]["features"]
            return f[n2] == v2 and f[m2] == w2

        def explained(i, p, n, exclude=()):
            """Bit p of capture i is set by an override of another feature
            or by a conditional override already found"""
            return overridden(i, p, n) or any(
                p in bits and key not in exclude and condition_holds(i, key)
                for key, bits in conditional.items())

        conditioned = {}
        for (n, v, m, w) in conditional:
            conditioned.setdefault((n, v), set()).update(conditional[(n, v, m, w)])
        candidates = []
        for n in names:
            for v in values[n]:
                for g in names:
                    if g == n:
                        continue
                    for p in sorted(owned[g] - positions[(n, v)] - conditioned.get((n, v), set())):
                        group = [i for i in groups[(n, v)] if not explained(i, p, n)]
                        if len({codes[i][p] for i in group}) <= 1:
                            continue   # unconditional, or not forced
                        # The condition may be the forced feature's own value,
                        # e.g. a temperature only some modes accept
                        for m in names:
                            if m == n:
                                continue
                            for w in values[m]:
                                sub = [i for i in group if captures[i]["features"][m] == w]
                                found = {codes[i][p] for i in sub}
                                if len(found) != 1:
                                    continue
                                symbol = found.pop()
                                contra = [i for i in sub
                                          if single[g][captures[i]["features"][g]][p] != symbol]
                                if len(contra) >= 2:
                                    candidates.append(((n, v, m, w), p, symbol, contra))
        # A candidate whose captures all match another candidate's condition
        # for the same bit is a coincidence (the same condition seen from the
        # other side counts as the same).
        by_bit = {}
        for key, p, symbol, contra in candidates:
            by_bit.setdefault(p, []).append(key)
        for key, p, symbol, contra in candidates:
            n, v, m, w = key
            mirror = (m, w, n, v)
            rivals = [k for k in by_bit[p] if k not in (key, mirror)]
            if all(any(condition_holds(i, k) for k in rivals) for i in contra):
                continue
            target = mirror if mirror in conditional else key
            conditional.setdefault(target, {})[p] = symbol
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
        sent = pulses(encode_remote(remote, c["features"]), remote["symboldefinitions"])
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


def _structure(code):
    return "".join("b" if s in "01" else s for s in code)


def _selector(captures, members):
    """The feature whose values in the captures numbered in members never
    occur in the others, with the fewest such values: (name, values) or None"""
    inside = set(members)
    best = None
    for name in captures[0]["features"]:
        values_in = {captures[i]["features"][name] for i in inside}
        values_out = {c["features"][name] for i, c in enumerate(captures) if i not in inside}
        if not values_in & values_out and (best is None or len(values_in) < len(best[1])):
            best = (name, values_in)
    if best is None:
        return None
    ordered = []
    for c in captures:
        v = c["features"][best[0]]
        if v in best[1] and v not in ordered:
            ordered.append(v)
    return best[0], ordered


def _derive_definition(captures, codes):
    """The template, features, checksums and overrides for captures whose
    codes have the same structure. A single code is a fixed template."""
    definition = OrderedDict()
    if len(set(codes)) == 1:
        definition["template"] = codes[0]
        return definition
    checksums = find_checksums(codes)
    excluded = set()
    for spec in checksums:
        excluded.update(unit_positions(codes[0], spec))
    features, template, overrides = derive_features(captures, codes, excluded)
    if checksums:
        definition["checksums"] = checksums
    definition["template"] = template
    definition["features"] = features
    if overrides:
        definition["overrides"] = overrides
    _repair(definition, captures, codes, excluded)
    return definition


# The most features an override found by _repair() may depend on
MAX_CONDITION = 3


def _repair(definition, captures, codes, excluded):
    """Add overrides for the bits the definition still gets wrong: for each,
    the fewest feature values (up to MAX_CONDITION) under which the bit is
    the same in every capture, held by at least two captures. Such an
    override only sets what was recorded, so the order of overrides does not
    matter."""
    names = list(captures[0]["features"])
    overrides = definition.setdefault("overrides", [])
    matching = {}

    def captures_with(condition):
        if condition not in matching:
            matching[condition] = [i for i, c in enumerate(captures)
                                   if all(c["features"][n] == v for n, v in condition)]
        return matching[condition]

    for _ in range(10):
        wrong = []
        for i, c in enumerate(captures):
            got = encode(definition["template"], definition.get("features", {}), [],
                         c["features"], overrides)
            wrong += [(i, p) for p, (a, b) in enumerate(zip(got, codes[i]))
                      if a != b and p not in excluded]
        if not wrong:
            break
        fixes = OrderedDict()
        for i, p in wrong:
            items = [(n, captures[i]["features"][n]) for n in names]
            best = None
            for size in range(1, MAX_CONDITION + 1):
                for condition in itertools.combinations(items, size):
                    members = captures_with(condition)
                    if len(members) < 2 or len({codes[j][p] for j in members}) != 1:
                        continue
                    if best is None or len(members) > len(captures_with(best)):
                        best = condition
                if best:
                    break
            if best:
                fixes.setdefault(best, {})[p] = codes[i][p]
        if not fixes:
            break
        for condition, bits in fixes.items():
            overrides.append(OrderedDict([
                ("when", OrderedDict(condition)),
                ("code", "".join(bits.get(p, " ") for p in range(len(codes[0]))).rstrip())]))
    if not overrides:
        del definition["overrides"]


def _hard_coded(captures, codes):
    """A definition sending the recorded codes of exactly these captures"""
    definition = OrderedDict(template=codes[0])
    definition["overrides"] = [OrderedDict([("when", OrderedDict(c["features"])), ("code", code)])
                               for c, code in zip(captures, codes)]
    return definition


def select_definition(remote, wanted):
    """The first variant whose "when" features are all wanted, else the remote"""
    for variant in remote.get("variants", []):
        if all(wanted.get(n) == v for n, v in variant["when"].items()):
            return variant
    return remote


def encode_remote(remote, wanted):
    """Encode like PIIR, including variants"""
    definition = select_definition(remote, wanted)
    return encode(definition["template"], definition.get("features", {}),
                  definition.get("checksums", []), wanted, definition.get("overrides", []))


def build_remote(captures, description, dutycycle=0.5, outpin=17, tolerance=0.2,
                 test_count=30, separators=True, fallback=False):
    """Returns the remote and the captures it does not reproduce. Codes of
    different structures (e.g. a short power off code) become variants,
    selected by the feature values only their captures have. With fallback,
    the codes of captures the remote does not reproduce are hard-coded."""
    symbols, codes = derive_symbols(captures, tolerance)
    if separators:
        codes = [add_separators(code) for code in codes]

    groups = OrderedDict()
    for i, code in enumerate(codes):
        groups.setdefault(_structure(code), []).append(i)
    ordered = sorted(groups.values(), key=lambda g: (-len(g), 0 not in g))
    main_group, others = ordered[0], ordered[1:]

    def derive(members, what):
        cs = [captures[i] for i in members]
        cc = [codes[i] for i in members]
        try:
            return _derive_definition(cs, cc)
        except Ir2PiirError as e:
            if not fallback:
                raise Ir2PiirError("%s: %s" % (what, e))
            print("warning: %s: %s; hard-coding its %d captures" % (what, e, len(cs)),
                  file=sys.stderr)
            return _hard_coded(cs, cc)

    variants = []
    for members in others:
        selector = _selector(captures, members)
        if selector:
            name, values = selector
            definition = derive(members, "codes with %s=%s" % (name, "/".join(values)))
            for v in values:
                variant = OrderedDict(when=OrderedDict([(name, v)]))
                variant.update(definition)
                variants.append(variant)
        elif fallback:
            for i in members:
                variant = OrderedDict(when=OrderedDict(captures[i]["features"]))
                variant["template"] = codes[i]
                variants.append(variant)
        else:
            raise Ir2PiirError(
                "%d codes have another structure than the first capture, and no feature "
                "value selects them (codes of different lengths need one, or --fallback); "
                "e.g. %s:\n  %s\n  %s" % (len(members), captures[members[0]]["where"],
                                            codes[main_group[0]], codes[members[0]]))
    main = derive(main_group, "codes of the most common structure")

    # Every value is known at the top, also those only selecting variants
    features = main.setdefault("features", OrderedDict())
    for c in captures:
        for n, v in c["features"].items():
            features.setdefault(n, OrderedDict()).setdefault(v, "")

    remote = OrderedDict()
    remote["description"] = description
    remote["frequency"] = Counter(c["frequency"] for c in captures).most_common(1)[0][0]
    remote["dutycycle"] = dutycycle
    remote["transmitter"] = OrderedDict(outpin=outpin)
    symboldefinitions = OrderedDict()
    if separators:
        symboldefinitions[SEPARATOR] = OrderedDict(mark=0, space=0)
    for name, (mark, space) in symbols.items():
        symboldefinitions[name] = OrderedDict(mark=mark, space=space)
    remote["symboldefinitions"] = symboldefinitions
    remote.update(main)
    if variants:
        remote["variants"] = variants

    def failures():
        result = []
        for c, code in zip(captures, codes):
            got = encode_remote(remote, c["features"])
            if got != code:
                result.append((c, code, got))
        return result

    failed = failures()
    if fallback and failed:
        for c, code, _ in failed:
            definition = select_definition(remote, c["features"])
            definition.setdefault("overrides", []).append(
                OrderedDict([("when", OrderedDict(c["features"])), ("code", code)]))
        failed = failures()

    step = max(1, len(captures) // test_count) if test_count else 0
    tests = []
    if step:
        for i in range(0, len(captures), step):
            if len(tests) == test_count:
                break
            if any(f[0] is captures[i] for f in failed):
                continue
            tests.append(OrderedDict([
                ("description", "Capture %d" % (i + 1)),
                ("features", captures[i]["features"]),
                ("outcome", codes[i])]))
    if tests:
        remote["tests"] = tests
    return remote, failed


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
    p.add_argument("--fallback", action="store_true",
                   help="hard-code the codes of captures the remote does not reproduce "
                        "(only these combinations are then sent exactly)")
    p.add_argument("--no-separators", dest="separators", action="store_false",
                   help="do not insert '-' between bytes")
    args = p.parse_args(argv)

    try:
        captures = load_captures(args.captures)
        remote, failures = build_remote(captures, args.description, args.dutycycle,
                                        args.outpin, args.tolerance, args.tests,
                                        args.separators, args.fallback)
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
    variants = remote.get("variants", [])
    if variants:
        print("  %d variants: %s" % (len(variants), ", ".join(
            " ".join("%s=%s" % kv for kv in v["when"].items()) for v in variants[:5])
            + (", ..." if len(variants) > 5 else "")), file=sys.stderr)
    hard = sum(1 for d in [remote] + variants for o in d.get("overrides", [])
               if len(o["when"]) == len(remote["features"]))
    if hard:
        print("  %d hard-coded codes" % hard, file=sys.stderr)
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
