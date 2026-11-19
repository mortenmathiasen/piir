"""Unit tests for tools/lirc2piir.py.

Run from the tests directory with tools/ on PYTHONPATH:
    PYTHONPATH=../tools python3 -m unittest test_lirc2piir
"""

import contextlib
import io
import json
import os
import stat
import sys
import tempfile
import textwrap
import unittest

import lirc2piir as L

HERE = os.path.dirname(os.path.abspath(__file__))


def remote(text):
    """Parse a config with exactly one remote."""
    remotes = L.parse_config(textwrap.dedent(text))
    assert len(remotes) == 1
    return remotes[0]


def durations(signal):
    return [v for v, _ in signal]


NEC = """
begin remote
  name  NEC
  bits  8
  flags SPACE_ENC
  header 9000 4500
  one 560 1690
  zero 560 560
  ptrail 560
  gap 40000
  begin codes
    KEY_A 0x01
    KEY_B 0x02
  end codes
end remote
"""


class Numbers(unittest.TestCase):
    def test_strtonum(self):
        self.assertEqual(255, L.strtonum("0xFF"))
        self.assertEqual(8, L.strtonum("010"))
        self.assertEqual(0, L.strtonum("0"))
        self.assertEqual(42, L.strtonum("42abc"))
        self.assertEqual(-3, L.strtonum("-3"))
        self.assertEqual(0, L.strtonum("abc"))

    def test_is_number(self):
        self.assertTrue(L.is_number("0x1F"))
        self.assertTrue(L.is_number("123"))
        self.assertFalse(L.is_number("12a"))
        self.assertFalse(L.is_number("comment"))

    def test_reverse_and_mask(self):
        self.assertEqual(0b0011, L.reverse(0b1100, 4))
        self.assertEqual(0xFF, L.gen_mask(8))


class Parser(unittest.TestCase):
    def assertConfigError(self, text, message):
        with self.assertRaises(L.ConfigError) as cm:
            L.parse_config(textwrap.dedent(text))
        self.assertIn(message, str(cm.exception))

    def test_basic(self):
        r = remote(NEC)
        self.assertEqual("NEC", r.name)
        self.assertEqual("SPACE_ENC", r.protocol())
        self.assertEqual((9000, 4500), (r.phead, r.shead))
        self.assertEqual(["KEY_A", "KEY_B"], [c.name for c in r.codes])
        self.assertEqual([[1], [2]], [c.codes for c in r.codes])
        self.assertEqual(L.DEFAULT_FREQ, r.freq)

    def test_comments_and_options(self):
        r = remote("""
        # a comment in the first column
        begin remote
          # an indented comment outside the codes
          name  X
          bits  8
          flags SPACE_ENC | CONST_LENGTH
          one 500 1500     # comment after values
          zero 500 500
          gap 30000 40000
          driver default
          frequency 36000
          duty_cycle 33
          begin codes
            # an indented comment inside the codes
            #KEY_X 0x10
            KEY_Y 0x20 0x21 # two codes
          end codes
        end remote
        """)
        self.assertEqual({"SPACE_ENC", "CONST_LENGTH"}, r.flags)
        self.assertEqual((30000, 40000), (r.gap, r.gap2))
        self.assertEqual(30000, r.min_gap())
        self.assertEqual((36000, 33), (r.freq, r.duty_cycle))
        self.assertEqual(["#KEY_X", "KEY_Y"], [c.name for c in r.codes])
        self.assertEqual([0x20, 0x21], r.codes[1].codes)

    def test_min_gap_uses_smaller_second_gap(self):
        r = remote(NEC.replace("gap 40000", "gap 40000 30000"))
        self.assertEqual(30000, r.min_gap())

    def test_raw_codes(self):
        r = remote("""
        begin remote
          name RAW
          flags RAW_CODES
          gap 30000
          begin raw_codes
            name ONE
              100 200
              300
            name TWO
              400 500 600 # comment
          end raw_codes
        end remote
        """)
        self.assertTrue(r.raw)
        self.assertEqual("RAW_CODES", r.protocol())
        self.assertEqual([[100, 200, 300], [400, 500, 600]], [c.signals for c in r.codes])

    def test_multiple_remotes(self):
        remotes = L.parse_config(NEC + NEC.replace("name  NEC", "name  NEC2"))
        self.assertEqual(["NEC", "NEC2"], [r.name for r in remotes])

    def test_errors(self):
        self.assertConfigError("begin remote\nbegin remote\n", "nested remote")
        self.assertConfigError("begin codes\n", "unexpected begin")
        self.assertConfigError("end remote\n", "unexpected end")
        self.assertConfigError("begin remote\nname X\nbegin codes\nKEY\n", "has no value")
        self.assertConfigError("begin remote\nname X\nbegin codes\nKEY 0x1 x\n", "invalid code")
        self.assertConfigError("begin remote\nbegin raw_codes\nname\n", "raw code without name")
        self.assertConfigError("begin remote\nbegin raw_codes\n100 200\n", "signals before name")
        self.assertConfigError("begin remote\nname X\nbegin raw_codes\nname A\n1 2\nend raw_codes\n",
                               "even number of signals")
        self.assertConfigError("begin remote\nflags SPACE_ENC|BOGUS\n", "unknown flag BOGUS")
        self.assertConfigError("begin remote\nbogus 1\n", "unknown definition")
        self.assertConfigError("begin remote\nbits\n", "without value")
        self.assertConfigError("include other.conf\n", "include is not supported")
        self.assertConfigError("bits 8\n", "outside of a remote")
        self.assertConfigError("begin remote\nname X\n", "unterminated remote")
        self.assertConfigError("begin remote\nbits 8\nend remote\n", "missing remote name")
        self.assertConfigError("begin remote\nname X\nbits 8\nend remote\n", "no codes")

    def test_finalize(self):
        r = remote("""
        begin remote
          name X
          bits 4
          flags SPACE_ENC|REVERSE
          one 1 2
          zero 1 1
          pre_data_bits 4
          pre_data 0x1
          post_data_bits 4
          post_data 0x18
          toggle_bit 2
          begin codes
            K 0x13
          end codes
        end remote
        """)
        # invalid bits are masked away, then everything is reversed
        self.assertEqual(0x8, r.pre_data)
        self.assertEqual(0x1, r.post_data)
        self.assertEqual([0xC], r.codes[0].codes)
        self.assertEqual(1 << (12 - 2), r.toggle_bit_mask)
        self.assertEqual(0, r.toggle_bit)

    def test_rc6_mask_from_toggle_bit(self):
        r = remote("""
        begin remote
          name X
          bits 8
          flags RC6
          one 444 444
          zero 444 444
          toggle_bit 3
          begin codes
            K 0x01
          end codes
        end remote
        """)
        self.assertEqual(1 << 5, r.rc6_mask)
        self.assertTrue(r.is_rc6() and r.is_biphase())

    def test_shift_enc_is_rc5(self):
        r = remote(NEC.replace("SPACE_ENC", "SHIFT_ENC"))
        self.assertEqual("RC5", r.protocol())

    def test_no_protocol(self):
        r = remote(NEC.replace("flags SPACE_ENC", ""))
        self.assertIsNone(r.protocol())

    def test_read_text_strips_bom(self):
        with tempfile.NamedTemporaryFile("wb", suffix=".conf", delete=False) as f:
            f.write(b"\xef\xbb\xbf" + NEC.encode())
        try:
            self.assertEqual("NEC", L.parse_config(L.read_text(f.name))[0].name)
        finally:
            os.unlink(f.name)


class Encoder(unittest.TestCase):
    def send(self, text, key=0):
        r = remote(text)
        return L.send_once(r, r.codes[key])

    def test_space_enc(self):
        sig = self.send(NEC)
        bits = [1690 if b == "1" else 560 for b in "00000001"]
        expected = [9000, 4500]
        for space in bits:
            expected += [560, space]
        expected.append(560)
        self.assertEqual(expected, durations(sig))

    def test_min_repeat_uses_repeat_code(self):
        sig = self.send(NEC.replace("gap 40000", "gap 40000\nrepeat 9000 2250\nmin_repeat 2"))
        values = durations(sig)
        # frame, gap, repeat code, gap, repeat code
        self.assertEqual([40000, 9000, 2250, 560, 40000, 9000, 2250, 560], values[-8:])
        # gaps are marked as gap so PIIR writes them with gap symbols
        self.assertEqual([40000, 40000], [g for _, g in sig if g])

    def test_min_repeat_resends_code_without_repeat_code(self):
        sig = self.send(NEC.replace("gap 40000", "gap 40000\nmin_repeat 1"))
        values = durations(sig)
        half = (len(values) - 1) // 2
        self.assertEqual(values[:half], values[half + 1:])

    def test_const_length(self):
        sig = self.send(NEC.replace("SPACE_ENC", "SPACE_ENC|CONST_LENGTH")
                        .replace("gap 40000", "gap 100000\nmin_repeat 1"))
        values = durations(sig)
        frame = (len(values) - 1) // 2
        # LIRC 0.10.2 leaves the trailing space out of the frame length
        self.assertEqual(100000, sum(values[:frame]) + values[frame])

    def test_const_length_too_short(self):
        self.assertIsNone(self.send(NEC.replace("SPACE_ENC", "SPACE_ENC|CONST_LENGTH")
                                    .replace("gap 40000", "gap 1000")))

    def test_small_gaps_are_concatenated(self):
        sig = self.send(NEC.replace("gap 40000", "gap 5000\nmin_repeat 1"))
        values = durations(sig)
        self.assertIn(5000, values)
        self.assertEqual(len(values) % 2, 1)

    def test_repeat_gap_header_and_mask(self):
        text = NEC.replace("SPACE_ENC", "SPACE_ENC|REPEAT_HEADER").replace(
            "gap 40000", "gap 40000\nrepeat 600 600\nrepeat_gap 20000\nmin_repeat 1")
        values = durations(self.send(text, 0))
        # the gap after the code is gap, repeat_gap follows repeat frames,
        # and REPEAT_HEADER sends the header before the repeat code
        self.assertEqual([40000, 9000, 4500, 600, 600, 560], values[-6:])
        values = durations(self.send(text.replace("min_repeat 1", "min_repeat 2")))
        self.assertEqual([20000, 9000, 4500, 600, 600, 560], values[-6:])

    def test_repeat_mask(self):
        text = NEC.replace("gap 40000", "gap 40000\nrepeat_mask 0x01\nmin_repeat 1")
        values = durations(self.send(text))
        frame = (len(values) - 1) // 2
        self.assertNotEqual(values[:frame], values[frame + 1:])

    def test_no_head_and_foot_rep(self):
        text = NEC.replace("SPACE_ENC", "SPACE_ENC|NO_HEAD_REP|NO_FOOT_REP|CONST_LENGTH").replace(
            "gap 40000", "gap 100000\nfoot 300 700\nmin_repeat 1")
        values = durations(self.send(text))
        self.assertEqual(9000, values[0])
        self.assertEqual(1, values.count(9000))   # no header in the repeat
        self.assertEqual(1, values.count(700))    # no foot in the repeat

    def test_lead_pre_post(self):
        text = NEC.replace("ptrail 560", "ptrail 560\nplead 300\npre_data_bits 2\npre_data 0x3\n"
                           "pre 800 900\npost_data_bits 1\npost_data 0x1\npost 1100 1200")
        values = durations(self.send(text))
        self.assertEqual([9000, 4500 + 0, 300 + 560], values[:3])
        self.assertIn(900, values)
        self.assertIn(1200, values)

    def test_space_first(self):
        values = durations(self.send(NEC.replace("SPACE_ENC", "SPACE_FIRST")))
        self.assertEqual(9000, values[0])
        self.assertEqual(len(values) % 2, 1)

    def test_rc5_biphase(self):
        text = """
        begin remote
          name RC5
          bits 4
          flags RC5
          one 889 889
          zero 889 889
          plead 889
          gap 100000
          begin codes
            K 0x5
          end codes
        end remote
        """
        # lead pulse, then 0101: zero = pulse+space, one = space+pulse,
        # adjacent halves merge
        self.assertEqual([889 + 889, 889 + 889, 889 + 889, 889 + 889, 889],
                         durations(self.send(text)))

    def test_toggle_masks(self):
        text = NEC.replace("gap 40000", "gap 40000\ntoggle_bit_mask 0x01\ntoggle_mask 0x02\nmin_repeat 1")
        values = durations(self.send(text))
        frame = (len(values) - 1) // 2
        # toggle_mask flips its bit on every frame after the first
        self.assertNotEqual(values[:frame], values[frame + 1:])

    def test_multi_bit_toggle_mask(self):
        text = NEC.replace("gap 40000", "gap 40000\ntoggle_bit_mask 0x03")
        r = remote(text)
        # LIRC starts with the bits of the first code inverted
        self.assertEqual(0x02, L.initial_toggle_state(r))
        self.assertIsNotNone(L.send_once(r, r.codes[0]))
        self.assertEqual(0, L.initial_toggle_state(remote(NEC)))

    def test_chained_codes(self):
        text = NEC.replace("KEY_B 0x02", "KEY_B 0x02\n    MACRO 0x01 0x02 0x03")
        r = remote(text.replace("gap 40000", "gap 5000"))
        self.assertIsNotNone(L.send_once(r, r.codes[2]))
        r = remote(text)
        self.assertIsNotNone(L.send_once(r, r.codes[2]))
        r = remote(text.replace("gap 40000", "gap 40000\nmin_repeat 1"))
        self.assertIsNotNone(L.send_once(r, r.codes[2]))

    def test_raw_repeat(self):
        text = """
        begin remote
          name RAW
          flags RAW_CODES
          gap 5000
          min_repeat 1
          begin raw_codes
            name K
              100 200 300
          end raw_codes
        end remote
        """
        self.assertEqual([100, 200, 300, 5000, 100, 200, 300], durations(self.send(text)))

    def test_buffer_too_long(self):
        signal = " ".join(["100"] * (L.WBUF_SIZE + 1))
        text = "begin remote\nname R\nflags RAW_CODES\ngap 1000\nbegin raw_codes\nname K\n%s\nend raw_codes\nend remote\n" % signal
        self.assertIsNone(self.send(text))

    def test_zero_durations(self):
        text = NEC.replace("one 560 1690", "one 0 1690")
        self.assertIsNotNone(self.send(text))
        r = remote(text)
        lines = L.simsend([r])
        self.assertTrue(lines)

    def test_negative_shift_does_not_crash(self):
        text = NEC.replace("bits  8", "bits  0")
        r = remote(text)
        L.send_once(r, r.codes[0])

    def test_simsend(self):
        r = remote(NEC.replace("gap 40000", "gap 40000\nrepeat 9000 2250"))
        lines = L.simsend([r], count=2, keysym="KEY_B")
        self.assertEqual("pulse 9000", lines[0])
        self.assertEqual("space 4500", lines[1])
        self.assertIn("pulse 9000", lines[1:])

    def test_simsend_receiver_remote(self):
        r = remote(NEC.replace("zero 560 560", "zero 0 0"))
        self.assertEqual(["code 0x1"], L.simsend([r], keysym="KEY_A"))

    def test_simsend_sorts_like_lirc(self):
        raw = """
        begin remote
          name RAW
          flags RAW_CODES
          gap 1000
          begin raw_codes
            name K
              1 2 3
          end raw_codes
        end remote
        """
        remotes = L.parse_config(textwrap.dedent(raw) + NEC)
        self.assertEqual("pulse 9000", L.simsend(remotes)[0])
        for r in remotes:
            r.manual_sort = 1
        self.assertEqual("pulse 1", L.simsend(remotes)[0])

    def test_simsend_repeats_same_name(self):
        text = NEC.replace("KEY_B 0x02", "KEY_A 0x02").replace("gap 40000", "gap 40000\nrepeat 9000 2250")
        lines = L.simsend([remote(text)])
        self.assertIn("pulse 9000", lines)


class Conversion(unittest.TestCase):
    def convert(self, text):
        return L.convert_remote(remote(text), {"lirc-remotes": "remotes/x.conf"})

    def test_convert(self):
        data, warnings = self.convert(NEC)
        self.assertEqual([], warnings)
        self.assertEqual("NEC (LIRC remote)", data["description"])
        self.assertEqual({"lirc-remotes": "remotes/x.conf"}, data["source"])
        self.assertEqual(0.5, data["dutycycle"])
        symbols = data["symboldefinitions"]
        self.assertEqual({"mark": 9000, "space": 4500}, symbols["H"])
        self.assertEqual({"mark": 560, "space": 1690}, symbols["1"])
        self.assertEqual({"mark": 0, "space": 0}, symbols["-"])
        keys = data["features"]["key"]
        self.assertEqual("H00000001", keys["KEY_A"][:9])
        self.assertEqual(len(data["template"]), max(len(v) for v in keys.values()))

    def test_gap_symbols(self):
        self.assertEqual("UVVWWWXXXXYYYYYZZZZZZ", L.gap_symbols(123456))
        self.assertEqual("", L.gap_symbols(0))
        data, _ = self.convert(NEC.replace("gap 40000", "gap 40000\nmin_repeat 1"))
        self.assertIn("VVVV", data["features"]["key"]["KEY_A"])
        self.assertEqual({"mark": 0, "space": 10000}, data["symboldefinitions"]["V"])

    def test_frequency_and_duty_defaults(self):
        data, _ = self.convert(NEC.replace("gap 40000", "gap 40000\nfrequency 0\nduty_cycle 150"))
        self.assertEqual(L.DEFAULT_FREQ, data["frequency"])
        self.assertEqual(0.5, data["dutycycle"])

    def test_warnings(self):
        text = NEC.replace("KEY_B 0x02", "KEY_B 0x02\n    KEY_A 0x03")
        _, warnings = self.convert(text)
        self.assertIn("duplicate key KEY_A ignored", warnings)

    def test_errors(self):
        for text, message in (
                (NEC.replace("zero 560 560", "zero 0 0"), "no timing"),
                (NEC.replace("SPACE_ENC", "XMP"), "XMP encoding is not supported"),
                (NEC.replace("flags SPACE_ENC", ""), "no encoding flag"),
                (NEC.replace("SPACE_ENC", "SPACE_ENC|CONST_LENGTH").replace("gap 40000", "gap 10"),
                 "no key can be sent")):
            with self.assertRaises(L.ConfigError) as cm:
                self.convert(text)
            self.assertIn(message, str(cm.exception))

    def test_too_long(self):
        text = NEC.replace("gap 40000", "gap 40000\nmin_repeat 60")
        with self.assertRaises(L.ConfigError) as cm:
            self.convert(text)
        self.assertIn("no key can be sent", str(cm.exception))

    def test_too_many_symbols(self):
        # Short codes with long repeated gaps need many gap symbols
        text = NEC.replace("bits  8", "bits  1").replace(
            "gap 40000", "gap 99999\nrepeat 1 1\nmin_repeat 50").replace("0x02", "0x01")
        with self.assertRaises(L.ConfigError) as cm:
            self.convert(text)
        self.assertIn("no key fits PIIR's limits", str(cm.exception))

    def test_raw_rounding(self):
        # Many slightly different durations must be rounded to fit the alphabet
        lines = []
        for k in range(12):
            values = []
            for i in range(9):
                values += [500 + k * 7 + i, 600 + k * 5 + i * 3]
            values.append(500 + k)
            lines.append("name K%d\n%s" % (k, " ".join(map(str, values))))
        text = ("begin remote\nname R\nflags RAW_CODES\ngap 30000\nbegin raw_codes\n%s\n"
                "end raw_codes\nend remote\n" % "\n".join(lines))
        data, warnings = self.convert(text)
        self.assertTrue(any("rounded" in w for w in warnings))
        self.assertLessEqual(len(data["symboldefinitions"]), len(L.SYMBOL_ALPHABET) + 1)

    def test_raw_too_many_pairs(self):
        # 10 x 10 mark/space combinations 30% apart cannot be merged
        lines = []
        steps = [int(200 * 1.3 ** i) for i in range(10)]
        for i, mark in enumerate(steps):
            for j, space in enumerate(steps):
                lines.append("name K%d_%d\n%d %d %d" % (i, j, mark, space, mark))
        text = ("begin remote\nname R\nflags RAW_CODES\ngap 30000\nbegin raw_codes\n%s\n"
                "end raw_codes\nend remote\n" % "\n".join(lines))
        with self.assertRaises(L.ConfigError) as cm:
            self.convert(text)
        self.assertIn("too many different pulse/space pairs", str(cm.exception))

    def test_cluster(self):
        mapping = L.cluster([100, 101, 103, 200, 205], lambda v: 10)
        self.assertEqual(101, mapping[100])
        self.assertEqual(101, mapping[103])
        self.assertEqual(202, mapping[205])

    def test_tokenize_and_expand(self):
        signal = [(500, 0), (1500, 0), (500, 0), (20500, 20000), (500, 0)]
        tokens = L.tokenize(signal)
        self.assertEqual([("pair", 500, 1500), ("pair", 500, 500), ("gap", 20000), ("pair", 500, 0)],
                         tokens)
        symbols = {"a": (500, 1500), "b": (500, 500), "c": (500, 0), "V": (0, 10000), "-": (0, 0)}
        self.assertEqual([500, 1500, 500, 20500, 500], L.expand("abVVc--", symbols))
        self.assertEqual([500], L.expand("Vb", symbols))

    def test_names(self):
        self.assertEqual("a_b", L.safe_name("a b"))
        self.assertEqual("remote", L.safe_name("..."))
        self.assertEqual("sony/RM-ED011", L.output_base("sony/RM-ED011.lircd.conf"))
        self.assertEqual("x/y", L.output_base("x/y.conf"))
        self.assertEqual("x/y.txt", L.output_base("x/y.txt"))

    def test_format_json(self):
        data, _ = self.convert(NEC)
        self.assertEqual(data, json.loads(L.format_json(data)))


class Commands(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()

    def write(self, name, text):
        path = os.path.join(self.dir, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(textwrap.dedent(text))
        return path

    def run_main(self, *args):
        out = io.StringIO()
        err = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = L.main(list(args))
        return code, out.getvalue(), err.getvalue()

    def test_convert_command(self):
        self.write("src/brand/one.lircd.conf", NEC)
        self.write("src/brand/two.lircd.conf", NEC + NEC)
        self.write("src/brand/broken.lircd.conf", "bits 8\n")
        self.write("src/brand/empty.lircd.conf", "# nothing\n")
        self.write("src/brand/xmp.lircd.conf", NEC.replace("SPACE_ENC", "XMP"))
        self.write("src/brand/notes.txt", "not a config")
        out_dir = os.path.join(self.dir, "out")
        report = os.path.join(self.dir, "report.txt")
        code, out, _ = self.run_main("convert", "-o", out_dir, "--report", report,
                                     os.path.join(self.dir, "src"))
        self.assertEqual(0, code)
        self.assertIn("3 remotes converted, 3 skipped", out)
        files = sorted(os.listdir(os.path.join(out_dir, "brand")))
        self.assertEqual(["one.json", "two_NEC.json", "two_NEC_2.json"], files)
        with open(report) as f:
            text = f.read()
        self.assertIn("SKIP brand/broken.lircd.conf: line 1: bits outside of a remote", text)
        self.assertIn("SKIP brand/empty.lircd.conf: no remotes", text)
        self.assertIn("XMP encoding is not supported", text)
        with open(os.path.join(out_dir, "brand", "one.json")) as f:
            data = json.load(f)
        self.assertEqual("remotes/brand/one.lircd.conf", data["source"]["lirc-remotes"])

    def test_convert_single_file_reports_to_stderr(self):
        path = self.write("x.lircd.conf", NEC.replace("SPACE_ENC", "XMP"))
        code, out, err = self.run_main("convert", "-o", self.dir, path)
        self.assertIn("0 remotes converted, 1 skipped", out)
        self.assertIn("XMP", err)

    def test_convert_warning(self):
        path = self.write("x.lircd.conf", NEC.replace("KEY_B 0x02", "KEY_A 0x02"))
        code, out, err = self.run_main("convert", "-o", self.dir, path)
        self.assertIn("WARN x.lircd.conf [NEC]: duplicate key KEY_A ignored", err)

    def test_simsend_command(self):
        path = os.path.join(HERE, "lirc", "nec.lircd.conf")
        code, out, _ = self.run_main("simsend", "-c", "2", path)
        with open(os.path.join(HERE, "lirc", "nec.simsend")) as f:
            self.assertEqual(f.read(), out)
        code, out, _ = self.run_main("simsend", "-k", "KEY_MUTE", path)
        self.assertTrue(out.startswith("pulse 9000\n"))

    def fake_irsimsend(self, body):
        """An irsimsend replacement writing simsend.out."""
        path = os.path.join(self.dir, "irsimsend")
        with open(path, "w") as f:
            f.write("#!%s\nimport sys\nsys.path.insert(0, %r)\nimport lirc2piir as L\n"
                    % (sys.executable, os.path.dirname(L.__file__)))
            f.write("args = sys.argv[1:]\ncount = int(args[args.index('-c') + 1])\n")
            f.write("path = args[-1]\n")
            f.write(textwrap.dedent(body))
        os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)
        return path

    def test_verify(self):
        good = self.fake_irsimsend("""
            lines = L.simsend(L.parse_config(L.read_text(path)), count)
            open('simsend.out', 'w').write('\\n'.join(lines) + '\\n')
        """)
        src = os.path.join(self.dir, "src")
        self.write("src/nec.lircd.conf", NEC)
        self.write("src/broken.lircd.conf", "bits 8\n")
        self.write("src/receiver.lircd.conf", NEC.replace("zero 560 560", "zero 0 0"))
        code, out, _ = self.run_main("verify", "--irsimsend", good, "--plugindir", self.dir, src)
        self.assertEqual(0, code)
        self.assertIn("2 checks, 0 mismatches", out)

        bad = self.fake_irsimsend("""
            open('simsend.out', 'w').write('pulse 1\\n')
        """)
        code, out, _ = self.run_main("verify", "--irsimsend", bad, src)
        self.assertEqual(1, code)
        self.assertIn("MISMATCH nec.lircd.conf (count 1) at line 1: lirc 'pulse 1', ours 'pulse 9000'", out)

        silent = self.fake_irsimsend("""
            sys.exit(0)
        """)
        code, out, _ = self.run_main("verify", "--irsimsend", silent, src)
        self.assertIn("MISMATCH", out)

        failing = self.fake_irsimsend("""
            sys.exit(1)
        """)
        code, out, _ = self.run_main("verify", "--irsimsend", failing, src)
        self.assertIn("0 checks, 0 mismatches", out)

    def test_version(self):
        with self.assertRaises(SystemExit):
            with contextlib.redirect_stdout(io.StringIO()):
                L.main(["--version"])


if __name__ == "__main__":
    unittest.main()
