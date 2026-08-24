#!/usr/bin/env python3
import csv
import sys
import os

def analyze(csv_path="events.csv"):
    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} not found. Run efhs-app first!")
        sys.exit(1)

    latencies = []
    events_by_comm = {}

    with open(csv_path, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            lat = float(row["latency_ms"])
            comm = row["comm"]
            latencies.append(lat)
            events_by_comm[comm] = events_by_comm.get(comm, 0) + 1

    if not latencies:
        print("No slow I/O events recorded yet.")
        return

    print("=== EFHS Latency Analysis ===")
    print(f"Total Slow Events Tracked : {len(latencies)}")
    print(f"Minimum Latency           : {min(latencies):.2f} ms")
    print(f"Maximum Latency           : {max(latencies):.2f} ms")
    print(f"Average Latency           : {sum(latencies)/len(latencies):.2f} ms")
    print("\n--- Events Per Process ---")
    for comm, count in events_by_comm.items():
        print(f"  {comm:<16} : {count} event(s)")

if __name__ == "__main__":
    filepath = sys.argv[1] if len(sys.argv) > 1 else "events.csv"
    analyze(filepath)#!/usr/bin/env python3
import csv
import sys
import os

def analyze(csv_path="events.csv"):
    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} not found. Run efhs-app first!")
        sys.exit(1)

    latencies = []
    events_by_comm = {}

    with open(csv_path, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            lat = float(row["latency_ms"])
            comm = row["comm"]
            latencies.append(lat)
            events_by_comm[comm] = events_by_comm.get(comm, 0) + 1

    if not latencies:
        print("No slow I/O events recorded yet.")
        return

    print("=== EFHS Latency Analysis ===")
    print(f"Total Slow Events Tracked : {len(latencies)}")
    print(f"Minimum Latency           : {min(latencies):.2f} ms")
    print(f"Maximum Latency           : {max(latencies):.2f} ms")
    print(f"Average Latency           : {sum(latencies)/len(latencies):.2f} ms")
    print("\n--- Events Per Process ---")
    for comm, count in events_by_comm.items():
        print(f"  {comm:<16} : {count} event(s)")

if __name__ == "__main__":
    filepath = sys.argv[1] if len(sys.argv) > 1 else "events.csv"
    analyze(filepath)
