"""Unit tests of tools/ir2irlingo.py (run with PYTHONPATH=tools)"""

import contextlib
import io
import itertools
import json
import os
import random
import tempfile
import unittest
from collections import OrderedDict

import ir2irlingo

SYMBOLS = {"H": (3500, 1750), "0": (435, 435), "1": (435, 1300), "W": (435, 10000)}


def durations(code, symbols=SYMBOLS):
    values = []
    for s in code:
        values += list(symbols[s])
    return values[:-1]   # the final space is not recorded


def capture(features, code, symbols=SYMBOLS):
    return {"features": OrderedDict(features), "durations": durations(code, symbols),
            "frequency": 38000, "where": str(features)}


def bits(value, width=8, msb=False):
    order = range(width - 1, -1, -1) if msb else range(width)
    return "".join(str(value >> i & 1) for i in order)


class ClusterTest(unittest.TestCase):
    def test_groups_within_tolerance(self):
        index, means = ir2irlingo.cluster([430, 440, 435, 1300, 1290, 1750], 0.2)
        self.assertEqual(3, len(means))
        self.assertEqual(index[430], index[440])
        self.assertNotEqual(index[1300], index[1750])
        self.assertEqual(435, means[index[430]])

    def test_weighted_mean(self):
        index, means = ir2irlingo.cluster([400, 400, 400, 460], 0.2)
        self.assertEqual([415], means)


class SymbolTest(unittest.TestCase):
    def test_pulse_distance(self):
        # Codes end with a stop mark, the space after it is not recorded
        captures = [capture({"k": "a"}, "H0110W10000"), capture({"k": "b"}, "H1001W01110")]
        symbols, codes = ir2irlingo.derive_symbols(captures)
        self.assertEqual((435, 435), symbols["0"])
        self.assertEqual((435, 1300), symbols["1"])
        self.assertEqual((3500, 1750), symbols["H"])
        self.assertEqual((435, 10000), symbols["W"])
        # The final mark without its space becomes a symbol with that mark
        self.assertEqual(["H0110W10000", "H1001W01110"], codes)

    def test_pulse_width(self):
        table = {"S": (2400, 600), "0": (600, 600), "1": (1200, 600)}
        captures = [capture({"k": "a"}, "S0110", table), capture({"k": "b"}, "S1001", table)]
        symbols, codes = ir2irlingo.derive_symbols(captures)
        self.assertEqual((600, 600), symbols["0"])
        self.assertEqual((1200, 600), symbols["1"])
        self.assertEqual(["H0110", "H1001"], codes)

    def test_jitter(self):
        jittered = [capture({"k": "a"}, "H0110W10000")]
        jittered[0]["durations"] = [d + random.Random(i).randint(-30, 30)
                                    for i, d in enumerate(jittered[0]["durations"])]
        jittered.append(capture({"k": "b"}, "H1001W01110"))
        symbols, codes = ir2irlingo.derive_symbols(jittered)
        self.assertEqual(["H0110W10000", "H1001W01110"], codes)

    def test_marks_and_spaces_differ(self):
        # The bits differ in both mark and space: the shorter one is 0
        table = {"S": (3000, 3000), "0": (500, 500), "1": (1000, 1500)}
        captures = [capture({"k": "a"}, "S0110", table), capture({"k": "b"}, "S1001", table)]
        symbols, codes = ir2irlingo.derive_symbols(captures)
        self.assertEqual((500, 500), symbols["0"])
        self.assertEqual((1000, 1500), symbols["1"])

    def test_final_mark_of_its_own(self):
        # A final mark no other symbol has becomes a symbol without space
        table = dict(SYMBOLS, E=(2000, 0))
        captures = [capture({"k": "a"}, "H0110E", table), capture({"k": "b"}, "H1001E", table)]
        symbols, codes = ir2irlingo.derive_symbols(captures)
        self.assertEqual(["H0110W", "H1001W"], codes)
        self.assertEqual((2000, 0), symbols["W"])

    def test_one_kind_of_pair(self):
        with self.assertRaises(ir2irlingo.Ir2IrlingoError):
            ir2irlingo.derive_symbols([capture({"k": "a"}, "000")])

    def test_separators(self):
        self.assertEqual("H-00001111-0101-W-1", ir2irlingo.add_separators("H000011110101W1"))
        self.assertEqual("00000000-11111111", ir2irlingo.add_separators("0000000011111111"))


class ChecksumTest(unittest.TestCase):
    def codes(self, make, count=40, seed=1):
        rng = random.Random(seed)
        return [make([rng.randrange(256) for _ in range(4)]) for _ in range(count)]

    def test_byte_sum(self):
        codes = self.codes(lambda d: "H" + "".join(bits(b) for b in d + [sum(d) & 0xff]))
        self.assertEqual([{"type": "sum", "from": 0, "to": 3, "at": 4}],
                         ir2irlingo.find_checksums(codes))

    def test_sum_msb_first_with_start_value(self):
        def make(d):
            return "H" + "".join(bits(b, msb=True) for b in d + [(0x5A + sum(d)) & 0xff])
        self.assertEqual([{"type": "sum", "from": 0, "to": 3, "at": 4, "init": 0x5A,
                           "bitorder": "msb"}],
                         ir2irlingo.find_checksums(self.codes(make)))

    def test_xor(self):
        def make(d):
            x = 0
            for b in d:
                x ^= b
            return "".join(bits(b) for b in d + [x])
        self.assertEqual([{"type": "xor", "from": 0, "to": 3, "at": 4}],
                         ir2irlingo.find_checksums(self.codes(make)))

    def test_negated_sum(self):
        codes = self.codes(lambda d: "".join(bits(b) for b in d + [-sum(d) & 0xff]))
        self.assertEqual([{"type": "sum", "from": 0, "to": 3, "at": 4, "negate": True}],
                         ir2irlingo.find_checksums(codes))

    def test_inverted_copies(self):
        # Every byte followed by its complement
        codes = self.codes(lambda d: "".join(bits(b) + bits(~b & 0xff) for b in d[:2]))
        found = ir2irlingo.find_checksums(codes)
        # Byte 3 may also be found as the XOR of bytes 0-2; either is right
        self.assertEqual([1, 3], sorted(spec["at"] for spec in found))
        for spec in found:
            self.assertLess(spec["to"], spec["at"])
        for code in codes:
            self.assertEqual(code, ir2irlingo.apply_checksums(code, found))

    def test_nibble_sum(self):
        def make(d):
            nibbles = [n for b in d for n in (b & 15, b >> 4)]
            return "".join(bits(b) for b in d) + bits(sum(nibbles) & 15, 4) + "0000"
        self.assertEqual([{"type": "sum", "unit": "nibble", "from": 0, "to": 7, "at": 8}],
                         ir2irlingo.find_checksums(self.codes(make)))

    def test_nibbles_not_next_to_each_other(self):
        # A 4-bit sum of the low nibbles of bytes 0-1 and the high nibble of
        # byte 2, plus 10, in the high nibble of byte 3 (Gree style)
        def make(d):
            check = (10 + (d[0] & 15) + (d[1] & 15) + (d[2] >> 4)) & 15
            return "".join(bits(b) for b in d[:3] + [check << 4 | d[3] & 15])
        self.assertEqual([{"type": "sum", "unit": "nibble", "data": [0, 2, 5], "at": 7,
                           "init": 10}],
                         ir2irlingo.find_checksums(self.codes(make)))

    def test_bytes_not_next_to_each_other(self):
        def make(d):
            return "".join(bits(b) for b in d + [d[0] ^ d[2]])
        self.assertEqual([{"type": "xor", "data": [0, 2], "at": 4}],
                         ir2irlingo.find_checksums(self.codes(make)))

    def test_constant_byte_inside_range(self):
        # Byte 1 never changes, but the checksum covers it
        def make(d):
            data = [d[0], 0x37, d[1], d[2]]
            return "".join(bits(b) for b in data + [sum(data) & 0xff])
        self.assertEqual([{"type": "sum", "from": 0, "to": 3, "at": 4}],
                         ir2irlingo.find_checksums(self.codes(make)))

    def test_frame_sent_twice(self):
        # The sum is found on the first frame, not as a mix of both copies
        def make(d):
            frame = "".join(bits(b) for b in d + [sum(d) & 0xff])
            return frame + "W" + frame
        found = ir2irlingo.find_checksums(self.codes(make))
        self.assertIn({"type": "sum", "from": 0, "to": 3, "at": 4}, found)
        for code in self.codes(make, seed=2):
            broken = code[:32] + ("1" if code[32] == "0" else "0") + code[33:]
            self.assertEqual(code, ir2irlingo.apply_checksums(code, found))
            self.assertNotEqual(broken, ir2irlingo.apply_checksums(broken, found))

    def test_frames_ending_with_a_partial_byte(self):
        # The first frame has 3 extra bits, so bytes are counted again after
        # the gap
        def make(d):
            return ("H" + bits(d[0]) + bits(d[1]) + "010" + "W" + bits(d[2]) +
                    bits(d[0] ^ d[1] ^ d[2]) + "0")
        found = ir2irlingo.find_checksums([ir2irlingo.add_separators(c) for c in self.codes(make)])
        self.assertEqual([{"type": "xor", "from": 0, "to": 2, "at": 3, "restart": "HW"}], found)

    def test_range_over_constant_bytes(self):
        # Bytes 2 and 3 never change but are covered: the range is extended
        # over them so that no start value is needed
        def make(d):
            data = [d[0], d[1], 0x05, 0x00]
            return "".join(bits(b) for b in data + [sum(data) & 0xff])
        self.assertEqual([{"type": "sum", "from": 0, "to": 3, "at": 4}],
                         ir2irlingo.find_checksums(self.codes(make)))

    def test_popcount(self):
        # Samsung FJM style: 28 minus the set bits of bytes 2-3, in the high
        # nibble of byte 1
        def make(d):
            check = (28 - sum(bin(b).count("1") for b in d[2:4])) & 15
            return "".join(bits(b) for b in [d[0], check << 4 | 2, d[2], d[3]])
        found = ir2irlingo.find_checksums(self.codes(make))
        self.assertEqual([{"type": "popcount", "unit": "nibble", "from": 4, "to": 7, "at": 3,
                           "init": 4, "negate": True}], found)

    def test_popcount_into_two_units(self):
        # Samsung AQV style: the complement of the set bits of the other
        # nibbles, its low nibble in nibble 3 and its high one in nibble 4
        def make(d):
            data = [0x02, 0, d[1] & 0xf0, d[2], d[3]]
            check = ~sum(bin(b).count("1") for b in data) & 0xff
            data[1] = (check & 15) << 4
            data[2] |= check >> 4
            return "".join(bits(b) for b in data)
        codes = self.codes(make, count=200)
        found = ir2irlingo.find_checksums(codes)
        self.assertEqual([{"type": "popcount", "unit": "nibble",
                           "data": [0, 1, 2, 5, 6, 7, 8, 9], "at": [3, 4], "invert": True}],
                         found)
        for code in codes:
            self.assertEqual(code, ir2irlingo.apply_checksums(code, found))

    def test_crc(self):
        # CRC-8/MAXIM-DOW (reflected, lowest bit first) is found as the same
        # CRC unreflected with the highest bit first: the same bits are sent
        rng = random.Random(1)
        maxim = {"type": "crc", "poly": 0x31, "from": 0, "to": 3, "at": 4, "reflect": True}
        for spec, expected in (
                ({"type": "crc", "poly": 7, "from": 0, "to": 3, "at": 4, "bitorder": "msb"}, None),
                (maxim, dict(maxim, reflect=False, bitorder="msb")),
                ({"type": "crc", "poly": 0x1D, "from": 0, "to": 3, "at": 4,
                  "init": 0xFF, "xorout": 0xFF, "bitorder": "msb"}, None),
                # Without x^0 in the polynomial no start value may give the
                # result: a final XOR does
                ({"type": "crc", "poly": 6, "from": 0, "to": 3, "at": 4, "xorout": 1,
                  "bitorder": "msb"}, None),
                # The start value 5 over the constant first nibble 5 is the
                # same as no start value without it
                ({"type": "crc", "unit": "nibble", "poly": 3, "from": 0, "to": 5,
                  "at": 6, "init": 5},
                 {"type": "crc", "unit": "nibble", "poly": 3, "from": 1, "to": 5, "at": 6,
                  "reflect": False})):
            width = 4 if spec.get("unit") else 8
            msb = spec.get("bitorder") == "msb"
            codes = []
            for _ in range(40):
                d = [0x5 if width == 4 else 0x5A] + [rng.randrange(1 << width)
                                                     for _ in range(spec["to"])]
                d.append(ir2irlingo.checksum_value(spec, d, width))
                codes.append("".join(bits(b, width, msb) for b in d) + "0" * (8 - width))
            if expected:
                del expected["reflect"]
            found = ir2irlingo.find_checksums(codes)
            self.assertEqual([expected or spec], found)
            for code in codes:
                self.assertEqual(code, ir2irlingo.apply_checksums(code, found))

    def test_none(self):
        self.assertEqual([], ir2irlingo.find_checksums(
            self.codes(lambda d: "".join(bits(b) for b in d))))

    def test_apply_like_irlingo(self):
        # Vectors from tests/test_checksum.c
        apply = ir2irlingo.apply_checksums
        self.assertEqual("10000000-01000000-11000000", apply(
            "10000000-01000000-00000000", [{"type": "sum", "from": 0, "to": 1, "at": 2}]))
        self.assertEqual("0001-1111-0010-1111-0011", apply(
            "0001-1111-0010-1111-0000", [{"type": "sum", "unit": "nibble", "data": [0, 2],
                                          "at": 4, "bitorder": "msb"}]))
        self.assertEqual("00000001-00000010-00000100", apply(
            "00000001-00000010-00000000", [{"type": "sum", "from": 0, "to": 1, "at": 2,
                                            "invert": True, "negate": True,
                                            "bitorder": "msb"}]))
        self.assertEqual("11110000-11000000-01100000", apply(
            "11110000-11000000-11111111", [{"type": "popcount", "from": 0, "to": 1, "at": 2}]))
        self.assertEqual("1110-0101-1111-1100", apply(
            "1110-0000-0000-1100", [{"type": "popcount", "unit": "nibble", "data": [0, 3],
                                     "at": [1, 2], "invert": True}]))
        self.assertEqual("00000001-11111111-00000000-00000001", apply(
            "00000001-11111111-11111111-11111111", [{"type": "sum", "from": 0, "to": 1,
                                                     "at": [3, 2], "bitorder": "msb"}]))
        check = "".join(bits(b, msb=True) for b in b"123456789")
        self.assertEqual(check + bits(0x4B, msb=True), apply(
            check + "0" * 8, [{"type": "crc", "poly": 0x1D, "init": 0xFF, "xorout": 0xFF,
                               "from": 0, "to": 8, "at": 9, "bitorder": "msb"}]))
        check = "".join(bits(b) for b in b"123456789")
        self.assertEqual(check + bits(0x37) + bits(0x4B), apply(
            check + "0" * 16, [{"type": "crc", "poly": 0x8005, "init": 0xFFFF, "reflect": True,
                                "from": 0, "to": 8, "at": [9, 10]}]))


# A small air conditioner: mode and fan nibbles, a byte checksum. Mode DRY
# forces fan 1, which is also the base fan, so the override only shows in
# combinations.
MODES = {"AUTO": 0, "COOL": 1, "DRY": 2}
FANS = {"1": 1, "2": 2, "3": 3}


def aircon(mode, fan):
    if mode == "DRY":
        fan = "1"
    data = [0xA5, MODES[mode] | FANS[fan] << 4]
    return "H" + "".join(bits(b) for b in data + [sum(data) & 0xff]) + "0"  # stop mark


def aircon_captures(combinations=True):
    sets = [("AUTO", "1"), ("COOL", "1"), ("DRY", "1"), ("AUTO", "2"), ("AUTO", "3")]
    if combinations:
        sets += list(itertools.product(MODES, FANS))
    return [capture([("mode", m), ("fan", f)], aircon(m, f)) for m, f in sets]


# DRY forces fan 3 only while the power is on. Fan 3 (11) shares a bit with
# the base fan 1 (01), so one forced bit never differs from the base code.
def conditional_aircon(power, mode, fan):
    if mode == "DRY" and power == "ON":
        fan = "3"
    data = [MODES[mode] | FANS[fan] << 4, 1 if power == "ON" else 0]
    return "H" + "".join(bits(b) for b in data + [sum(data) & 0xff]) + "0"


def conditional_captures(seed=1, count=60):
    names = ["power", "mode", "fan"]
    choices = [["ON", "OFF"], list(MODES), list(FANS)]
    base = ["ON", "COOL", "1"]
    sets = [base] + [base[:i] + [v] + base[i + 1:]
                     for i in range(3) for v in choices[i] if v != base[i]]
    rng = random.Random(seed)
    sets += [[rng.choice(c) for c in choices] for _ in range(count)]
    return [capture(list(zip(names, fs)), conditional_aircon(*fs)) for fs in sets]


class RemoteTest(unittest.TestCase):
    def test_conditional_override(self):
        remote, failures = ir2irlingo.build_remote(conditional_captures(), "test")
        self.assertEqual([], failures)
        self.assertEqual(1, len(remote["overrides"]))
        override = remote["overrides"][0]
        self.assertEqual({"mode": "DRY", "power": "ON"}, dict(override["when"]))
        # Both fan bits, also the one equal to the base fan
        self.assertEqual(2, len(override["code"].replace(" ", "")))
        everything = [capture([("power", p), ("mode", m), ("fan", f)], conditional_aircon(p, m, f))
                      for p in ("ON", "OFF") for m in MODES for f in FANS]
        self.assertEqual([], ir2irlingo.check_remote(remote, everything))

    def test_aircon(self):
        captures = aircon_captures()
        remote, failures = ir2irlingo.build_remote(captures, "test")
        self.assertEqual([], failures)
        self.assertEqual([{"type": "sum", "from": 0, "to": 1, "at": 2}], remote["checksums"])
        # Mode overrides the fan, so it comes after it
        self.assertEqual(["fan", "mode"], list(remote["features"]))
        self.assertEqual([], ir2irlingo.check_remote(remote, captures))
        self.assertTrue(remote["tests"])

    def test_override_needs_combinations(self):
        remote, failures = ir2irlingo.build_remote(aircon_captures(False), "test")
        self.assertEqual([], failures)
        # Without combinations DRY with fan 3 cannot be known
        wrong = ir2irlingo.check_remote(remote, [capture([("mode", "DRY"), ("fan", "3")],
                                                      aircon("DRY", "3"))])
        self.assertEqual(1, len(wrong))

    def test_missing_single_change(self):
        captures = [c for c in aircon_captures()
                    if dict(c["features"]) != {"mode": "COOL", "fan": "1"}]
        with self.assertRaisesRegex(ir2irlingo.Ir2IrlingoError, "mode=COOL"):
            ir2irlingo.build_remote(captures, "test")

    def test_different_lengths(self):
        captures = aircon_captures()
        captures.append(capture([("mode", "AUTO"), ("fan", "1")], aircon("AUTO", "1") + "0"))
        with self.assertRaisesRegex(ir2irlingo.Ir2IrlingoError, "structure"):
            ir2irlingo.build_remote(captures, "test")

    def test_check_tolerance(self):
        captures = aircon_captures()
        remote, _ = ir2irlingo.build_remote(captures, "test")
        late = capture([("mode", "COOL"), ("fan", "2")], aircon("COOL", "2"))
        late["durations"] = [d * 1.1 for d in late["durations"]]
        self.assertEqual([], ir2irlingo.check_remote(remote, [late]))
        late["durations"] = [d * 1.5 for d in late["durations"]]
        self.assertEqual(1, len(ir2irlingo.check_remote(remote, [late])))

    def test_tied_bit_breaks_cycle(self):
        # Bits 0-2 are the mode, bit 3 the power. Power off forces the mode
        # bits to 100 (HEAT's code), so bit 2 is changed once by mode=HEAT
        # and once by power=OFF from the base. Given to power (listed first),
        # each feature would override the other.
        modes = {"AUTO": 0, "COOL": 1, "DRY": 2, "FAN": 3, "HEAT": 4}
        def code(power, mode):
            byte = modes[mode] | 8 if power == "ON" else 4
            return "H" + bits(byte) + "0"
        sets = [("ON", "COOL"), ("OFF", "COOL")] + [("ON", m) for m in modes if m != "COOL"]
        sets += list(itertools.product(("ON", "OFF"), modes))
        captures = [capture([("power", p), ("mode", m)], code(p, m)) for p, m in sets]
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            remote, failures = ir2irlingo.build_remote(captures, "test")
        self.assertEqual([], failures)
        self.assertNotIn("override each other", err.getvalue())
        self.assertEqual(["mode", "power"], list(remote["features"]))

    def test_two_codes_for_one_setting(self):
        captures = aircon_captures()
        captures.insert(1, capture([("mode", "COOL"), ("fan", "1")], aircon("AUTO", "3")))
        with self.assertRaisesRegex(ir2irlingo.Ir2IrlingoError, "two different codes"):
            ir2irlingo.build_remote(captures, "test")
    def test_conditional_override_matching_the_base(self):
        # DRY forces fan 1, the base fan, only while the power is on: no bit
        # of it ever differs from the base code
        def code(power, mode, fan):
            if mode == "DRY" and power == "ON":
                fan = "1"
            data = [MODES[mode] | FANS[fan] << 4, 1 if power == "ON" else 0]
            return "H" + "".join(bits(b) for b in data + [sum(data) & 0xff]) + "0"
        captures = [capture(c["features"].items(), code(*c["features"].values()))
                    for c in conditional_captures(count=80)]
        remote, failures = ir2irlingo.build_remote(captures, "test")
        self.assertEqual([], failures)
        self.assertEqual([{"mode": "DRY", "power": "ON"}],
                         [dict(o["when"]) for o in remote["overrides"]])
        everything = [capture([("power", p), ("mode", m), ("fan", f)], code(p, m, f))
                      for p in ("ON", "OFF") for m in MODES for f in FANS]
        self.assertEqual([], ir2irlingo.check_remote(remote, everything))

    def test_repair_three_way_condition(self):
        # DRY with fan 2 sends fan 1, but only while the power is on: a
        # condition on three features (fan 1 and 2 share no bit, so no
        # smaller condition holds)
        def code(power, mode, fan):
            if mode == "DRY" and fan == "2" and power == "ON":
                fan = "1"
            data = [MODES[mode] | FANS[fan] << 4, 1 if power == "ON" else 0]
            return "H" + "".join(bits(b) for b in data + [sum(data) & 0xff]) + "0"
        captures = [capture(c["features"].items(), code(*c["features"].values()))
                    for c in conditional_captures(count=150)]
        remote, failures = ir2irlingo.build_remote(captures, "test")
        self.assertEqual([], failures)
        self.assertIn({"power": "ON", "mode": "DRY", "fan": "2"},
                      [dict(o["when"]) for o in remote["overrides"]])
        everything = [capture([("power", p), ("mode", m), ("fan", f)], code(p, m, f))
                      for p in ("ON", "OFF") for m in MODES for f in FANS]
        self.assertEqual([], ir2irlingo.check_remote(remote, everything))

    def test_variant_for_another_length(self):
        # Power off sends one short code
        def code(power, mode, fan):
            return "H" + bits(0x5A) + "0" if power == "OFF" else conditional_aircon("ON", mode, fan)
        captures = [capture(c["features"].items(), code(*c["features"].values()))
                    for c in conditional_captures()]
        remote, failures = ir2irlingo.build_remote(captures, "test")
        self.assertEqual([], failures)
        self.assertEqual(1, len(remote["variants"]))
        variant = remote["variants"][0]
        self.assertEqual({"power": "OFF"}, dict(variant["when"]))
        self.assertEqual(["when", "template"], list(variant))
        self.assertIn("OFF", remote["features"]["power"])
        everything = [capture([("power", p), ("mode", m), ("fan", f)], code(p, m, f))
                      for p in ("ON", "OFF") for m in MODES for f in FANS]
        self.assertEqual([], ir2irlingo.check_remote(remote, everything))

    def test_fallback(self):
        # A checksum squaring the data cannot be expressed
        def code(mode, fan):
            data = [MODES[mode] | FANS[fan] << 4]
            return "H" + bits(data[0]) + bits(data[0] * data[0] & 0xff) + "0"
        sets = [("AUTO", "1"), ("COOL", "1"), ("DRY", "1"), ("AUTO", "2"), ("AUTO", "3"),
                ("COOL", "3")]
        captures = [capture([("mode", m), ("fan", f)], code(m, f)) for m, f in sets]
        remote, failures = ir2irlingo.build_remote(captures, "test")
        self.assertTrue(failures)
        remote, failures = ir2irlingo.build_remote(captures, "test", fallback=True)
        self.assertEqual([], failures)
        self.assertEqual([], ir2irlingo.check_remote(remote, captures))
        hard = [o for o in remote["overrides"] if len(o["when"]) == 2]
        self.assertTrue(hard)
        # Only the recorded combinations are hard-coded
        unrecorded = capture([("mode", "DRY"), ("fan", "3")], code("DRY", "3"))
        self.assertEqual(1, len(ir2irlingo.check_remote(remote, [unrecorded])))

    def test_fallback_without_selector(self):
        # A longer code no feature value selects: hard-coded as a variant
        captures = [c for c in aircon_captures()
                    if dict(c["features"]) != {"mode": "COOL", "fan": "2"}]
        captures.append(capture([("mode", "COOL"), ("fan", "2")], aircon("COOL", "2") + "0"))
        remote, failures = ir2irlingo.build_remote(captures, "test", fallback=True)
        self.assertEqual([], failures)
        self.assertEqual({"mode": "COOL", "fan": "2"}, dict(remote["variants"][0]["when"]))

    def test_json_output(self):
        remote, _ = ir2irlingo.build_remote(aircon_captures(), "test")
        text = ir2irlingo.to_json(remote)
        self.assertIn('"0": {"mark": 435, "space": 435}', text)
        self.assertIn('{"type": "sum", "from": 0, "to": 1, "at": 2}', text)


def write_captures(path, captures):
    with open(path, "w") as f:
        f.write("# recorded\n\n")
        for c in captures:
            f.write(json.dumps({"features": c["features"], "frequency": c["frequency"],
                                "durations": c["durations"]}) + "\n")


class LoadTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.dir.name, "captures.jsonl")

    def tearDown(self):
        self.dir.cleanup()

    def load(self, text):
        with open(self.path, "w") as f:
            f.write(text)
        return ir2irlingo.load_captures([self.path])

    def test_load(self):
        write_captures(self.path, aircon_captures()[:2])
        captures = ir2irlingo.load_captures([self.path])
        self.assertEqual(2, len(captures))
        self.assertEqual({"mode": "AUTO", "fan": "1"}, dict(captures[0]["features"]))
        self.assertEqual(38000, captures[0]["frequency"])
        self.assertTrue(captures[0]["where"].endswith("captures.jsonl:3"))

    def test_default_frequency_and_numbers_as_values(self):
        captures = self.load('{"features": {"temperature": 21}, "durations": [100.4, 200]}\n')
        self.assertEqual(38000, captures[0]["frequency"])
        self.assertEqual("21", captures[0]["features"]["temperature"])
        self.assertEqual([100, 200], captures[0]["durations"])

    def test_invalid(self):
        for text, message in (
                ("{\n", "captures.jsonl:1"),
                ("[1]\n", "not a JSON object"),
                ('{"durations": [1, 2]}\n', "no features"),
                ('{"features": {}, "durations": [1, 2]}\n', "no features"),
                ('{"features": {"a": "1"}, "durations": [1]}\n', "positive numbers"),
                ('{"features": {"a": "1"}, "durations": [1, -2]}\n', "positive numbers"),
                ('{"features": {"a": "1"}, "durations": [1, 2]}\n'
                 '{"features": {"b": "1"}, "durations": [1, 2]}\n', "differ"),
                ("# nothing\n", "no captures")):
            with self.assertRaisesRegex(ir2irlingo.Ir2IrlingoError, message):
                self.load(text)


class MainTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.captures = os.path.join(self.dir.name, "captures.jsonl")
        self.output = os.path.join(self.dir.name, "remote.json")

    def tearDown(self):
        self.dir.cleanup()

    def run_main(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            result = ir2irlingo.main(list(args))
        return result, out.getvalue(), err.getvalue()

    def test_derive_and_check(self):
        write_captures(self.captures, aircon_captures())
        check = os.path.join(self.dir.name, "check.jsonl")
        write_captures(check, [capture([("mode", "DRY"), ("fan", "3")], aircon("DRY", "3"))])
        result, out, err = self.run_main(self.captures, "-o", self.output, "--check", check,
                                         "--description", "Test aircon")
        self.assertEqual(0, result, err)
        self.assertIn("checksum", err)
        self.assertIn("all captures reproduced", err)
        self.assertIn("0 of 1 checked captures", err)
        with open(self.output) as f:
            remote = json.load(f)
        self.assertEqual("Test aircon", remote["description"])

    def test_standard_output(self):
        write_captures(self.captures, aircon_captures())
        result, out, err = self.run_main(self.captures, "--no-separators", "--tests", "0")
        self.assertEqual(0, result, err)
        remote = json.loads(out)
        self.assertNotIn("tests", remote)
        self.assertNotIn("-", remote["symboldefinitions"])

    def test_check_fails(self):
        # Without combinations the DRY override is not found
        write_captures(self.captures, aircon_captures(False))
        check = os.path.join(self.dir.name, "check.jsonl")
        write_captures(check, [capture([("mode", "DRY"), ("fan", "3")], aircon("DRY", "3"))])
        result, out, err = self.run_main(self.captures, "-o", self.output, "--check", check)
        self.assertEqual(1, result)
        self.assertIn("1 of 1 checked captures are not sent", err)

    def test_not_reproduced(self):
        # A checksum squaring the data cannot be expressed
        def code(mode, fan):
            data = MODES[mode] | FANS[fan] << 4
            return "H" + bits(data) + bits(data * data & 0xff) + "0"
        sets = [("AUTO", "1"), ("COOL", "1"), ("DRY", "1"), ("AUTO", "2"), ("AUTO", "3"),
                ("COOL", "3"), ("DRY", "2")]
        write_captures(self.captures, [capture([("mode", m), ("fan", f)], code(m, f))
                                       for m, f in sets])
        result, out, err = self.run_main(self.captures, "-o", self.output)
        self.assertEqual(1, result)
        self.assertIn("captures are not reproduced", err)
        self.assertIn("expected", err)

    def test_error(self):
        result, out, err = self.run_main(os.path.join(self.dir.name, "empty.jsonl"))
        self.assertEqual(2, result)
        with open(self.captures, "w") as f:
            f.write("# nothing\n")
        result, out, err = self.run_main(self.captures)
        self.assertEqual(2, result)
        self.assertIn("ir2irlingo: no captures", err)


if __name__ == "__main__":
    unittest.main()
