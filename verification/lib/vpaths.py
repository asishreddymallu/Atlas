"""Where things are, for every verification script.

ROOT    the repository checkout (two levels above this file)
VERIFY  verification/
LIB     verification/lib (shared oracles: common, fpo, p6_oracle)
OUT     generated corpora, probe output and reports; $ATLAS_VERIFY_OUT or
        verification/out (git-ignored, recreated by the scripts)
BUILD   the build tree with the probes; $ATLAS_BUILD_VERIFY or build-verify
HARNESS lib/run_hermetic.sh, which every probe run goes through
"""
import os

LIB = os.path.dirname(os.path.abspath(__file__))
VERIFY = os.path.dirname(LIB)
ROOT = os.path.dirname(VERIFY)
OUT = os.path.abspath(os.environ.get("ATLAS_VERIFY_OUT", os.path.join(VERIFY, "out")))
BUILD = os.path.abspath(os.environ.get("ATLAS_BUILD_VERIFY", os.path.join(ROOT, "build-verify")))
HARNESS = os.path.join(LIB, "run_hermetic.sh")


def probe(name):
    """Path of a probe executable built with -DATLAS_BUILD_VERIFICATION=ON."""
    return os.path.join(BUILD, "verification", "probes", name)


def out(*parts):
    """A path under OUT; its directory is created."""
    p = os.path.join(OUT, *parts)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p
