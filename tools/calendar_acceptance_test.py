"""Host-side tests for tools/calendar_acceptance.py.

The acceptance script is the gate that decides whether the real Google
connection works, so its judgement is pinned here instead of being trusted after
one manual run. No network: `check_status` is a pure function of the JSON the
panel serves.

    python tools/calendar_acceptance_test.py
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import calendar_acceptance as acceptance

FAILURES = 0


def check(what, ok):
    global FAILURES
    if ok:
        print("ok   %-64s" % what)
    else:
        print("FAIL %-64s" % what)
        FAILURES += 1


def ready(**overrides):
    payload = {
        "kind": "ready",
        "configured": True,
        "authorized": True,
        "pending": False,
        "userCode": "",
        "verificationUrl": "",
        "selectedId": "primary",
        "selectedName": "maxi@example.com",
        "graceMin": 5,
        "lastError": "",
        "hasNext": True,
        "nextSummary": "Dentista",
        "nextInMin": 20,
        "nextStarted": False,
        "nextLeadMin": 30,
        "reaction": {"active": True, "phase": "early", "intensity": 33,
                     "msToStart": 1200000},
    }
    payload.update(overrides)
    return payload


def main():
    print("-- a healthy device passes")
    check("ready with a matching window", acceptance.check_status(ready()) == [])
    check("an unconfigured device is not a consistency failure",
          acceptance.check_status({"kind": "unconfigured"}) == [])

    print("-- the reaction must agree with the event's reminder")
    wrong = ready(reaction={"active": False, "phase": "none", "intensity": 0,
                            "msToStart": 1200000})
    problems = acceptance.check_status(wrong)
    check("inactive inside the window is a problem", len(problems) == 1)
    check("  and the sentence names the reminder",
          "no coincide con el recordatorio" in problems[0])

    outside = ready(nextInMin=60, reaction={"active": True, "phase": "early",
                                            "intensity": 10, "msToStart": 3600000})
    check("active outside the window is a problem",
          len(acceptance.check_status(outside)) == 1)

    ongoing = ready(nextInMin=-2, nextStarted=True, nextLeadMin=30,
                    reaction={"active": True, "phase": "ongoing",
                              "intensity": 100, "msToStart": -120000})
    check("an ongoing event is consistent", acceptance.check_status(ongoing) == [])

    print("-- the borders are ambiguous by one truncated minute, not failures")
    at_lead = ready(nextInMin=30,
                    reaction={"active": False, "phase": "none", "intensity": 0,
                              "msToStart": 0})
    check("exactly at the lead border is not a failure",
          acceptance.check_status(at_lead) == [])
    at_grace = ready(nextInMin=-5, nextStarted=True,
                     reaction={"active": False, "phase": "none", "intensity": 0,
                               "msToStart": 0})
    check("exactly at the grace border is not a failure",
          acceptance.check_status(at_grace) == [])

    print("-- the JSON-lifetime defect must be caught")
    garbage = ready(userCode="\xed_\x03\x82")
    check("non-ASCII garbage in userCode is a problem",
          any("userCode" in p for p in acceptance.check_status(garbage)))
    stale = ready(userCode="ABCD-EFGH")
    check("a user code without a pending flow is a problem",
          any("userCode no vacio" in p for p in acceptance.check_status(stale)))

    print("-- the fields that must exist")
    check("ready without a selected calendar is a problem",
          any("sin calendario" in p for p in
              acceptance.check_status(ready(selectedId=""))))
    check("an unknown phase is a problem",
          any("fase" in p for p in acceptance.check_status(
              ready(reaction={"active": True, "phase": "soon", "intensity": 50,
                              "msToStart": 60000}))))
    check("an intensity outside 0..100 is a problem",
          any("intensidad" in p for p in acceptance.check_status(
              ready(reaction={"active": True, "phase": "early", "intensity": 140,
                              "msToStart": 60000}))))
    check("ongoing with time left is a problem",
          any("ongoing" in p for p in acceptance.check_status(
              ready(nextInMin=-1, reaction={"active": True, "phase": "ongoing",
                                            "intensity": 100, "msToStart": 5000}))))

    print()
    print("PASSED" if FAILURES == 0 else "FAILED (%d)" % FAILURES)
    return 0 if FAILURES == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
