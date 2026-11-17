"""Host-side test for the `.env`-to-header build step (S2 of web-auth).

    python tools/admin_seed_test.py

The firmware and this build step derive the credential digest independently, one
in C++ and one with hashlib. If they ever disagree the owner flashes a device
that rejects the password they just wrote into `.env`, and no screen explains
why. The pin is the same fixed vector `tools/admin_auth_test.cpp` checks:
salt 00..0f, user `admin`, password `s3cret-pass`.

The rest of the cases are the ones that would leave a bad seed behind: a short
password, a missing key, a name with a space, and a `.env` that was deleted after
a header had been generated.
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GENERATOR = ROOT / "tools" / "gen_admin_defaults.py"
SALT_HEX = "000102030405060708090a0b0c0d0e0f"
REFERENCE = "6e0c104b74e78fb9e3616b8c0051fbf0675c5702f7bfa3cbc8bf9fb4f90b79ba"

failures = 0
checks = 0


def check(what, ok, detail=""):
    global failures, checks
    checks += 1
    if ok:
        print("ok   %s" % what)
    else:
        failures += 1
        print("FAIL %s%s" % (what, (": " + detail) if detail else ""))


def run(env_path, out_path, *extra):
    command = [
        sys.executable,
        str(GENERATOR),
        "--env",
        str(env_path),
        "--out",
        str(out_path),
        *extra,
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    return result.returncode, result.stdout + result.stderr


def read_hex(header, name, length):
    match = re.search(r"ADMIN_SEED_%s\[%d\] = \{(.*?)\}" % (name, length), header, re.S)
    if not match:
        return None
    values = [int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1))]
    return bytes(values).hex()


def write_env(directory, body):
    path = Path(directory) / ".env"
    path.write_text(body, encoding="utf-8", newline="\n")
    return path


def main():
    with tempfile.TemporaryDirectory() as work:
        out = Path(work) / "admin_seed.h"
        env = write_env(work, "ADMIN_USER=admin\nADMIN_PASS=s3cret-pass\n")

        code, output = run(env, out, "--salt", SALT_HEX)
        check("S2 the generator succeeds on a valid .env", code == 0, output)
        check("S2 the header is written", out.exists())
        header = out.read_text(encoding="utf-8") if out.exists() else ""

        check("S2 salt is the requested one", read_hex(header, "SALT", 16) == SALT_HEX)
        check(
            "S2 the digest matches the firmware's reference vector",
            read_hex(header, "HASH", 32) == REFERENCE,
            "got %s" % read_hex(header, "HASH", 32),
        )
        check("S2 the header names the user", '#define ADMIN_SEED_USER "admin"' in header)
        check("S2 the header is marked present", "#define ADMIN_SEED_PRESENT 1" in header)
        check("S2 the plaintext password is absent from the header", "s3cret-pass" not in header)
        check("S2 the header is a single header file", "#pragma once" in header)
        check("S2 the header is marked generated", "gen_admin_defaults.py" in header)

        # Re-running with the same input must not churn the firmware build.
        before = header
        code, output = run(env, out, "--salt", SALT_HEX)
        check("S2 a second run is a no-op", code == 0, output)
        check(
            "S2 a second run keeps the header byte-identical",
            out.read_text(encoding="utf-8") == before,
        )
        check("S2 a second run says it was already up to date", "al dia" in output)

        # Reusing the existing salt without --salt keeps the digest stable.
        code, output = run(env, out)
        check("S2 a run without --salt keeps the stored salt", code == 0, output)
        check(
            "S2 the digest survives its own regeneration",
            read_hex(out.read_text(encoding="utf-8"), "HASH", 32) == REFERENCE,
        )

        # Short password.
        short = write_env(work, "ADMIN_USER=admin\nADMIN_PASS=short\n")
        bad_out = Path(work) / "short_seed.h"
        code, output = run(short, bad_out, "--salt", SALT_HEX)
        check("S2 a 5-character password is refused", code == 2, output)
        check("S2 a refused password writes no header", not bad_out.exists())
        check("S2 the refusal names ADMIN_PASS", "ADMIN_PASS" in output)

        # Missing key.
        incomplete = write_env(work, "ADMIN_USER=admin\n")
        code, output = run(incomplete, bad_out, "--salt", SALT_HEX)
        check("S2 a .env without ADMIN_PASS is refused", code == 2, output)
        check("S2 the refusal names the missing key", "ADMIN_PASS" in output)

        # Unusable names.
        for name, label in [
            ("ad min", "a space"),
            ("ad:min", "a colon"),
            ("", "empty"),
            ("x" * 33, "33 characters"),
        ]:
            spaced = write_env(work, "ADMIN_USER=%s\nADMIN_PASS=s3cret-pass\n" % name)
            code, output = run(spaced, bad_out, "--salt", SALT_HEX)
            check("S2 a user with %s is refused" % label, code == 2, output)

        # 64 characters is the ceiling and it is accepted.
        long_env = write_env(work, "ADMIN_USER=owner\nADMIN_PASS=%s\n" % ("p" * 64))
        long_out = Path(work) / "long_seed.h"
        code, output = run(long_env, long_out, "--salt", SALT_HEX)
        check("S2 a 64-character password is accepted", code == 0, output)

        # A deleted .env must not leave a stale seed behind.
        deleted = Path(work) / "deleted_seed.h"
        code, output = run(env, deleted, "--salt", SALT_HEX)
        check("S2 the seed is written while the .env exists", code == 0, output)
        env.unlink()
        code, output = run(env, deleted)
        check("S2 a missing .env still succeeds", code == 0, output)
        check("S2 a missing .env removes the stale header", not deleted.exists())
        check("S2 the message names the admin/admin fallback", "admin/admin" in output)

        # Comments, quotes and `export` are what a hand-written .env looks like.
        styled = write_env(
            work,
            "# credenciales\nexport ADMIN_USER='owner home'\nADMIN_PASS=\"s3cret-pass\"\n",
        )
        code, output = run(styled, deleted, "--salt", SALT_HEX)
        check("S2 a quoted name with a space is still refused", code == 2, output)
        styled.write_text(
            "# credenciales\nexport ADMIN_USER='admin'\nADMIN_PASS=\"s3cret-pass\"\n",
            encoding="utf-8",
        )
        code, output = run(styled, deleted, "--salt", SALT_HEX)
        check("S2 quotes around the values are stripped", code == 0, output)
        check(
            "S2 the quoted pair produces the same digest as the bare one",
            read_hex(deleted.read_text(encoding="utf-8"), "HASH", 32) == REFERENCE,
        )

        # --check must report without touching the file.
        stale = Path(work) / "stale_seed.h"
        code, output = run(env, stale, "--salt", SALT_HEX, "--check")
        check("S2 --check reports a missing header", code == 1, output)
        check("S2 --check writes nothing", not stale.exists())

    print("\n%d checks, %d failures" % (checks, failures))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
