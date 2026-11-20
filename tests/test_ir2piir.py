"""Unit tests of tools/ir2piir.py (run with PYTHONPATH=tools)"""

import itertools
import random
import unittest
from collections import OrderedDict

import ir2piir

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
        index, means = ir2piir.cluster([430, 440, 435, 1300, 1290, 1750], 0.2)
        self.assertEqual(3, len(means))
        self.assertEqual(index[430], index[440])
        self.assertNotEqual(index[1300], index[1750])
        self.assertEqual(435, means[index[430]])

    def test_weighted_mean(self):
        index, means = ir2piir.cluster([400, 400, 400, 460], 0.2)
        self.assertEqual([415], means)


class SymbolTest(unittest.TestCase):
    def test_pulse_distance(self):
        # Codes end with a stop mark, the space after it is not recorded
        captures = [capture({"k": "a"}, "H0110W10000"), capture({"k": "b"}, "H1001W01110")]
        symbols, codes = ir2piir.derive_symbols(captures)
        self.assertEqual((435, 435), symbols["0"])
        self.assertEqual((435, 1300), symbols["1"])
        self.assertEqual((3500, 1750), symbols["H"])
        self.assertEqual((435, 10000), symbols["W"])
        # The final mark without its space becomes a symbol with that mark
        self.assertEqual(["H0110W10000", "H1001W01110"], codes)

    def test_pulse_width(self):
        table = {"S": (2400, 600), "0": (600, 600), "1": (1200, 600)}
        captures = [capture({"k": "a"}, "S0110", table), capture({"k": "b"}, "S1001", table)]
        symbols, codes = ir2piir.derive_symbols(captures)
        self.assertEqual((600, 600), symbols["0"])
        self.assertEqual((1200, 600), symbols["1"])
        self.assertEqual(["H0110", "H1001"], codes)

    def test_jitter(self):
        jittered = [capture({"k": "a"}, "H0110W10000")]
        jittered[0]["durations"] = [d + random.Random(i).randint(-30, 30)
                                    for i, d in enumerate(jittered[0]["durations"])]
        jittered.append(capture({"k": "b"}, "H1001W01110"))
        symbols, codes = ir2piir.derive_symbols(jittered)
        self.assertEqual(["H0110W10000", "H1001W01110"], codes)

    def test_one_kind_of_pair(self):
        with self.assertRaises(ir2piir.Ir2PiirError):
            ir2piir.derive_symbols([capture({"k": "a"}, "000")])

    def test_separators(self):
        self.assertEqual("H-00001111-0101-W-1", ir2piir.add_separators("H000011110101W1"))
        self.assertEqual("00000000-11111111", ir2piir.add_separators("0000000011111111"))


class ChecksumTest(unittest.TestCase):
    def codes(self, make, count=40, seed=1):
        rng = random.Random(seed)
        return [make([rng.randrange(256) for _ in range(4)]) for _ in range(count)]

    def test_byte_sum(self):
        codes = self.codes(lambda d: "H" + "".join(bits(b) for b in d + [sum(d) & 0xff]))
        self.assertEqual([{"type": "sum", "from": 0, "to": 3, "at": 4}],
                         ir2piir.find_checksums(codes))

    def test_sum_msb_first_with_start_value(self):
        def make(d):
            return "H" + "".join(bits(b, msb=True) for b in d + [(0x5A + sum(d)) & 0xff])
        self.assertEqual([{"type": "sum", "from": 0, "to": 3, "at": 4, "init": 0x5A,
                           "bitorder": "msb"}],
                         ir2piir.find_checksums(self.codes(make)))

    def test_xor(self):
        def make(d):
            x = 0
            for b in d:
                x ^= b
            return "".join(bits(b) for b in d + [x])
        self.assertEqual([{"type": "xor", "from": 0, "to": 3, "at": 4}],
                         ir2piir.find_checksums(self.codes(make)))

    def test_negated_sum(self):
        codes = self.codes(lambda d: "".join(bits(b) for b in d + [-sum(d) & 0xff]))
        self.assertEqual([{"type": "sum", "from": 0, "to": 3, "at": 4, "negate": True}],
                         ir2piir.find_checksums(codes))

    def test_inverted_copies(self):
        # Every byte followed by its complement
        codes = self.codes(lambda d: "".join(bits(b) + bits(~b & 0xff) for b in d[:2]))
        found = ir2piir.find_checksums(codes)
        # Byte 3 may also be found as the XOR of bytes 0-2; either is right
        self.assertEqual([1, 3], sorted(spec["at"] for spec in found))
        for spec in found:
            self.assertLess(spec["to"], spec["at"])
        for code in codes:
            self.assertEqual(code, ir2piir.apply_checksums(code, found))

    def test_nibble_sum(self):
        def make(d):
            nibbles = [n for b in d for n in (b & 15, b >> 4)]
            return "".join(bits(b) for b in d) + bits(sum(nibbles) & 15, 4) + "0000"
        self.assertEqual([{"type": "sum", "unit": "nibble", "from": 0, "to": 7, "at": 8}],
                         ir2piir.find_checksums(self.codes(make)))

    def test_nibbles_not_next_to_each_other(self):
        # A 4-bit sum of the low nibbles of bytes 0-1 and the high nibble of
        # byte 2, plus 10, in the high nibble of byte 3 (Gree style)
        def make(d):
            check = (10 + (d[0] & 15) + (d[1] & 15) + (d[2] >> 4)) & 15
            return "".join(bits(b) for b in d[:3] + [check << 4 | d[3] & 15])
        self.assertEqual([{"type": "sum", "unit": "nibble", "data": [0, 2, 5], "at": 7,
                           "init": 10}],
                         ir2piir.find_checksums(self.codes(make)))

    def test_bytes_not_next_to_each_other(self):
        def make(d):
            return "".join(bits(b) for b in d + [d[0] ^ d[2]])
        self.assertEqual([{"type": "xor", "data": [0, 2], "at": 4}],
                         ir2piir.find_checksums(self.codes(make)))

    def test_constant_byte_inside_range(self):
        # Byte 1 never changes, but the checksum covers it
        def make(d):
            data = [d[0], 0x37, d[1], d[2]]
            return "".join(bits(b) for b in data + [sum(data) & 0xff])
        self.assertEqual([{"type": "sum", "from": 0, "to": 3, "at": 4}],
                         ir2piir.find_checksums(self.codes(make)))

    def test_frame_sent_twice(self):
        # The sum is found on the first frame, not as a mix of both copies
        def make(d):
            frame = "".join(bits(b) for b in d + [sum(d) & 0xff])
            return frame + "W" + frame
        found = ir2piir.find_checksums(self.codes(make))
        self.assertEqual({"type": "sum", "from": 0, "to": 3, "at": 4}, found[0])
        for code in self.codes(make, seed=2):
            broken = code[:32] + ("1" if code[32] == "0" else "0") + code[33:]
            self.assertEqual(code, ir2piir.apply_checksums(code, found))
            self.assertNotEqual(broken, ir2piir.apply_checksums(broken, found))

    def test_none(self):
        self.assertEqual([], ir2piir.find_checksums(
            self.codes(lambda d: "".join(bits(b) for b in d))))

    def test_apply_like_piir(self):
        # Vectors from tests/test_checksum.c
        apply = ir2piir.apply_checksums
        self.assertEqual("10000000-01000000-11000000", apply(
            "10000000-01000000-00000000", [{"type": "sum", "from": 0, "to": 1, "at": 2}]))
        self.assertEqual("0001-1111-0010-1111-0011", apply(
            "0001-1111-0010-1111-0000", [{"type": "sum", "unit": "nibble", "data": [0, 2],
                                          "at": 4, "bitorder": "msb"}]))
        self.assertEqual("00000001-00000010-00000100", apply(
            "00000001-00000010-00000000", [{"type": "sum", "from": 0, "to": 1, "at": 2,
                                            "invert": True, "negate": True,
                                            "bitorder": "msb"}]))


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
        remote, failures = ir2piir.build_remote(conditional_captures(), "test")
        self.assertEqual([], failures)
        self.assertEqual(1, len(remote["overrides"]))
        override = remote["overrides"][0]
        self.assertEqual({"mode": "DRY", "power": "ON"}, dict(override["when"]))
        # Both fan bits, also the one equal to the base fan
        self.assertEqual(2, len(override["code"].replace(" ", "")))
        everything = [capture([("power", p), ("mode", m), ("fan", f)], conditional_aircon(p, m, f))
                      for p in ("ON", "OFF") for m in MODES for f in FANS]
        self.assertEqual([], ir2piir.check_remote(remote, everything))

    def test_aircon(self):
        captures = aircon_captures()
        remote, failures = ir2piir.build_remote(captures, "test")
        self.assertEqual([], failures)
        self.assertEqual([{"type": "sum", "from": 0, "to": 1, "at": 2}], remote["checksums"])
        # Mode overrides the fan, so it comes after it
        self.assertEqual(["fan", "mode"], list(remote["features"]))
        self.assertEqual([], ir2piir.check_remote(remote, captures))
        self.assertTrue(remote["tests"])

    def test_override_needs_combinations(self):
        remote, failures = ir2piir.build_remote(aircon_captures(False), "test")
        self.assertEqual([], failures)
        # Without combinations DRY with fan 3 cannot be known
        wrong = ir2piir.check_remote(remote, [capture([("mode", "DRY"), ("fan", "3")],
                                                      aircon("DRY", "3"))])
        self.assertEqual(1, len(wrong))

    def test_missing_single_change(self):
        captures = [c for c in aircon_captures()
                    if dict(c["features"]) != {"mode": "COOL", "fan": "1"}]
        with self.assertRaisesRegex(ir2piir.Ir2PiirError, "mode=COOL"):
            ir2piir.build_remote(captures, "test")

    def test_different_lengths(self):
        captures = aircon_captures()
        captures.append(capture([("mode", "AUTO"), ("fan", "1")], aircon("AUTO", "1") + "0"))
        with self.assertRaisesRegex(ir2piir.Ir2PiirError, "structure"):
            ir2piir.build_remote(captures, "test")

    def test_check_tolerance(self):
        captures = aircon_captures()
        remote, _ = ir2piir.build_remote(captures, "test")
        late = capture([("mode", "COOL"), ("fan", "2")], aircon("COOL", "2"))
        late["durations"] = [d * 1.1 for d in late["durations"]]
        self.assertEqual([], ir2piir.check_remote(remote, [late]))
        late["durations"] = [d * 1.5 for d in late["durations"]]
        self.assertEqual(1, len(ir2piir.check_remote(remote, [late])))

    def test_json_output(self):
        remote, _ = ir2piir.build_remote(aircon_captures(), "test")
        text = ir2piir.to_json(remote)
        self.assertIn('"0": {"mark": 435, "space": 435}', text)
        self.assertIn('{"type": "sum", "from": 0, "to": 1, "at": 2}', text)


if __name__ == "__main__":
    unittest.main()
