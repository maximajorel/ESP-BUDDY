"""Round-trip check for the generated asset header.

`pack_web.py` turns the web app into C arrays. If it drops a byte, escapes
something wrong, or reports a length that does not match, the board serves a
broken page and the failure looks like a network problem. This decodes the
generated header back into bytes and compares it with `web_ui/`.

    python tools/verify_assets.py esp32_gadget/web_ui esp32_gadget/web_assets.h
"""

import os
import re
import sys


def parse_header(path):
    with open(path, "r", encoding="utf-8") as handle:
        text = handle.read()

    arrays = {}
    for match in re.finditer(
        r"static const uint8_t (asset_\w+)\[\] = \{(.*?)\};", text, re.S
    ):
        name, body = match.group(1), match.group(2)
        values = [int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", body)]
        arrays[name] = bytes(values)

    table = []
    for match in re.finditer(
        r'\{"(/[^"]*)",\s*"([^"]*)",\s*(asset_\w+),\s*sizeof\((asset_\w+)\)\}', text
    ):
        path_, mime, name, size_of = match.groups()
        if name != size_of:
            raise SystemExit("table row mismatch on %s" % path_)
        table.append((path_, mime, name))

    return arrays, table


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2

    root, header = argv[1], argv[2]
    arrays, table = parse_header(header)

    failures = 0
    seen = set()
    for route, mime, name in table:
        relative = route.lstrip("/")
        full = os.path.join(root, relative.replace("/", os.sep))
        if not os.path.isfile(full):
            print("FAIL %-16s source file missing: %s" % (route, full))
            failures += 1
            continue

        expected = open(full, "rb").read()
        actual = arrays.get(name)
        if actual is None:
            print("FAIL %-16s no byte array for %s" % (route, name))
            failures += 1
            continue

        seen.add(name)
        if actual == expected:
            print("ok   %-16s %6d bytes  %s" % (route, len(expected), mime))
        else:
            print(
                "FAIL %-16s header=%d source=%d first_diff=%s"
                % (route, len(actual), len(expected), first_difference(actual, expected))
            )
            failures += 1

    declared = set(re.findall(r"WEB_ASSET_COUNT = (\d+);", open(header, encoding="utf-8").read()))
    if declared and int(declared.pop()) != len(table):
        print("FAIL WEB_ASSET_COUNT does not match the table")
        failures += 1

    orphans = set(arrays) - seen
    if orphans:
        print("FAIL arrays with no table entry: %s" % sorted(orphans))
        failures += 1

    print("\n%s (%d problem%s)" % ("FAILED" if failures else "PASSED", failures,
                                   "" if failures == 1 else "s"))
    return 1 if failures else 0


def first_difference(a, b):
    for index in range(min(len(a), len(b))):
        if a[index] != b[index]:
            return "index %d (%02x vs %02x)" % (index, a[index], b[index])
    return "length only"


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
