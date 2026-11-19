#!/usr/bin/env python3
"""Convert LIRC remote configurations (lircd.conf) to PIIR remote JSON files.

The encoder is a port of lib/transmit.c and the repeat handling in lircd of
LIRC 0.10.2, the release Linux distributions ship, so a converted key sends
what `irsend SEND_ONCE` would send: the code itself, min_repeat repeats
(repeat code or full code) and the gaps between them. Two LIRC 0.10.2 bugs
are only emulated by the simsend and verify commands, not when converting:
durations of 0 (e.g. "one 0 833") break the alternation of pulses and spaces,
and raw remotes with a repeat code send the start of the raw signal instead
of the repeat code.

Commands:
  convert  PATH...   write PIIR JSON files (default) for lircd.conf files or
                     directories of them
  simsend  FILE      print the pulses and spaces LIRC's irsimsend would write
                     for FILE, using this script's encoder
  verify   PATH...   compare the encoder with the real irsimsend

Run with --help for the options of each command.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

VERSION = "1.0"

# LIRC constants (lib/ir_remote_types.h, lib/transmit.c)
DEFAULT_FREQ = 38000
WBUF_SIZE = 256
LIRCD_EXACT_GAP_THRESHOLD = 10000

PROTOCOLS = ("RAW_CODES", "RC5", "RC6", "RCMM", "SPACE_ENC", "SPACE_FIRST",
             "GRUNDIG", "BO", "SERIAL", "XMP")
FLAG_NAMES = set(PROTOCOLS) | {"SHIFT_ENC", "REVERSE", "NO_HEAD_REP",
                               "NO_FOOT_REP", "CONST_LENGTH", "REPEAT_HEADER"}
UNSUPPORTED = ("RCMM", "GRUNDIG", "BO", "SERIAL", "XMP")

# PIIR limits (src/irgeneric.h, src/lirc.h)
PIIR_MAX_COMMAND_SIZE = 2048
PIIR_MAX_LIRC_VALUES = 1024

# Symbols: '-' is the no-op padding symbol, U-Z are pure spaces used to write
# gaps between repeated frames in decimal (U = 100 ms ... Z = 1 us).
GAP_SYMBOLS = (("U", 100000), ("V", 10000), ("W", 1000), ("X", 100),
               ("Y", 10), ("Z", 1))
RESERVED = set("- \"\\") | {s for s, _ in GAP_SYMBOLS}
# H, 1 and 0 come last: they are used for header, one and zero where the
# encoding has them and only given to other pairs when nothing else is left.
SYMBOL_ALPHABET = [c for c in
                   "ABCDEFGIJKLMNOPQRST"
                   "abcdefghijklmnopqrstuvwxyz"
                   "23456789"
                   "!#$%&'()*+,./:;<=>?@[]^_`{|}~"
                   "H10"
                   if c not in RESERVED]


class ConfigError(Exception):
    pass


class Code:
    def __init__(self, name, codes=None, signals=None):
        self.name = name
        self.codes = codes or []     # code followed by its "next" codes
        self.signals = signals       # raw durations


class Remote:
    def __init__(self):
        self.name = None
        self.flags = set()
        self.bits = 0
        self.eps = 0
        self.aeps = 0
        self.phead = self.shead = 0
        self.pone = self.sone = 0
        self.pzero = self.szero = 0
        self.ptwo = self.stwo = 0
        self.pthree = self.sthree = 0
        self.pfoot = self.sfoot = 0
        self.prepeat = self.srepeat = 0
        self.pre_p = self.pre_s = 0
        self.post_p = self.post_s = 0
        self.plead = 0
        self.ptrail = 0
        self.pre_data_bits = 0
        self.pre_data = 0
        self.post_data_bits = 0
        self.post_data = 0
        self.gap = 0
        self.gap2 = 0
        self.repeat_gap = 0
        self.repeat_mask = 0
        self.toggle_bit = 0
        self.toggle_bit_mask = 0
        self.toggle_mask = 0
        self.rc6_mask = 0
        self.min_repeat = 0
        self.manual_sort = 0
        self.freq = DEFAULT_FREQ
        self.duty_cycle = 0
        self.codes = []
        self.raw = False
        # transmit state
        self.toggle_bit_mask_state = 0
        self.toggle_mask_state = 0
        self.repeat_countdown = 0
        self.min_remaining_gap = 0

    # --- helpers from lib/ir_remote.h
    def protocol(self):
        if self.raw:
            return "RAW_CODES"
        for p in PROTOCOLS:
            if p in self.flags:
                return p
        if "SHIFT_ENC" in self.flags:
            return "RC5"
        return None

    def bit_count(self):
        return self.pre_data_bits + self.bits + self.post_data_bits

    def has_repeat(self):
        return self.prepeat > 0 and self.srepeat > 0

    def has_header(self):
        return self.phead > 0 and self.shead > 0

    def has_foot(self):
        return self.pfoot > 0 and self.sfoot > 0

    def is_rc6(self):
        return self.protocol() == "RC6" or self.rc6_mask != 0

    def is_biphase(self):
        return self.protocol() == "RC5" or self.is_rc6()

    def is_const(self):
        return "CONST_LENGTH" in self.flags

    def min_gap(self):
        if self.gap2 != 0 and self.gap2 < self.gap:
            return self.gap2
        return self.gap

    def has_timing(self):
        return self.raw or not (self.pzero == 0 and self.szero == 0)


def strtonum(text):
    """C strtoull(text, NULL, 0): parses a prefix, 0 on failure."""
    m = re.match(r"\s*([+-]?)(0[xX][0-9a-fA-F]+|0[0-7]*|[1-9][0-9]*)", text)
    if not m:
        return 0
    digits = m.group(2)
    if digits[:2].lower() == "0x":
        value = int(digits, 16)
    elif digits.startswith("0") and len(digits) > 1:
        value = int(digits, 8)
    else:
        value = int(digits)
    return -value if m.group(1) == "-" else value


def is_number(text):
    return re.fullmatch(r"0[xX][0-9a-fA-F]+|[0-9]+", text) is not None


def reverse(data, bits):
    c = 0
    for i in range(bits):
        if data & (1 << i):
            c |= 1 << (bits - 1 - i)
    return c


def gen_mask(bits):
    return (1 << bits) - 1


PAIR_KEYS = {"header": ("phead", "shead"), "three": ("pthree", "sthree"),
             "two": ("ptwo", "stwo"), "one": ("pone", "sone"),
             "zero": ("pzero", "szero"), "foot": ("pfoot", "sfoot"),
             "repeat": ("prepeat", "srepeat"), "pre": ("pre_p", "pre_s"),
             "post": ("post_p", "post_s")}
INT_KEYS = {"bits": "bits", "eps": "eps", "aeps": "aeps", "plead": "plead",
            "ptrail": "ptrail", "pre_data_bits": "pre_data_bits",
            "pre_data": "pre_data", "post_data_bits": "post_data_bits",
            "post_data": "post_data", "repeat_gap": "repeat_gap",
            "repeat_mask": "repeat_mask", "toggle_bit": "toggle_bit",
            "repeat_bit": "toggle_bit", "toggle_bit_mask": "toggle_bit_mask",
            "toggle_mask": "toggle_mask", "rc6_mask": "rc6_mask",
            "min_repeat": "min_repeat", "frequency": "freq",
            "manual_sort": "manual_sort",
            "duty_cycle": "duty_cycle"}
IGNORED_KEYS = {"driver", "ignore_mask", "suppress_repeat",
                "min_code_repeat", "baud", "serial_mode", "dyncodes_name"}


def parse_config(text):
    """Parse lircd.conf text into Remote objects, like lib/config_file.c."""
    remotes = []
    rem = None
    mode = None            # None, "remote", "codes", "raw"
    raw_name = None
    raw_signals = []
    seen_names = set()

    def finish_raw():
        nonlocal raw_name, raw_signals
        if raw_name is not None:
            if len(raw_signals) % 2 == 0:
                raise ConfigError("raw code %s: even number of signals" % raw_name)
            rem.codes.append(Code(raw_name, signals=raw_signals))
        raw_name = None
        raw_signals = []

    for lineno, line in enumerate(text.splitlines(), 1):
        # Only a '#' in the first column starts a comment line; an indented
        # "#KEY_X 0x10" defines a key named "#KEY_X", as in LIRC. Elsewhere
        # indented comments are accepted, which LIRC does not.
        if line.startswith("#"):
            continue
        tokens = line.split()
        if not tokens or (mode != "codes" and tokens[0].startswith("#")):
            continue
        key = tokens[0]
        lkey = key.lower()
        val = tokens[1] if len(tokens) > 1 else None
        val2 = tokens[2] if len(tokens) > 2 else None

        if lkey == "begin":
            what = (val or "").lower()
            if what == "remote":
                if mode is not None:
                    raise ConfigError("line %d: nested remote" % lineno)
                rem = Remote()
                mode = "remote"
            elif what == "codes" and mode == "remote":
                mode = "codes"
            elif what == "raw_codes" and mode == "remote":
                rem.raw = True
                mode = "raw"
            else:
                raise ConfigError("line %d: unexpected begin %s" % (lineno, val))
            continue
        if lkey == "end":
            what = (val or "").lower()
            if what == "codes" and mode == "codes":
                mode = "remote"
            elif what == "raw_codes" and mode == "raw":
                finish_raw()
                mode = "remote"
            elif what == "remote" and mode == "remote":
                finalize_remote(rem)
                remotes.append(rem)
                rem = None
                mode = None
            else:
                raise ConfigError("line %d: unexpected end %s" % (lineno, val))
            continue

        if mode == "codes":
            if key.startswith("#") and (val is None or not is_number(val)):
                continue  # an indented comment
            if val is None:
                raise ConfigError("line %d: code %s has no value" % (lineno, key))
            values = []
            for t in tokens[1:]:
                if t.startswith("#"):
                    break
                if not is_number(t):
                    raise ConfigError("line %d: invalid code %s" % (lineno, t))
                values.append(strtonum(t))
            rem.codes.append(Code(key, codes=values))
        elif mode == "raw":
            if lkey == "name":
                finish_raw()
                if val is None:
                    raise ConfigError("line %d: raw code without name" % lineno)
                raw_name = val
            else:
                if raw_name is None:
                    raise ConfigError("line %d: signals before name" % lineno)
                for t in tokens:
                    if t.startswith("#"):
                        break
                    raw_signals.append(strtonum(t))
        elif mode == "remote":
            if val is None:
                raise ConfigError("line %d: %s without value" % (lineno, key))
            if lkey == "name":
                rem.name = val
            elif lkey == "flags":
                for flag in re.split(r"\s*\|\s*", " ".join(tokens[1:]).split("#")[0].strip()):
                    if not flag:
                        continue
                    if flag.upper() not in FLAG_NAMES:
                        raise ConfigError("line %d: unknown flag %s" % (lineno, flag))
                    rem.flags.add(flag.upper())
            elif lkey == "gap":
                rem.gap = strtonum(val)
                if val2 is not None and not val2.startswith("#"):
                    rem.gap2 = strtonum(val2)
            elif lkey in PAIR_KEYS and val2 is not None and not val2.startswith("#"):
                p, s = PAIR_KEYS[lkey]
                setattr(rem, p, strtonum(val))
                setattr(rem, s, strtonum(val2))
            elif lkey in INT_KEYS:
                setattr(rem, INT_KEYS[lkey], strtonum(val))
            elif lkey in IGNORED_KEYS:
                pass
            else:
                raise ConfigError("line %d: unknown definition %s" % (lineno, key))
        else:
            if lkey == "include":
                raise ConfigError("line %d: include is not supported" % lineno)
            raise ConfigError("line %d: %s outside of a remote" % (lineno, key))
    if mode is not None:
        raise ConfigError("unterminated remote")
    return remotes


def finalize_remote(rem):
    """sanityChecks() and the post processing in read_config()."""
    if not rem.name:
        raise ConfigError("missing remote name")
    if rem.raw:
        return
    if not rem.codes:
        raise ConfigError("%s: no codes" % rem.name)
    rem.pre_data &= gen_mask(rem.pre_data_bits)
    rem.post_data &= gen_mask(rem.post_data_bits)
    for code in rem.codes:
        code.codes = [c & gen_mask(rem.bits) for c in code.codes]
    if "REVERSE" in rem.flags:
        if rem.pre_data_bits:
            rem.pre_data = reverse(rem.pre_data, rem.pre_data_bits)
        if rem.post_data_bits:
            rem.post_data = reverse(rem.post_data, rem.post_data_bits)
        for code in rem.codes:
            code.codes = [reverse(c, rem.bits) for c in code.codes]
    if rem.protocol() == "RC6" and rem.rc6_mask == 0 and rem.toggle_bit > 0:
        rem.rc6_mask = 1 << (rem.bit_count() - rem.toggle_bit)
    if rem.toggle_bit > 0:
        if not rem.toggle_bit_mask:
            rem.toggle_bit_mask = 1 << (rem.bit_count() - rem.toggle_bit)
        rem.toggle_bit = 0


class SendBuffer:
    """The send buffer of lib/transmit.c. Spaces also record how much of
    them is gap, so PIIR can write gaps with dedicated symbols."""

    def __init__(self, zero_bug=False):
        self.zero_bug = zero_bug    # emulate LIRC 0.10.2 for zero durations
        self.clear()
        self.raw_ptr = None         # LIRC's send_buffer.data pointing at raw signals

    def clear(self):
        self.data = []
        self.gaps = []          # gap part of each value (spaces only)
        self.too_long = False
        self.pendingp = 0
        self.pendings = 0
        self.pendings_gap = 0
        self.sum = 0

    def add(self, value, gap=0):
        if len(self.data) < WBUF_SIZE:
            self.sum += value
            self.data.append(value)
            self.gaps.append(gap)
        else:
            self.too_long = True

    def pulse(self, value):
        if value or self.zero_bug:
            if self.pendingp > 0:
                self.pendingp += value
            else:
                if self.pendings > 0:
                    self.add(self.pendings, self.pendings_gap)
                    self.pendings = self.pendings_gap = 0
                self.pendingp = value

    def space(self, value, gap=False):
        if value or self.zero_bug:
            if not self.data and self.pendingp == 0:
                return  # first signal is a space
            if self.pendings > 0:
                self.pendings += value
            else:
                if self.pendingp > 0:
                    self.add(self.pendingp)
                    self.pendingp = 0
                self.pendings = value
                self.pendings_gap = 0
            if gap:
                self.pendings_gap += value

    def flush(self):
        if self.pendingp > 0:
            self.add(self.pendingp)
            self.pendingp = 0
        if self.pendings > 0:
            self.add(self.pendings, self.pendings_gap)
            self.pendings = self.pendings_gap = 0

    def sync(self):
        if self.pendingp > 0:
            self.add(self.pendingp)
            self.pendingp = 0
        if self.data and len(self.data) % 2 == 0:
            self.data.pop()
            self.gaps.pop()


class Transmitter:
    """lib/transmit.c send functions for one remote."""

    def __init__(self, remote, lirc_bugs=None):
        """lirc_bugs: None to send correctly, or a dict shared by all
        transmitters to emulate LIRC 0.10.2 including its bugs."""
        self.r = remote
        self.bugs = lirc_bugs
        self.buf = SendBuffer(lirc_bugs is not None)

    def header(self):
        if self.r.has_header():
            self.buf.pulse(self.r.phead)
            self.buf.space(self.r.shead)

    def foot(self):
        if self.r.has_foot():
            self.buf.space(self.r.sfoot)
            self.buf.pulse(self.r.pfoot)

    def lead(self):
        if self.r.plead:
            self.buf.pulse(self.r.plead)

    def trail(self):
        if self.r.ptrail:
            self.buf.pulse(self.r.ptrail)

    def toggle_bit(self, data, mask, toggle_bit_mask_bits):
        r = self.r
        if r.toggle_bit_mask and mask & r.toggle_bit_mask:
            if toggle_bit_mask_bits == 1:
                data &= ~1
                if r.toggle_bit_mask_state & mask:
                    data |= 1
            elif r.toggle_bit_mask_state & mask:
                data ^= 1
        if r.toggle_mask and mask & r.toggle_mask and r.toggle_mask_state % 2:
            data ^= 1
        return data

    def data(self, data, bits, done):
        r = self.r
        b = self.buf
        toggle_bit_mask_bits = bin(r.toggle_bit_mask).count("1")
        data = reverse(data, bits)
        # A negative shift is undefined in C; x86 uses the count modulo 64
        mask = (1 << ((r.bit_count() - 1 - done) % 64)) & gen_mask(64)
        for _ in range(bits):
            data = self.toggle_bit(data, mask, toggle_bit_mask_bits)
            if data & 1:
                if r.is_biphase():
                    if mask & r.rc6_mask:
                        b.space(2 * r.sone)
                        b.pulse(2 * r.pone)
                    else:
                        b.space(r.sone)
                        b.pulse(r.pone)
                elif r.protocol() == "SPACE_FIRST":
                    b.space(r.sone)
                    b.pulse(r.pone)
                else:
                    b.pulse(r.pone)
                    b.space(r.sone)
            else:
                if mask & r.rc6_mask:
                    b.pulse(2 * r.pzero)
                    b.space(2 * r.szero)
                elif r.protocol() == "SPACE_FIRST":
                    b.space(r.szero)
                    b.pulse(r.pzero)
                else:
                    b.pulse(r.pzero)
                    b.space(r.szero)
            data >>= 1
            mask >>= 1

    def pre(self):
        r = self.r
        if r.pre_data_bits:
            self.data(r.pre_data, r.pre_data_bits, 0)
            if r.pre_p > 0 and r.pre_s > 0:
                self.buf.pulse(r.pre_p)
                self.buf.space(r.pre_s)

    def post(self):
        r = self.r
        if r.post_data_bits:
            if r.post_p > 0 and r.post_s > 0:
                self.buf.pulse(r.post_p)
                self.buf.space(r.post_s)
            self.data(r.post_data, r.post_data_bits, r.pre_data_bits + r.bits)

    def repeat(self):
        self.lead()
        self.buf.pulse(self.r.prepeat)
        self.buf.space(self.r.srepeat)
        self.trail()

    def code(self, code, repeat):
        r = self.r
        if not repeat or "NO_HEAD_REP" not in r.flags:
            self.header()
        self.lead()
        self.pre()
        self.data(code, r.bits, r.pre_data_bits)
        self.post()
        self.trail()
        if not repeat or "NO_FOOT_REP" not in r.flags:
            self.foot()
        if not repeat and "NO_HEAD_REP" in r.flags and r.is_const():
            self.buf.sum -= r.phead + r.shead

    def init_send(self, code, state, repeat):
        """init_send_or_sim() with sim=0. state is a dict holding the
        transmit_state of the code. Returns the buffer and the gap, or None
        if LIRC cannot send it."""
        r = self.r
        b = self.buf
        b.clear()
        if not repeat:
            r.repeat_countdown = r.min_repeat
        chained = len(code.codes) > 1
        while True:
            if repeat and r.has_repeat():
                if "REPEAT_HEADER" in r.flags and r.has_header():
                    self.header()
                self.repeat()
            elif not r.raw:
                if state["next"] is None:
                    next_code = code.codes[0]
                else:
                    next_code = code.codes[state["next"]]
                if repeat and r.repeat_mask:
                    next_code ^= r.repeat_mask
                self.code(next_code, repeat)
                if self.bugs is not None:
                    self.bugs["raw_ptr"] = None
                if r.toggle_mask:
                    r.toggle_mask_state += 1
                    if r.toggle_mask_state == 4:
                        r.toggle_mask_state = 2
            else:
                if self.bugs is not None and not b.data:
                    self.bugs["raw_ptr"] = code.signals
                for value in code.signals:
                    b.add(value)
            b.sync()
            if b.too_long:
                return None

            if r.repeat_gap > 0 and repeat and r.has_repeat():
                r.min_remaining_gap = r.repeat_gap
            elif r.is_const():
                total = b.sum
                if r.min_gap() > total:
                    r.min_remaining_gap = r.min_gap() - total
                else:
                    return None  # too short gap
            else:
                r.min_remaining_gap = r.min_gap()

            if chained:
                if state["next"] is None:
                    state["next"] = 1
                else:
                    state["next"] += 1
                    if state["next"] >= len(code.codes):
                        state["next"] = None

            if (r.repeat_countdown > 0 or state["next"] is not None) \
                    and r.min_remaining_gap < LIRCD_EXACT_GAP_THRESHOLD:
                if not chained or state["next"] is None:
                    r.repeat_countdown -= 1
                if self.bugs is not None:
                    self.bugs["raw_ptr"] = None  # unrolling raw signal
                b.space(r.min_remaining_gap, gap=True)
                b.flush()
                b.sum = 0
                repeat = True
                continue
            break

        data = b.data
        if self.bugs is not None and self.bugs.get("raw_ptr") is not None:
            # LIRC writes len(data) values from where send_buffer.data points
            data = (self.bugs["raw_ptr"] + [0] * len(data))[:len(data)]
        if not data or any(v == 0 for v in data):
            return None
        return list(zip(data, b.gaps)), r.min_remaining_gap


def initial_toggle_state(remote):
    """toggle_bit_mask_state after reading the config (read_config())."""
    if remote.toggle_bit_mask and not remote.raw and remote.codes and remote.codes[0].codes:
        state = remote.codes[0].codes[0] & remote.toggle_bit_mask
        if state:
            state ^= remote.toggle_bit_mask
        return state
    return 0


def send_once(remote, code):
    """What lircd sends for SEND_ONCE right after it started, as a list of
    (duration, gap part) tuples alternating pulse and space, starting and
    ending with a pulse. Returns None if LIRC cannot send the code."""
    r = remote
    r.toggle_mask_state = 0
    r.toggle_bit_mask_state = initial_toggle_state(r)
    if r.toggle_bit_mask:
        r.toggle_bit_mask_state ^= r.toggle_bit_mask
    t = Transmitter(r)
    state = {"next": None}
    out = []

    def append(sent):
        if out:
            out.append((gap, gap))
        out.extend(sent)

    result = t.init_send(code, state, repeat=False)
    if result is None:
        return None
    sent, gap = result
    append(sent)
    # lircd's repeat timer
    chained = len(code.codes) > 1
    if r.repeat_countdown > 0 or chained:
        while True:
            if not chained or (state["next"] is not None
                               and state["next"] + 1 >= len(code.codes)):
                r.repeat_countdown -= 1
            result = t.init_send(code, state, repeat=True)
            if result is None:
                break
            sent, next_gap = result
            append(sent)
            gap = next_gap
            if r.repeat_countdown <= 0:
                break
    return out


def simsend(remote_list, count=1, keysym=None):
    """Lines LIRC's irsimsend writes to simsend.out for these remotes."""
    lines = []
    bugs = {"raw_ptr": None}
    last_code = None   # irsimsend's static last_code[32]
    if not any(r.manual_sort for r in remote_list):
        # sort_by_bit_count() in lib/config_file.c (a stable sort)
        remote_list = sorted(remote_list, key=lambda r: (
            r.raw, len(r.codes) if r.raw else r.bit_count()))
    for r in remote_list:
        codes = r.codes
        if keysym is not None:
            codes = [c for c in r.codes if c.name == keysym][:1]
        r.toggle_bit_mask_state = initial_toggle_state(r)
        for code in codes:
            t = Transmitter(r, lirc_bugs=bugs)
            state = {"next": None}
            r.toggle_mask_state = 0
            if r.toggle_bit_mask:
                r.toggle_bit_mask_state ^= r.toggle_bit_mask
            saved_min_repeat = r.min_repeat
            r.min_repeat = 0
            repeat_remote = len(code.name) < 32 and code.name == last_code
            for i in range(count):
                if not r.has_timing():
                    lines.append("code 0x%x" % code.codes[0] if code.codes else "code 0x0")
                    continue
                result = t.init_send(code, state, repeat=repeat_remote or i > 0)
                if result is not None:
                    sent, gap = result
                    for j, (value, _) in enumerate(sent):
                        lines.append("%s %d" % ("pulse" if j % 2 == 0 else "space", value))
                    lines.append("space %d" % gap)
            r.min_repeat = saved_min_repeat
            last_code = code.name[:32]
    return lines


# --- conversion to PIIR

def cluster(values, tolerance):
    """Group sorted durations, starting a new group when a value is more than
    tolerance(first value of the group) above the first value, and map each
    to the rounded mean of its group. Returns {value: representative}."""
    mapping = {}
    group = []

    def close():
        rep = int(round(sum(group) / len(group)))
        for v in group:
            mapping[v] = rep

    for v in sorted(set(values)):
        if group and v - group[0] > tolerance(group[0]):
            close()
            group = []
        group.append(v)
    if group:
        close()
    return mapping


def tokenize(signal):
    """Split a signal into (mark, space) pairs and gap spaces.
    Returns a list of ("pair", mark, space) and ("gap", duration)."""
    tokens = []
    for i in range(0, len(signal), 2):
        mark = signal[i][0]
        space, gap = (signal[i + 1] if i + 1 < len(signal) else (0, 0))
        tokens.append(("pair", mark, space - gap))
        if gap:
            tokens.append(("gap", gap))
    return tokens


def gap_symbols(duration):
    text = ""
    for symbol, unit in GAP_SYMBOLS:
        n, duration = divmod(duration, unit)
        text += symbol * n
    return text


def expand(code, symbols):
    """Durations PIIR sends for a symbol string (merging like src/lirc.c)."""
    out = []
    for ch in code:
        mark, space = symbols[ch]
        for is_pulse, value in ((True, mark), (False, space)):
            if not value:
                continue
            if not out and not is_pulse:
                continue
            if out and (len(out) % 2 == 1) == is_pulse:
                out[-1] += value
            else:
                out.append(value)
    if out and len(out) % 2 == 0:
        out.pop()
    return out


def convert_remote(remote, source=None):
    """Return (piir_json_dict, warnings) or raise ConfigError."""
    proto = remote.protocol()
    if not remote.has_timing():
        raise ConfigError("no timing: codes are receiver hardware scancodes")
    if proto in UNSUPPORTED:
        raise ConfigError("%s encoding is not supported" % proto)
    if proto is None:
        raise ConfigError("no encoding flag")

    warnings = []
    keys = {}
    for code in remote.codes:
        if code.name in keys:
            warnings.append("duplicate key %s ignored" % code.name)
            continue
        if not remote.raw and not code.codes:
            warnings.append("key %s has no code" % code.name)
            continue
        signal = send_once(remote, code)
        if signal is None:
            warnings.append("LIRC cannot send key %s" % code.name)
            continue
        if len(signal) > PIIR_MAX_LIRC_VALUES - 1:
            warnings.append("key %s needs %d pulses and spaces, more than PIIR supports"
                            % (code.name, len(signal)))
            continue
        keys[code.name] = signal
    if not keys:
        raise ConfigError("no key can be sent")

    tokenized = {name: tokenize(sig) for name, sig in keys.items()}

    # Quantize raw codes so the mark/space pairs fit the symbol alphabet
    pairs = {(t[1], t[2]) for toks in tokenized.values() for t in toks if t[0] == "pair"}
    marks = {p[0] for p in pairs}
    spaces = {p[1] for p in pairs}
    tolerance_pct = 0
    mark_map = {m: m for m in marks}
    space_map = {s: s for s in spaces}
    while len({(mark_map[m], space_map[s]) for m, s in pairs}) > len(SYMBOL_ALPHABET):
        tolerance_pct += 2
        if tolerance_pct > 20:
            raise ConfigError("too many different pulse/space pairs (%d)" % len(pairs))
        tol = lambda v: max(v * tolerance_pct / 100.0, 20)
        mark_map = cluster(marks, tol)
        space_map = cluster(spaces, tol)
    if tolerance_pct:
        warnings.append("durations rounded within %d%% to fit the symbol alphabet"
                        % tolerance_pct)

    def q(pair):
        return (mark_map[pair[0]], space_map[pair[1]])

    # Assign symbols, giving header/one/zero their conventional names
    usage = {}
    for toks in tokenized.values():
        for t in toks:
            if t[0] == "pair":
                usage[q((t[1], t[2]))] = usage.get(q((t[1], t[2])), 0) + 1
    preferred = {}
    if not remote.raw:
        if remote.protocol() in ("SPACE_ENC", "SPACE_FIRST"):
            preferred[(remote.pone, remote.sone)] = "1"
            preferred[(remote.pzero, remote.szero)] = "0"
        if remote.has_header():
            preferred[(remote.phead, remote.shead)] = "H"
    symbols = {}
    by_pair = {}
    free = [c for c in SYMBOL_ALPHABET]
    for pair, ch in preferred.items():
        if pair in usage and ch in free and pair not in by_pair:
            by_pair[pair] = ch
            free.remove(ch)
    for pair in sorted(usage, key=lambda p: (-usage[p], p)):
        if pair not in by_pair:
            by_pair[pair] = free.pop(0)
    for pair, ch in by_pair.items():
        symbols[ch] = pair

    features = {}
    for name, toks in tokenized.items():
        text = ""
        for t in toks:
            if t[0] == "pair":
                text += by_pair[q((t[1], t[2]))]
            else:
                text += gap_symbols(t[1])
        if len(text) > PIIR_MAX_COMMAND_SIZE:
            warnings.append("key %s needs %d symbols, more than PIIR supports"
                            % (name, len(text)))
            continue
        features[name] = text

    if not features:
        raise ConfigError("no key fits PIIR's limits")

    # Self check: PIIR must send what LIRC sends
    all_symbols = dict(symbols)
    all_symbols["-"] = (0, 0)
    for s, unit in GAP_SYMBOLS:
        all_symbols[s] = (0, unit)
    for name, text in features.items():
        expected = [v for v, _ in keys[name]]
        got = expand(text, all_symbols)
        if len(got) != len(expected):
            raise ConfigError("internal error: key %s converts to %d values instead of %d"
                              % (name, len(got), len(expected)))
        for e, g in zip(expected, got):
            if abs(e - g) > max(e * tolerance_pct / 100.0, 20 if tolerance_pct else 0):
                raise ConfigError("internal error: key %s converts %d to %d" % (name, e, g))

    symboldefs = {}
    for ch in sorted(symbols, key=SYMBOL_ALPHABET.index):
        symboldefs[ch] = {"mark": symbols[ch][0], "space": symbols[ch][1]}
    used_gaps = {c for text in features.values() for c in text}
    for s, unit in GAP_SYMBOLS:
        if s in used_gaps:
            symboldefs[s] = {"mark": 0, "space": unit}
    symboldefs["-"] = {"mark": 0, "space": 0}

    longest = max(len(t) for t in features.values())
    description = "%s (LIRC remote)" % remote.name
    result = {"description": description}
    if source:
        result["source"] = source
    freq = remote.freq if remote.freq > 0 else DEFAULT_FREQ
    duty = remote.duty_cycle if 0 < remote.duty_cycle < 100 else 50
    result.update({
        "frequency": freq,
        "dutycycle": duty / 100.0,
        "transmitter": {"outpin": 17},
        "symboldefinitions": symboldefs,
        "template": "-" * longest,
        "features": {"key": features},
    })
    return result, warnings


def safe_name(name):
    return re.sub(r"[^A-Za-z0-9._+-]", "_", name).strip("._") or "remote"


def output_base(rel_path):
    """remotes/sony/RM-ED009.lircd.conf -> sony/RM-ED009"""
    parts = rel_path.replace(os.sep, "/").split("/")
    filename = parts[-1]
    for suffix in (".lircd.conf", ".conf"):
        if filename.endswith(suffix):
            filename = filename[: -len(suffix)]
            break
    return "/".join([safe_name(p) for p in parts[:-1]] + [safe_name(filename)])


def find_configs(paths):
    for path in paths:
        if os.path.isdir(path):
            for root, dirs, files in os.walk(path):
                dirs.sort()
                for f in sorted(files):
                    if f.endswith(".conf"):
                        yield os.path.join(root, f), os.path.relpath(os.path.join(root, f), path)
        else:
            yield path, os.path.basename(path)


def write_json(path, data):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(format_json(data))


def format_json(data):
    """Pretty print with one key or symbol per line."""
    lines = ["{"]
    items = list(data.items())
    for i, (key, value) in enumerate(items):
        comma = "," if i < len(items) - 1 else ""
        if key == "symboldefinitions":
            lines.append('    "symboldefinitions": {')
            sym = list(value.items())
            for j, (s, d) in enumerate(sym):
                lines.append('\t%s: {"mark": %d, "space": %d}%s'
                             % (json.dumps(s), d["mark"], d["space"],
                                "," if j < len(sym) - 1 else ""))
            lines.append("    }" + comma)
        elif key == "features":
            lines.append('    "features": {')
            feats = list(value.items())
            for j, (fname, values) in enumerate(feats):
                lines.append("\t%s: {" % json.dumps(fname))
                vals = list(values.items())
                for k, (vname, code) in enumerate(vals):
                    lines.append("\t    %s: %s%s" % (json.dumps(vname, ensure_ascii=False),
                                                   json.dumps(code),
                                                   "," if k < len(vals) - 1 else ""))
                lines.append("\t}" + ("," if j < len(feats) - 1 else ""))
            lines.append("    }" + comma)
        else:
            text = json.dumps(value, ensure_ascii=False)
            lines.append("    %s: %s%s" % (json.dumps(key), text, comma))
    lines.append("}")
    return "\n".join(lines) + "\n"


def read_text(path):
    with open(path, "rb") as f:
        data = f.read()
    if data.startswith(b"\xef\xbb\xbf"):
        data = data[3:]  # UTF-8 byte order mark
    return data.decode("utf-8", errors="replace")


def cmd_convert(args):
    report = []
    written = 0
    for path, rel in find_configs(args.paths):
        base = output_base(rel)
        try:
            remotes = parse_config(read_text(path))
        except ConfigError as e:
            report.append("SKIP %s: %s" % (rel, e))
            continue
        if not remotes:
            report.append("SKIP %s: no remotes" % rel)
            continue
        names = set()
        for remote in remotes:
            name = base if len(remotes) == 1 else "%s_%s" % (base, safe_name(remote.name))
            if name in names:
                n = 2
                while "%s_%d" % (name, n) in names:
                    n += 1
                name = "%s_%d" % (name, n)
            names.add(name)
            source = {"lirc-remotes": "remotes/" + rel.replace(os.sep, "/"),
                      "remote": remote.name,
                      "converter": "tools/lirc2piir.py " + VERSION}
            try:
                data, warnings = convert_remote(remote, source)
            except ConfigError as e:
                report.append("SKIP %s [%s]: %s" % (rel, remote.name, e))
                continue
            for w in warnings:
                report.append("WARN %s [%s]: %s" % (rel, remote.name, w))
            write_json(os.path.join(args.output, name + ".json"), data)
            written += 1
    if args.report:
        with open(args.report, "w", encoding="utf-8") as f:
            f.write("\n".join(report) + ("\n" if report else ""))
    else:
        for line in report:
            print(line, file=sys.stderr)
    skipped = sum(1 for line in report if line.startswith("SKIP"))
    print("%d remotes converted, %d skipped" % (written, skipped))
    return 0


def cmd_simsend(args):
    remotes = parse_config(read_text(args.file))
    for line in simsend(remotes, args.count, args.keysym):
        print(line)
    return 0


def cmd_verify(args):
    """Compare simsend() with LIRC's irsimsend for every config file."""
    failures = 0
    checked = 0
    workdir = tempfile.mkdtemp(prefix="lirc2piir-")
    for path, rel in find_configs(args.paths):
        try:
            remotes = parse_config(read_text(path))
        except ConfigError:
            continue  # convert reports these
        if not remotes:
            continue
        if any(r.protocol() in UNSUPPORTED or not r.has_timing() for r in remotes):
            continue
        for count in (1, 2):
            out = os.path.join(workdir, "simsend.out")
            if os.path.exists(out):
                os.remove(out)
            cmd = [args.irsimsend, "-c", str(count)]
            if args.plugindir:
                cmd += ["-U", args.plugindir]
            cmd.append(os.path.abspath(path))
            proc = subprocess.run(cmd, cwd=workdir, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL, timeout=60)
            if not os.path.exists(out):
                if proc.returncode != 0:
                    continue  # LIRC cannot read this file either
                lirc_lines = []
            else:
                with open(out) as f:
                    lirc_lines = [l.strip() for l in f if l.strip()]
            ours = simsend(parse_config(read_text(path)), count)
            checked += 1
            if lirc_lines != ours:
                failures += 1
                diff = next((i for i, (a, b) in enumerate(zip(lirc_lines, ours)) if a != b),
                            min(len(lirc_lines), len(ours)))
                print("MISMATCH %s (count %d) at line %d: lirc %r, ours %r"
                      % (rel, count, diff + 1,
                         lirc_lines[diff] if diff < len(lirc_lines) else None,
                         ours[diff] if diff < len(ours) else None))
    print("%d checks, %d mismatches" % (checked, failures))
    return 1 if failures else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", action="version", version="%(prog)s " + VERSION)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("convert", help="convert lircd.conf files to PIIR JSON")
    p.add_argument("paths", nargs="+", help="lircd.conf files or directories")
    p.add_argument("-o", "--output", default=".", help="output directory")
    p.add_argument("--report", help="write skipped remotes and warnings to this file")
    p.set_defaults(func=cmd_convert)

    p = sub.add_parser("simsend", help="print irsimsend output using this encoder")
    p.add_argument("file")
    p.add_argument("-c", "--count", type=int, default=1)
    p.add_argument("-k", "--keysym")
    p.set_defaults(func=cmd_simsend)

    p = sub.add_parser("verify", help="compare the encoder with LIRC's irsimsend")
    p.add_argument("paths", nargs="+")
    p.add_argument("--irsimsend", default="irsimsend")
    p.add_argument("--plugindir", help="LIRC plugin directory (irsimsend -U)")
    p.set_defaults(func=cmd_verify)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
