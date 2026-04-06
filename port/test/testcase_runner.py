"""
testcase_runner.py — Discover and run MGS port test cases.

Usage:
    python testcase_runner.py                          # run all test_*.py in this directory
    python testcase_runner.py test_foo.py              # run specific file
    python testcase_runner.py test_*.py                # glob pattern
    python testcase_runner.py -k socom                 # filter: only tests matching 'socom'
    python testcase_runner.py testcases/ -k d01a       # directory + filter
    python testcase_runner.py --no-headless testcases/ # show game window during tests
    python testcase_runner.py --timeout 60 testcases/ # 60s hard timeout per test (default: 120)

Each test file should contain functions named test_*() with no arguments.
Tests pass by returning normally. Tests fail by raising any exception.
"""

import glob
import importlib.util
import inspect
import os
import signal
import subprocess
import sys
import time
import traceback


TEST_TIMEOUT = 120  # seconds per test function — hard kill after this


def _kill_stale_games():
    """Kill any leftover mgs processes from previous test runs."""
    try:
        subprocess.run(["pkill", "-9", "mgs"], capture_output=True, timeout=3)
        time.sleep(0.3)
    except Exception:
        pass


class _TestTimedOut(Exception):
    pass


def _alarm_handler(signum, frame):
    raise _TestTimedOut(f"Test exceeded {TEST_TIMEOUT}s hard timeout")


def discover_tests(patterns):
    """Find all test_*.py files matching patterns or directories."""
    files = []
    for pattern in patterns:
        if os.path.isfile(pattern):
            files.append(pattern)
        elif os.path.isdir(pattern):
            files.extend(sorted(glob.glob(os.path.join(pattern, "test_*.py"))))
        else:
            files.extend(sorted(glob.glob(pattern)))
    return files


def load_module(path):
    """Load a Python file as a module."""
    name = os.path.splitext(os.path.basename(path))[0]
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_file(path, name_filter=None):
    """Run all test_* functions in a file. Returns (passed, failed) counts."""
    passed = []
    failed = []

    try:
        module = load_module(path)
    except Exception as e:
        print(f"\n  ERROR loading {path}: {e}")
        traceback.print_exc()
        return [], [("(load)", str(e))]

    test_fns = [
        (name, fn)
        for name, fn in inspect.getmembers(module, inspect.isfunction)
        if name.startswith("test_")
    ]

    if name_filter:
        test_fns = [(n, f) for n, f in test_fns if name_filter in n]

    if not test_fns:
        return [], []

    for name, fn in test_fns:
        # Kill stale game processes before each test for isolation
        _kill_stale_games()

        doc = (fn.__doc__ or "").strip().splitlines()[0] if fn.__doc__ else ""
        label = f"{os.path.basename(path)}::{name}"
        print(f"  RUN  {label}", end="", flush=True)
        t0 = time.monotonic()

        # Set a hard timeout — SIGALRM kills the test if it hangs.
        prev_handler = signal.signal(signal.SIGALRM, _alarm_handler)
        signal.alarm(TEST_TIMEOUT)
        try:
            fn()
            signal.alarm(0)
            elapsed = time.monotonic() - t0
            print(f" ... PASS ({elapsed:.1f}s)")
            if doc:
                print(f"       {doc}")
            passed.append(name)
        except _TestTimedOut as e:
            signal.alarm(0)
            elapsed = time.monotonic() - t0
            print(f" ... TIMEOUT ({elapsed:.1f}s)")
            print(f"       {e}")
            failed.append((name, str(e)))
            # Force-kill the game — it's stuck
            _kill_stale_games()
        except Exception as e:
            signal.alarm(0)
            elapsed = time.monotonic() - t0
            print(f" ... FAIL ({elapsed:.1f}s)")
            print(f"       {e}")
            traceback.print_exc(file=sys.stdout)
            failed.append((name, str(e)))
        finally:
            signal.signal(signal.SIGALRM, prev_handler)

    return passed, failed


def main():
    # Add this directory to sys.path so tests can import mgs_client
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.insert(0, here)

    # Parse CLI flags
    args = sys.argv[1:]
    name_filter = None
    headless = True

    if "--no-headless" in args:
        headless = False
        args.remove("--no-headless")

    global TEST_TIMEOUT
    if "--timeout" in args:
        idx = args.index("--timeout")
        if idx + 1 < len(args):
            TEST_TIMEOUT = int(args[idx + 1])
            args = args[:idx] + args[idx + 2:]
        else:
            print("Error: --timeout requires a value in seconds")
            sys.exit(2)

    if "-k" in args:
        idx = args.index("-k")
        if idx + 1 < len(args):
            name_filter = args[idx + 1]
            args = args[:idx] + args[idx + 2:]
        else:
            print("Error: -k requires a pattern argument")
            sys.exit(2)

    # Propagate headless setting so MGSTestClient picks it up.
    # Tests import mgs_client and use its default; we override that default.
    import mgs_client
    mgs_client._default_headless = headless

    patterns = args or [os.path.join(here, "test_*.py")]
    files = discover_tests(patterns)

    if not files:
        print("No test files found.")
        sys.exit(0)

    total_passed = []
    total_failed = []

    print(f"\n{'='*60}")
    print(f"MGS Port Test Runner")
    if name_filter:
        print(f"Filter: -k {name_filter}")
    if not headless:
        print(f"Mode: --no-headless (game window visible)")
    print(f"{'='*60}")

    for path in files:
        print(f"\n{os.path.basename(path)}")
        p, f = run_file(path, name_filter=name_filter)
        total_passed.extend(p)
        total_failed.extend(f)

    # Cleanup any remaining game processes
    _kill_stale_games()

    print(f"\n{'='*60}")
    print(f"Results: {len(total_passed)} passed, {len(total_failed)} failed")
    if total_failed:
        print("\nFailed tests:")
        for name, err in total_failed:
            print(f"  FAIL  {name}: {err}")
        sys.exit(1)
    else:
        print("All tests passed.")
        sys.exit(0)


if __name__ == "__main__":
    main()
