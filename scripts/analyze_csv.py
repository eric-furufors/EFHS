#!/usr/bin/env python3
import csv
import sys
import os
import argparse


def percentile(values, p):
    # values must be sorted, linear interpolation between neighbours
    if not values:
        return 0.0
    k = (len(values) - 1) * p
    lo = int(k)
    hi = min(lo + 1, len(values) - 1)
    return values[lo] + (values[hi] - values[lo]) * (k - lo)


def analyze(csv_path, dev_filter, op_filter):
    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} not found. Run efhs-app first!")
        sys.exit(1)

    groups = {}      # (dev, op) -> list of latencies
    procs = {}       # (dev, op) -> {comm: count}

    with open(csv_path, "r") as f:
        reader = csv.DictReader(f)
        if "dev" not in (reader.fieldnames or []):
            print("This CSV has the old format (no dev column). Delete it and record a new one.")
            sys.exit(1)
        for row in reader:
            dev = row["dev"]
            op = row["op"]
            if dev_filter and dev != dev_filter:
                continue
            if op_filter and op != op_filter:
                continue
            key = (dev, op)
            groups.setdefault(key, []).append(float(row["latency_ms"]))
            p = procs.setdefault(key, {})
            p[row["comm"]] = p.get(row["comm"], 0) + 1

    if not groups:
        print("No events matched.")
        return

    print("=== EFHS Latency Analysis (ms) ===")
    print(f"{'DEV':<8} {'OP':<3} {'N':>7} {'MIN':>9} {'P50':>9} {'P95':>9} {'P99':>9} {'MAX':>9} {'MEAN':>9}")
    for key in sorted(groups, key=lambda k: -len(groups[k])):
        lat = sorted(groups[key])
        print(f"{key[0]:<8} {key[1]:<3} {len(lat):>7} {lat[0]:>9.2f} "
              f"{percentile(lat, 0.50):>9.2f} {percentile(lat, 0.95):>9.2f} "
              f"{percentile(lat, 0.99):>9.2f} {lat[-1]:>9.2f} {sum(lat)/len(lat):>9.2f}")

    print("\n--- Top processes per device/op ---")
    for key in sorted(groups, key=lambda k: -len(groups[k])):
        top = sorted(procs[key].items(), key=lambda x: -x[1])[:5]
        text = ", ".join(f"{c} ({n})" for c, n in top)
        print(f"  {key[0]} {key[1]}: {text}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", nargs="?", default="events.csv")
    parser.add_argument("--dev", help="only this device, e.g. 251:0")
    parser.add_argument("--op", choices=["R", "W", "O"], help="only reads or writes")
    args = parser.parse_args()
    analyze(args.csv, args.dev, args.op)
