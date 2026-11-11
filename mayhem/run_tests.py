#!/usr/bin/env python3
"""Run upstream's pytest suite against the normally-built extension and print one summary line."""
import os
import sys

import pytest

SRC = os.environ.get("SRC", "/mayhem")
os.chdir(SRC)
sys.path.insert(0, os.path.join(SRC, "build", "test-lib"))
sys.dont_write_bytecode = True


class Counter:
    def pytest_terminal_summary(self, terminalreporter):
        st = terminalreporter.stats
        n = lambda k: len([r for r in st.get(k, []) if getattr(r, "when", "call") in ("call", "setup")])
        passed, failed = n("passed"), n("failed") + len(st.get("error", []))
        skipped = n("skipped") + n("xfailed")
        print(f"RUNTESTS tests={passed + failed + skipped} passed={passed} failed={failed} skipped={skipped}")


rc = pytest.main(["-p", "no:cacheprovider", "-W", "ignore::pytest.PytestUnknownMarkWarning", "tests"],
                 plugins=[Counter()])
sys.exit(int(rc))
