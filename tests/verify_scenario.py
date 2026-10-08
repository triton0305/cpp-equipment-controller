"""Compare a scenario's observed values with the verified pre-refactor run."""
import re
import subprocess
import sys

number = int(sys.argv[1])
result = subprocess.run([sys.argv[2]], capture_output=True, text=True, timeout=15)
if result.returncode:
    print(result.stdout, result.stderr)
    sys.exit(result.returncode)

with open(sys.argv[3], encoding="utf-8") as baseline_file:
    baseline = baseline_file.read()


def scenario(text, n):
    marker = re.search(rf"^\[TEST {n}\] .*?$", text, re.M)
    if marker is None:
        raise ValueError(f"TEST {n} missing")
    end = re.search(r"^\[TEST \d+\] ", text[marker.end():], re.M)
    return text[marker.end():marker.end() + end.start() if end else None].strip()


def normalized(text):
    return re.sub(r"\[\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\]", "[TIMESTAMP]", text)

expected = normalized(scenario(baseline, number))
actual = normalized(scenario(result.stdout, number))
if actual != expected:
    print(f"TEST {number} mismatch\nExpected:\n{expected}\nActual:\n{actual}")
    sys.exit(1)
print(f"TEST {number} passed: observed values match baseline")
