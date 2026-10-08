#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber

"""What the timing log says: how long each build and test stage takes.

    scripts/build-timings.py [--log FILE] [--since DATE] [--stage TEXT]
                             [--script TEXT] [--slowest N] [--json]

Reads the JSON lines scripts/timing.sh appends (one per stage; the log is
~/Library/Logs/neon-ingvy/build-timings.jsonl on macOS,
$XDG_STATE_HOME/neon-ingvy/build-timings.jsonl elsewhere, NI_TIMING_LOG or
--log another) and prints:

    per stage    count, median, p90 and the last run's duration, by script
                 and stage, with how many of them failed
    per day      stages, total time and failures, day by day
    slowest      the N slowest single stages, with the commit and checkout
    ccache       hits and misses over every stage that counted them, and the
                 hit rate

--since keeps records from that day (YYYY-MM-DD) or that instant (ISO 8601)
on; --stage and --script keep records whose stage or script contains the
text. --json prints the same summary as one JSON document instead.

Standard library only, so it runs wherever the scripts do. A line that is not
a record (a torn write, an edit by hand) is counted and skipped, never fatal.
"""

import argparse
import json
import os
import platform
import statistics
import sys
from collections import OrderedDict, defaultdict


def default_log():
    if os.environ.get("NI_TIMING_LOG"):
        return os.environ["NI_TIMING_LOG"]
    home = os.path.expanduser("~")
    if platform.system() == "Darwin":
        return os.path.join(home, "Library", "Logs", "neon-ingvy", "build-timings.jsonl")
    state = os.environ.get("XDG_STATE_HOME") or os.path.join(home, ".local", "state")
    return os.path.join(state, "neon-ingvy", "build-timings.jsonl")


def read_records(path):
    """The records in the log, in order, and how many lines were not one."""
    records, skipped = [], 0
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                r = json.loads(line)
            except ValueError:
                skipped += 1
                continue
            if not isinstance(r, dict) or not isinstance(r.get("duration_s"), (int, float)) \
                    or not r.get("stage") or not r.get("time"):
                skipped += 1
                continue
            records.append(r)
    return records, skipped


def keep(r, args):
    if args.since and r["time"] < args.since:
        return False
    if args.stage and args.stage not in r["stage"]:
        return False
    if args.script and args.script not in (r.get("script") or ""):
        return False
    return True


def percentile(values, p):
    """The p-th percentile by the nearest-rank method: a value that occurred."""
    ordered = sorted(values)
    rank = max(1, -(-len(ordered) * p // 100))
    return ordered[int(rank) - 1]


def summarise(records, slowest):
    stages = OrderedDict()
    for r in sorted(records, key=lambda r: r["time"]):
        key = (r.get("script") or "?", r["stage"])
        stages.setdefault(key, []).append(r)
    per_stage = []
    for (script, stage), rs in stages.items():
        d = [r["duration_s"] for r in rs]
        per_stage.append({
            "script": script, "stage": stage, "count": len(rs),
            "median_s": round(statistics.median(d), 3),
            "p90_s": round(percentile(d, 90), 3),
            "last_s": round(d[-1], 3),
            "failed": sum(1 for r in rs if r.get("exit_code", 0) != 0),
        })

    days = defaultdict(lambda: {"stages": 0, "total_s": 0.0, "failed": 0})
    for r in records:
        day = days[r["time"][:10]]
        day["stages"] += 1
        day["total_s"] += r["duration_s"]
        day["failed"] += 1 if r.get("exit_code", 0) != 0 else 0
    per_day = [{"day": k, "stages": v["stages"], "total_s": round(v["total_s"], 3), "failed": v["failed"]}
               for k, v in sorted(days.items())]

    worst = sorted(records, key=lambda r: r["duration_s"], reverse=True)[:slowest]
    slow = [{"time": r["time"], "script": r.get("script"), "stage": r["stage"],
             "duration_s": r["duration_s"], "exit_code": r.get("exit_code"),
             "sha": r.get("sha"), "worktree": r.get("worktree")} for r in worst]

    counted = [r for r in records
               if isinstance(r.get("ccache_hits"), int) and isinstance(r.get("ccache_misses"), int)]
    hits = sum(r["ccache_hits"] for r in counted)
    misses = sum(r["ccache_misses"] for r in counted)
    ccache = {"stages": len(counted), "hits": hits, "misses": misses,
              "hit_rate": round(hits / (hits + misses), 4) if hits + misses else None}

    return {"records": len(records), "per_stage": per_stage, "per_day": per_day,
            "slowest": slow, "ccache": ccache}


def seconds(s):
    if s >= 3600:
        return "%dh%02dm" % (s // 3600, s % 3600 // 60)
    if s >= 60:
        return "%dm%02ds" % (s // 60, s % 60)
    return "%.1fs" % s


def table(rows, header):
    widths = [max(len(str(x)) for x in col) for col in zip(header, *rows)]
    line = lambda cells: "  ".join(str(c).ljust(w) for c, w in zip(cells, widths)).rstrip()
    return "\n".join([line(header), line("-" * w for w in widths)] + [line(row) for row in rows])


def print_text(summary, log, skipped):
    print("%s: %d records%s" % (log, summary["records"],
                                ", %d unreadable lines skipped" % skipped if skipped else ""))
    print()
    print("per stage")
    print(table([[s["script"], s["stage"], s["count"], seconds(s["median_s"]), seconds(s["p90_s"]),
                  seconds(s["last_s"]), s["failed"]] for s in summary["per_stage"]],
                ["script", "stage", "runs", "median", "p90", "last", "failed"]))
    print()
    print("per day")
    print(table([[d["day"], d["stages"], seconds(d["total_s"]), d["failed"]] for d in summary["per_day"]],
                ["day", "stages", "total", "failed"]))
    print()
    print("slowest")
    print(table([[s["time"], s["script"], s["stage"], seconds(s["duration_s"]), s["exit_code"],
                  s["sha"] or "", s["worktree"] or ""] for s in summary["slowest"]],
                ["time", "script", "stage", "took", "exit", "sha", "worktree"]))
    print()
    c = summary["ccache"]
    rate = "n/a" if c["hit_rate"] is None else "%.1f%%" % (100 * c["hit_rate"])
    print("ccache: %d hits, %d misses over %d stages, hit rate %s" % (c["hits"], c["misses"], c["stages"], rate))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--log", default=default_log(), help="the timing log (default: %(default)s)")
    ap.add_argument("--since", help="keep records from this day (YYYY-MM-DD) or instant (ISO 8601) on")
    ap.add_argument("--stage", help="keep records whose stage contains this text")
    ap.add_argument("--script", help="keep records whose script contains this text")
    ap.add_argument("--slowest", type=int, default=10, help="how many of the slowest stages to list")
    ap.add_argument("--json", action="store_true", help="print the summary as JSON")
    args = ap.parse_args(argv)

    if not os.path.exists(args.log):
        print("no timing log at %s -- a build or test script writes it" % args.log, file=sys.stderr)
        return 1
    records, skipped = read_records(args.log)
    records = [r for r in records if keep(r, args)]
    summary = summarise(records, args.slowest)
    if args.json:
        summary["log"] = args.log
        summary["skipped"] = skipped
        json.dump(summary, sys.stdout, indent=2)
        print()
    else:
        print_text(summary, args.log, skipped)
    return 0


if __name__ == "__main__":
    sys.exit(main())
