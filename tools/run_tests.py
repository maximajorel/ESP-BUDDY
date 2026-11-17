"""Run every host-side check for the gadget in one command.

The firmware cannot be unit-tested on the board, so the parts that are pure C++
(or pure enough to compile without Arduino) live in `esp32_gadget/*.h/.cpp` and
are pinned by the tests in `tools/*_test.cpp`. Running them one g++ command at a
time is easy to get wrong, and a test that silently stops compiling looks like a
test that passes. This runner compiles and executes all of them, plus the
round-trip check for the generated web asset header.

    python tools/run_tests.py              # everything
    python tools/run_tests.py --filter fold
    python tools/run_tests.py --verbose    # show each test's output

Exit code is 0 only when every test passed and the asset check is clean.

Requirements: a C++17 compiler (`g++` or `$CXX`) and Python 3. The Spotify test
also needs the ArduinoJson headers; the runner finds them the same way the
Arduino IDE does, or reads `$ARDUINOJSON_DIR` (the `.../ArduinoJson/src`
directory). When ArduinoJson is missing that single test is reported as skipped,
never as passed.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIRMWARE = ROOT / "esp32_gadget"
TOOLS = ROOT / "tools"

# Tests that need more than `-I esp32_gadget <test>.cpp`. Keep this table next to
# the code it describes; a new test either needs nothing or appears here.
COMPANION_SOURCES = {
    "text_fold_test": ["text_fold.cpp"],
    "spotify_core_test": ["spotify_parse.cpp", "spotify_pkce.cpp"],
}

# Tests that include ArduinoJson headers.
ARDUINOJSON_TESTS = {"spotify_core_test"}


def find_compiler():
    """Return the C++ compiler to use, or None when there is none."""
    for candidate in (os.environ.get("CXX"), "g++", "clang++"):
        if candidate and shutil.which(candidate):
            return candidate
    return None


def find_arduinojson():
    """Locate the ArduinoJson `src` directory the way the Arduino IDE lays it out."""
    env = os.environ.get("ARDUINOJSON_DIR")
    if env and (Path(env) / "ArduinoJson.h").exists():
        return Path(env)

    home = Path.home()
    candidates = [
        home / "Documents" / "Arduino" / "libraries" / "ArduinoJson" / "src",
        home / "Arduino" / "libraries" / "ArduinoJson" / "src",
        home / ".arduino15" / "libraries" / "ArduinoJson" / "src",
        home / "AppData" / "Local" / "Arduino15" / "libraries" / "ArduinoJson" / "src",
    ]
    # A Windows Arduino install can be reached from WSL through /mnt/c/Users.
    for mount in Path("/mnt/c/Users").glob("*/Documents/Arduino/libraries"):
        candidates.append(mount / "ArduinoJson" / "src")
    for path in candidates:
        if (path / "ArduinoJson.h").exists():
            return path
    return None


def discover_tests():
    return sorted(TOOLS.glob("*_test.cpp"))


def compile_test(compiler, test, build_dir, arduinojson):
    """Compile one test. Returns (binary, error_output_or_None)."""
    name = test.stem
    sources = [test] + [FIRMWARE / s for s in COMPANION_SOURCES.get(name, [])]
    binary = build_dir / (name + (".exe" if os.name == "nt" else ""))
    cmd = [compiler, "-std=c++17", "-O1", "-I", str(FIRMWARE)]
    if name in ARDUINOJSON_TESTS:
        cmd += ["-I", str(arduinojson)]
    cmd += [str(s) for s in sources] + ["-o", str(binary)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        return binary, result.stdout + result.stderr
    return binary, None


def run_binary(binary):
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    return result.returncode, result.stdout + result.stderr


def check_assets():
    """Run the generated-header round trip and return (ok, output)."""
    result = subprocess.run(
        [
            sys.executable,
            str(TOOLS / "verify_assets.py"),
            str(FIRMWARE / "web_ui"),
            str(FIRMWARE / "web_assets.h"),
        ],
        capture_output=True,
        text=True,
    )
    return result.returncode == 0, result.stdout + result.stderr


def tail(text, lines=20):
    return "\n".join(text.strip().splitlines()[-lines:])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--filter", default="", help="only run tests whose name contains this")
    parser.add_argument("--verbose", action="store_true", help="print every test's output")
    parser.add_argument("--no-assets", action="store_true", help="skip the generated web asset check")
    args = parser.parse_args(argv)

    compiler = find_compiler()
    if compiler is None:
        print("FAIL no C++17 compiler found (install g++ or set $CXX)")
        return 1
    arduinojson = find_arduinojson()

    tests = [t for t in discover_tests() if args.filter in t.stem]
    if not tests:
        print("FAIL no tests matched filter %r" % args.filter)
        return 1

    passed, failed, skipped = [], [], []
    with tempfile.TemporaryDirectory(prefix="gadget-tests-") as tmp:
        build_dir = Path(tmp)
        for test in tests:
            name = test.stem
            if name in ARDUINOJSON_TESTS and arduinojson is None:
                print("SKIP %-26s (ArduinoJson not found; set $ARDUINOJSON_DIR)" % name)
                skipped.append(name)
                continue

            binary, compile_error = compile_test(compiler, test, build_dir, arduinojson)
            if compile_error is not None:
                print("FAIL %-26s compile" % name)
                print("     " + tail(compile_error).replace("\n", "\n     "))
                failed.append(name)
                continue

            code, output = run_binary(binary)
            if code != 0:
                print("FAIL %-26s exit %d" % (name, code))
                print("     " + tail(output).replace("\n", "\n     "))
                failed.append(name)
                continue

            if args.verbose:
                print("ok   %-26s\n%s" % (name, output))
            else:
                print("ok   %-26s %s" % (name, output.strip().splitlines()[-1]))
            passed.append(name)

    asset_ok = True
    if not args.no_assets:
        asset_ok, asset_output = check_assets()
        if asset_ok:
            print("ok   %-26s %s" % ("web_assets.h", tail(asset_output, 1)))
        else:
            print("FAIL %-26s round-trip" % "web_assets.h")
            print("     " + tail(asset_output).replace("\n", "\n     "))
            print("     run: python tools/pack_web.py esp32_gadget/web_ui esp32_gadget/web_assets.h")

    print()
    summary = "%d passed" % len(passed)
    if skipped:
        summary += ", %d skipped" % len(skipped)
    if failed or not asset_ok:
        summary += ", %d failed" % (len(failed) + (0 if asset_ok else 1))
    print(summary)
    return 0 if not failed and asset_ok else 1


if __name__ == "__main__":
    sys.exit(main())
