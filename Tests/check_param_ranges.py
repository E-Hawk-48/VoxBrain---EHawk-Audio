#!/usr/bin/env python3
"""
check_param_ranges.py — verify every table-driven parameter target names a REAL
parameter and sits inside its DECLARED range.

WHY THIS EXISTS AS A SCRIPT AND NOT A C++ TEST
----------------------------------------------
GenreProfiles (46 genres, ~1000 targets) and VoiceCharacter (14 characters)
describe settings as raw {paramId, value} pairs. A typo in an id, or a value
that drifts outside a range after somebody narrows a slider, is invisible at
compile time: the target simply does nothing, silently, forever.

The obvious check is to instantiate the real APVTS and ask it. That requires
linking juce_audio_processors, which drags in juce_gui_basics and therefore X11
development headers that the Linux verification sandbox does not have (and
cannot install without root). So this script reads the ACTUAL declarations out
of Source/Parameters.cpp and Source/ParameterIDs.h instead. It is parsing the
same source of truth the APVTS is built from, which makes it equally
authoritative about ranges.

Run from the repo root:      python Tests/check_param_ranges.py
Exit code 0 = everything is in range, 1 = at least one problem.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PARAMS_CPP = ROOT / "Source" / "Parameters.cpp"
IDS_H = ROOT / "Source" / "ParameterIDs.h"
TABLES = [
    ROOT / "Source" / "Brain" / "GenreProfiles.cpp",
    ROOT / "Source" / "DSP" / "VoiceCharacter.cpp",
]

# Helper factories in Parameters.cpp and the ranges they produce.
HELPER_RANGES = {
    "pct": (0.0, 100.0),
    "onOff": (0.0, 1.0),
}


def read(path):
    return path.read_text(encoding="utf-8", errors="replace")


def constant_names():
    """symbol -> parameter id string, from ParameterIDs.h"""
    out = {}
    for m in re.finditer(
        r'inline\s+constexpr\s+auto\s+(\w+)\s*=\s*"([^"]+)"', read(IDS_H)
    ):
        out[m.group(1)] = m.group(2)
    return out


def declared_ranges(symbols):
    """parameter id -> (lo, hi) or None for choice/unbounded parameters."""
    src = read(PARAMS_CPP)
    ranges = {}

    def resolve(tok):
        tok = tok.strip()
        return symbols.get(tok, tok.strip('"'))

    # db(id,"Name",lo,hi,def) / hz(...) / ms(...)
    for m in re.finditer(
        r"\b(?:db|hz|ms)\s*\(\s*(\w+)\s*,\s*\"[^\"]*\"\s*,\s*"
        r"(-?[\d.]+)f?\s*,\s*(-?[\d.]+)f?\s*,",
        src,
    ):
        ranges[resolve(m.group(1))] = (float(m.group(2)), float(m.group(3)))

    # pct(id,"Name",def) and onOff(id,"Name",def)
    for helper, rng in HELPER_RANGES.items():
        for m in re.finditer(rf"\b{helper}\s*\(\s*(\w+)\s*,", src):
            ranges[resolve(m.group(1))] = rng

    # Explicit APF with a Range{lo, hi, step[, skew]}
    for m in re.finditer(
        r"ParameterID\s*\{\s*(\w+)\s*,\s*1\s*\}\s*,\s*\"[^\"]*\"\s*,\s*"
        r"Range\s*\{\s*(-?[\d.]+)f?\s*,\s*(-?[\d.]+)f?",
        src,
    ):
        ranges[resolve(m.group(1))] = (float(m.group(2)), float(m.group(3)))

    # AudioParameterChoice — range is 0..(count-1)
    for m in re.finditer(
        r"AudioParameterChoice>\s*\(\s*\n?\s*juce::ParameterID\s*\{\s*(\w+)\s*,\s*1\s*\}\s*,"
        r"\s*\"[^\"]*\"\s*,\s*juce::StringArray\s*\{(.*?)\}",
        src,
        re.S,
    ):
        count = len(re.findall(r'"[^"]*"', m.group(2)))
        if count:
            ranges[resolve(m.group(1))] = (0.0, float(count - 1))

    # Choice lists built from a table (e.g. the Voice menu). The count is not
    # visible here, so record the id with an open upper bound rather than
    # pretending it does not exist — a target still has to name a real id.
    for m in re.finditer(
        r"ParameterID\s*\{\s*(\w+)\s*,\s*1\s*\}\s*,\s*\"[^\"]*\"\s*,\s*"
        r"vf::\w+::\w+\s*\(\s*\)",
        src,
    ):
        ranges.setdefault(resolve(m.group(1)), None)

    return ranges


def targets_in(path, symbols):
    """[(paramId, value, line)] for every { id, value } pair in a table file."""
    found = []
    for lineno, line in enumerate(read(path).splitlines(), 1):
        if line.lstrip().startswith("//"):
            continue
        for m in re.finditer(r"\{\s*(\w+)\s*,\s*(-?[\d.]+)f?\s*\}", line):
            sym, val = m.group(1), m.group(2)
            if sym in symbols:
                found.append((symbols[sym], float(val), lineno))
    return found


def main():
    symbols = constant_names()
    ranges = declared_ranges(symbols)
    if not symbols or not ranges:
        print("could not parse the parameter declarations — check the paths")
        return 1

    print(f"parsed {len(symbols)} parameter ids, {len(ranges)} with declared ranges\n")

    problems, checked = [], 0
    for table in TABLES:
        if not table.exists():
            continue
        rows = targets_in(table, symbols)
        checked += len(rows)
        for pid, value, lineno in rows:
            if pid not in ranges:
                problems.append(f"{table.name}:{lineno}  {pid} is not a declared parameter")
                continue
            rng = ranges[pid]
            if rng is None:
                continue  # id is real; bounds are table-driven
            lo, hi = rng
            if not (lo - 1e-4 <= value <= hi + 1e-4):
                problems.append(
                    f"{table.name}:{lineno}  {pid} = {value} is outside [{lo}, {hi}]"
                )
        print(f"  {table.name}: {len(rows)} targets")

    print(f"\nchecked {checked} targets")
    if problems:
        print(f"\n{len(problems)} PROBLEM(S):")
        for p in problems:
            print("  * " + p)
        return 1

    print("all targets name a real parameter and sit inside its range")
    return 0


if __name__ == "__main__":
    sys.exit(main())
