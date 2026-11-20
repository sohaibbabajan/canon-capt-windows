# Hi-SCoA codec and raster-band milestone

Implemented and tested on Ubuntu x86_64 on 2026-11-16. This milestone provides
portable band preparation and compression. It does not implement a CAPT job,
Windows rendering, a USB adapter or a printer compatibility result.

## Audited sources and changes

The encoder in `src/hiscoa.cpp` adapts Alexey Galakhov's GPL-3.0-or-later
`captdriver/src/hiscoa-compress.c` at the reference commit recorded in
[NOTICE.md](../NOTICE.md). Its copyright and provenance are retained. No source
under `captdriver/` was changed. The production target has no CUPS dependency.

The port preserves the reference's greedy token selection, tie order, per-row
match boundaries, maximum match length of 639 bytes, 16-entry move-to-front
stash, origin exchanges, parameter bytes, XOR `0x43` and termination behavior.
It replaces the silently truncating writer and unchecked arithmetic with
validated inputs and bounded output that throws on failure. Indexes use
`size_t`; parameter fields are checked before wire encoding. Literal/stash
operations change the stash; long repeats do not.

Two details were resolved using source inspection and fixed bitstream fixtures:

* `SPECS` §3.3's prefix formula says `128 * ((1 << order) - 1 - N)`. The actual
  encoder and reference test decoder agree on
  `128 * ((1 << (order + 1)) - 1 - N)`. For example, prefix order `00` and no
  following value bits mean 128, not zero. The port follows the implementation.
* The reference always appends **1 through 32 one bits** after END. If END is
  already word aligned, it adds a complete all-ones word (`BC BC BC BC` after
  XOR). The port preserves that behavior. The old test decoder only advances
  to the next alignment boundary and can leave this extra word unconsumed.
  Neither observation establishes that the printer requires the extra word.

Resolved nonpositive row-relative origins are disabled instead of undergoing
unsigned wraparound. Only backward matches into available input are emitted.
This agrees with the tested reference outputs, including narrow rows. L3/L5
must be positive signed-byte values; L0/L2 must fit signed bytes; L4 is supported
from 0 (disabled) through 32767. Unsupported parameter values are rejected.
These are codec validation rules, not an assertion that every permitted tuple
is supported by an LBP6000.

The production decoder has deliberately not been introduced. A separate,
bounded **test-only** decoder validates output without sharing encoder helpers.
It checks output size, stash/back-reference validity, END and reference-style
padding. Its interpretation derives from the same reverse-engineered material,
so byte fixtures and differential checks provide additional evidence.

## Codec contract and memory

`capt::hiscoa::BandView` describes exactly `line_size * rows` packed bytes.
Both dimensions must be in [1,65535]. Zero-sized bands, null data, truncated or
surplus bytes and unknown end markers are rejected before compression. The
input band is limited to 16 MiB as a project memory policy. Normal 160-row bands
are much smaller. This limit is not a discovered device capability.

`maximum_compressed_size(N)` uses at most 12 literal bits per input byte plus
the 10-bit END and 1..32 padding bits:

```text
maximum bytes = 4 * (floor((12*N + 10) / 32) + 1)
```

The calculation checks overflow. The encoder reserves at most this bound or
the caller's smaller output limit. Insufficient output space throws
`std::length_error`; no partially encoded band is returned. A caller may set
the limit to the actual compressed size even when it is below the worst-case
bound. At the 16 MiB input limit, the worst-case output is about 24 MiB plus
padding. The library does not allocate an entire job or retain prior bands.

The default parameter packet is `01 04 01 01 00 F9 00 00`. The band marker is
`00`, and the page marker is `01`. The reference's actual printing helper
`src/generic-ops.c` passes the normal **band** marker for every band and uses
the separate CAPT `0xC0A4` command to end a page. The synthetic tests also cover
the page marker, but future model/job logic must select marker policy from
validated printer behavior rather than changing the reference job sequence.

## Raster-band contract

`prepare_monochrome_band` consumes an 8-bit grayscale image view with explicit
buffer size, dimensions and source stride. It copies a caller-specified source
rectangle to an explicit destination pixel position. Rectangles must fit; there
is no inferred paper geometry, implicit centering, rescaling or silent cropping.

The caller specifies output page width/height in pixels and packed bytes per
row. Source rows may contain padding, and output rows may be padded beyond
`ceil(width/8)`. The function allocates only the requested band, stops at page
end and leaves margins, unused tail bits and output byte padding white.
It validates source extents without overflowing arithmetic and caps each
allocated band at 16 MiB. Empty placement plus an empty source produces blank
page bands. Thresholding uses the existing `sample < threshold` convention.

An integration pattern is:

```cpp
const auto band = capt::prepare_monochrome_band(source, layout, first_row, 160);
const auto bytes = capt::hiscoa::compress_band(
    {band.pixels.data(), band.pixels.size(), band.line_size, band.rows});
```

These bytes still require model setup, flow control, CAPT framing and transport.
The API does not infer DPI, physical printable margins, supported media or fuser
settings. Actual page geometry and physical black/white polarity remain hardware
validation items. Color/alpha conversion and the final image halftoner are later
rendering work.

## Fixed synthetic byte fixtures

Fixtures were assembled from token bits, padded with ones and XORed with `0x43`.
They were not generated by the new encoder or captured from a printer.
`BYTE(AA)` is `1101 10101010`; `END(band)` is `11111110 00`.

| Raw row | Token bits before padding | Encoded hexadecimal bytes |
| --- | --- | --- |
| `00` | `11111101 11111110 00` | `BE BD 7C BC` |
| `00`, page end | `11111101 11111110 01` | `BE BD 3C BC` |
| `AA` | `110110101010 11111110 00` | `99 EC A0 BC` |
| Two `AA` | `BYTE(AA) 10 1111 END(band)` | `99 E8 BC CC` |
| Three `AA` | `BYTE(AA) 1110 011 END(band)` | `99 ED 3C 84` |
| Nine `AA` | `BYTE(AA) 1110 110111 END(band)` | `99 ED 9C BB BC BC BC BC` |
| 129 `AA` | `BYTE(AA) 11111100 00 1110 111111 END(band)` | `99 EC 80 FC BD 7C BC BC` |

The nine-byte row's END lands exactly at bit 32 and exercises the extra word.
The 129-byte row exercises prefix 128 with a zero repeat remainder.

## Verification performed

The new suite validates **1,042 bands** using the separate decoder. The optional
reference build also checks every one byte-for-byte against the unchanged C
encoder. Inputs include zero/black bands, full-byte and small-palette seeded
data, narrow rows, 595/640-byte rows, 160-row bands, three parameter tuples,
all five long-repeat commands, stash eviction, origin swaps and prefix/match
boundaries. The xorshift32 seed is `0x6000B123`.

Seven fixed wire fixtures, exact/insufficient capacity, malformed dimensions,
parameter ranges, extent overflow, memory caps, decoder corruption/truncation,
and grayscale-to-band-to-codec integration supplement the round trips.
Debug, Release, differential Debug and ASan/UBSan differential builds each
passed all three CTest suites. See [TESTING.md](../TESTING.md) for commands.
No Windows ARM64 or physical printer test was performed.
