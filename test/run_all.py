import importlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "test"))

CHECKS = ("partitions", "versions", "firmware", "includes")


def main():
    bad = 0
    for name in CHECKS:
        mod = importlib.import_module("checks." + name)
        errs = mod.run(ROOT)
        if errs:
            bad += 1
            print("[FAIL] " + name)
            for e in errs:
                print("       " + e)
        else:
            print("[ OK ] " + name)
    print()
    print("%d/%d passed" % (len(CHECKS) - bad, len(CHECKS)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
