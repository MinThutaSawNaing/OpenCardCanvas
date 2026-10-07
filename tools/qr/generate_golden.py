#!/usr/bin/env python
"""Generate golden QR module matrices with segno (an independent encoder).

Emits D:/OpenCardCanvas/tests/data/qr_golden.json (or the path in --out) with
one entry per (text, ecc) case:

    { "cases": [ { "text": ..., "ecc": "L|M|Q|H", "version": n, "mode": "...",
                   "size": n,
                   "modules": "<rows joined by '/', '1' = dark, '0' = light>" } ] }

`encoding='utf-8'` is used so that segno's byte mode uses UTF-8 (matching the
QrEncoder contract) without writing an ECI designator.
"""
import argparse
import json
import os
import sys

import segno

DEFAULT_OUT = r"D:/OpenCardCanvas/tests/data/qr_golden.json"


def version_for(text, ecc):
    try:
        return int(segno.make(text, error=ecc, micro=False, boost_error=False,
                              encoding="utf-8").version)
    except segno.DataOverflowError:
        return 9999


def smallest_len_for_version(target, ecc, ch="x"):
    """Smallest byte-payload length whose minimal version is `target`."""
    lo, hi = 1, 3600
    while lo < hi:
        mid = (lo + hi) // 2
        if version_for(ch * mid, ecc) >= target:
            hi = mid
        else:
            lo = mid + 1
    assert version_for(ch * lo, ecc) == target, (target, ecc, lo)
    return lo


def build_cases():
    """Return a list of (text, ecc, forced_version_or_None)."""
    # ASCII payloads where latin-1 == utf-8, plus explicit unicode cases.
    cases = [
        # --- short / simple, natural version selection --------------------
        ("0", "L", None),
        ("1", "M", None),
        ("42", "Q", None),
        ("12345", "H", None),
        ("8675309", "L", None),
        ("1234567890", "M", None),
        ("98765432109876543210", "Q", None),
        ("HELLO WORLD", "L", None),
        ("HELLO WORLD 123", "M", None),
        ("ABC-123/$%", "Q", None),
        ("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "H", None),
        ("..--++", "M", None),
        ("$%*+-./:", "Q", None),
        (" ", "H", None),
        ("00000000", "H", None),
        ("A", "M", None),
        ("a", "Q", None),
        ("", "L", None),
        # --- URLs / mixed case / byte -------------------------------------
        ("https://example.com", "L", None),
        ("https://opencardcanvas.example/card/1234?x=1&y=2", "M", None),
        ("mailto:someone@example.com", "Q", None),
        ("Mixed Case Text 42", "H", None),
        ("The quick brown fox jumps over the lazy dog.", "L", None),
        # --- unicode / UTF-8 ----------------------------------------------
        ("h\u00e9llo w\u00f6rld", "M", None),
        ("\u65e5\u672c\u8a9e\u30c6\u30b9\u30c8", "Q", None),
        ("caf\u00e9 \u2615 123", "H", None),
        ("\u00c5ngstr\u00f6m \u2013 \u00e9l\u00e8ve", "L", None),
        # --- longer payloads forcing higher versions ----------------------
        ("1" * 40, "L", None),
        ("1" * 90, "Q", None),
        ("2" * 120, "M", None),
        ("2" * 160, "H", None),
        ("3" * 80, "L", None),
        ("4" * 200, "L", None),
        ("Lorem ipsum dolor sit amet, consectetur adipiscing elit.", "M", None),
        ("https://example.com/" + "x" * 60, "L", None),
        ("UPPERCASE ALPHANUMERIC 0123456789 " * 3, "Q", None),
        ("Mixed-case payload with 0123456789 and symbols !?@#" * 3, "H", None),
    ]

    # --- payload lengths chosen so segno lands on exactly versions 5..10 ---
    # The encoder always picks the smallest version that fits, so the *payload
    # length* must drive the version (segno's version= would force a bigger,
    # zero-padded symbol that a minimal-version encoder can never reproduce).
    ecc_cycle = ["L", "M", "Q", "H"]
    for v in range(5, 11):
        ecc = ecc_cycle[(v - 5) % 4]
        cases.append(("x" * smallest_len_for_version(v, ecc), ecc, None))
    # A few larger versions to exercise the full capacity tables, the version
    # information block (v >= 7) and the irregular alignment step (v == 32).
    for v in (15, 20, 27, 32, 40):
        ecc = ecc_cycle[v % 4]
        cases.append(("x" * smallest_len_for_version(v, ecc), ecc, None))
    return cases


def encode_case(text, ecc, version):
    kwargs = dict(error=ecc, micro=False, boost_error=False, encoding="utf-8")
    if version is not None:
        kwargs["version"] = version
    qr = segno.make(text, **kwargs)
    rows = ["".join("1" if bit else "0" for bit in row) for row in qr.matrix]
    return {
        "text": text,
        "ecc": ecc,
        "version": int(qr.version),
        "mode": qr.mode,
        "size": len(rows),
        "modules": "/".join(rows),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default=DEFAULT_OUT)
    args = parser.parse_args()

    out = []
    for text, ecc, version in build_cases():
        out.append(encode_case(text, ecc, version))

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as fh:
        json.dump({"cases": out}, fh, ensure_ascii=True, indent=1)

    versions = sorted({c["version"] for c in out})
    eccs = sorted({c["ecc"] for c in out})
    modes = sorted({c["mode"] for c in out})
    print("wrote %d cases to %s" % (len(out), args.out))
    print("versions:", versions)
    print("ecc levels:", eccs)
    print("modes:", modes)
    for c in out:
        assert c["size"] == 17 + 4 * c["version"], c
    return 0


if __name__ == "__main__":
    sys.exit(main())
