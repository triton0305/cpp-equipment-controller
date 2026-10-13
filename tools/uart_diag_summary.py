#!/usr/bin/env python3
"""Compare retained UART flight-recorder measurements; does not access UART."""
import argparse
import csv
from collections import defaultdict


def summarize(path):
    with open(path, encoding="utf-8") as source:
        metadata = source.readline().strip()
        rows = list(csv.DictReader(source))
    print(f"\n{path}\n{metadata}")
    sessions = defaultdict(list)
    for row in rows:
        sessions[int(row["session"])].append(row)
    for session, records in sorted(sessions.items()):
        print(f"session={session} retained_records={len(records)}")
        begin = next((r for r in records if r["event"] == "sync_begin"), None)
        recovered = next((r for r in records if r["event"] == "sync_complete"), None)
        if begin and recovered:
            duration = (int(recovered["monotonic_us"]) - int(begin["monotonic_us"])) / 1000
            print(f"  sync_duration_ms={duration:.3f}")
        for event in ("poll_end", "service_end", "read_events_end", "send_outputs_end", "write_end", "status_end"):
            values = [float(r["duration_ms"]) for r in records if r["event"] == event]
            if values:
                print(f"  max_{event}_ms={max(values):.3f}")
        print("  process_max_poll_gap_ms=" + records[-1]["max_poll_gap_ms"])
        reads = [r for r in records if r["event"] == "read_events_end"]
        print(f"  nonempty_reads={sum(int(r['bytes']) > 0 for r in reads)}/{len(reads)}")
        for index, row in enumerate(records):
            if row["event"] != "timeout_fault":
                continue
            print("  FAULT " + " ".join(f"{key}={row[key]}" for key in
                  ("monotonic_us", "phase", "poll", "last_sensor_age_ms", "sync_age_ms",
                   "recovery_age_ms", "rx_events", "rx_index", "poll_gap_ms")))
            if index + 1 < len(records) and records[index + 1]["event"] == "timeout_rx_queue":
                queued = records[index + 1]
                print(f"    kernel_queued_bytes={queued['bytes']} ioctl_ok={queued['result']}")
            print("    reason=" + bytes.fromhex(row["detail_hex"]).decode("ascii", errors="replace"))
        invalid = [r for r in records if r["event"] == "invalid_sensor"]
        for row in invalid[:5]:
            # repr prevents raw control bytes from altering the terminal display.
            print("  invalid_prefix=" + repr(bytes.fromhex(row["detail_hex"])))
    print("Durations/counts cover retained records only; inspect sequence gaps and dropped= metadata.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", nargs="+")
    args = parser.parse_args()
    for filename in args.csv:
        summarize(filename)
