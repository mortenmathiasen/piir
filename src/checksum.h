#ifndef CHECKSUM_H
#define CHECKSUM_H

#include "parson.h"

// Declarative checksums, the "checksums" array of a remote, e.g.
//   "checksums": [
//     {"type": "sum", "from": 8, "to": 25, "at": 26}
//   ]
// The bit symbols of a code ("0" and "1" unless "bits" names others) are
// read as units of 8 bits (bytes) or 4 bits (nibbles), numbered from 0 in
// the order they are sent, ignoring all other symbols. Each checksum
// combines the units "from" to "to" (inclusive), or those listed in "data",
// and writes the result into unit "at", or into the units of a list "at"
// (up to 32 bits; with "lsb" the first holds the lowest bits, with "msb" the
// highest):
//   type      "sum" (modulo 256 or 16), "xor", "popcount" (the number of
//             bits set) or "crc"
//   unit      "byte" (default) or "nibble"
//   data      list of unit numbers, instead of "from" and "to"
//   init      start value, at most the width of "at" (default 0)
//   invert    complement the result, ~x (default false)
//   negate    negate the result, 0 - x (default false), applied after invert
//   xorout    XOR the result with this (default 0), applied last
//   poly      "crc" only: the polynomial without its highest bit (required)
//   reflect   "crc" only: reflected, i.e. each unit enters the CRC lowest bit
//             first and init and the result are reversed (default false);
//             poly, init, reflect and xorout are those of the CRC catalogue
//   bitorder  "lsb" (default) when the first bit of a unit is its lowest,
//             "msb" when it is its highest
//   bits      the zero and the one symbol (default "01")
//   restart   symbols after which units are counted from a new bit, e.g. the
//             gap between frames; the bits of a partial unit before them
//             are skipped (default none)
// Checksums are applied in order, so a later one may cover an earlier one.

// Apply the checksums to code. Returns 0 on success, 1 when a checksum is
// invalid or does not fit the code (logged; code may be partly changed).
int applyChecksums(const JSON_Array *checksums, char *code);

// Apply the checksums of the loaded remote, if it has any
int applyConfigChecksums(char *code);

#endif
