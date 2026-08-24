# EFHS
```text
  ______ ______ _    _  _____ 
 |  ____|  ____| |  | |/ ____|
 | |__  | |__  | |__| | (___  
 |  __| |  __| |  __  |\___ \ 
 | |____| |    | |  | |____) |
 |______|_|    |_|  |_|_____/ 
                               
  eBPF Filesystem Health Sensor
```

## Overview
EFHS is a small eBPF tool to measure block I/O latency and spot drive performance spikes. It uses libbpf and CO-RE to hook into the kernel block layer safely.

* **MVP Goals:** Track I/O latency (issue to completion), show p50/p95/p99 percentiles, and test it against simulated slow drives (`null_blk`/`dm-delay`) using `fio`.
* **Non-Goals:** I am not targeting hardware RAID, complex GUIs, or process tracking for the initial version.

## Architecture
The tool has two main parts:
* **Kernel (`src/ebpf/`):** Hooks into `fentry/submit_bio` and `fentry/bio_endio`. Measuring at `submit_bio` captures top-of-stack block latency across virtual layers (LVM, Device Mapper, Docker `overlay2`) before requests enter driver queues. The start time, PID, and process name (`comm`) are tracked in a BPF Hash Map, evaluated on completion, and pushed to a BPF Ring Buffer if latency exceeds the `SLOW_IO_NS` threshold (20ms).
* **Userspace (`src/userspace/`):** Loads the BPF program, polls the ring buffer, converts nanosecond latencies to human-readable milliseconds, and applies ANSI color formatting for visual alerts (≥50ms highlighted in bold red).

```text
EFHS
├── Makefile
├── README.md
├── src/
│   ├── ebpf/           # sensor.bpf.c (eBPF C code)
│   ├── userspace/      # main.c (Loader and dashboard)
│   └── shared/         # common.h (Shared structs)
├── include/
├── docs/
├── scripts/
└── tests/              # Test scripts and virtual disk setup
```

## Current Status & Roadmap
* **Phase 1: The Skeleton - COMPLETED**
  * [x] Set up standalone libbpf-bootstrap project.
  * [x] Hook into `block_rq_issue` and verify events show up.
  * [x] Move data tracking from `trace_pipe` to a BPF Ring Buffer.
  * [x] Calculate basic latency delta (completion time - issue time).
### Phase 2: The Latency Engine & Virtual Lab (Current Phase)
- [x] Implement latency threshold flagging (kernel-level filtering for requests >20ms).
- [x] Build `dm-delay` lab script to simulate bad sectors and drive stalls.
- [x] Add visual latency alerts (bold red highlighting for latencies ≥50ms).
- [x] Add basic latency aggregation (tracking min/max/average and histogram buckets).
* **Phase 3: Empirical Benchmarking & Comparative Study (Current Phase)**
  * [ ] Build automated `fio` workload benchmarker to test under heavy I/O strain.
  * [ ] Measure CPU/Memory overhead of eBPF kernel filtering vs. traditional tools.
  * [ ] Field study: Manual troubleshooting (iostat/journalctl) vs. automated EFHS event attribution.

### Out of Scope
*The following features are for extended work:*
* **MD RAID Topology Mapping:** Correlating physical block layer delays to Linux MD software RAID arrays.
* **TUI/Dashboard Interface:** Custom `ncurses`-based terminal UI for live drive health monitoring.
* **Overhead Benchmarking:** Micro-benchmarking eBPF ring buffer strain under extreme I/O workloads.

## Installation & Dependencies
Requires `clang`, `llvm`, `libelf-dev`, and `bpftool`.

To compile the eBPF bytecode and userspace app use `make`

## Usage & Simulation
### 1. Run the userspace loader:
```bash
sudo ./efhs-app
```

### 2. Virtual Latency Lab (Simulating Slow Drives)
The repo includes shell scripts in scripts/ to create a virtual 100MB disk mapped through Device Mapper (dm-delay) with an injected 500ms delay:
**1. Create the delayed virtual disk (/dev/mapper/bad-disk)**
```bash
sudo ./scripts/setup_lab.sh
```

**2. Start EFHS in Terminal 1**
```bash
sudo ./efhs-app
```

**3. Trigger a read on the bad disk in Terminal 2**
```bash
sudo dd if=/dev/mapper/bad-disk of=/dev/null bs=4K count=1
```

**4. Tear down the virtual disk when finished**
```bash
sudo ./scripts/teardown_lab.sh
```

## Development Log & Findings
* **June 2026:** Got the build system working. Verified that the `block_rq_issue` hook is successfully catching background disk I/O from system daemons like `kworker` and `jbd2`.
* **July 2026:** Streamlined the build system to link against global system dependencies (`-lbpf` and system `bpftool`) rather than compiling them locally. Kernel code made to push live structures into a BPF Ring Buffer, eliminating the need to read `trace_pipe` and allowing the userspace C app to process and print structured I/O events in actively.
* **August 2026:** Replaced lower-level `block_rq_*` tracepoints with `fentry/submit_bio` and `fentry/bio_endio`. Lower-level tracepoints fired after Device Mapper delays slept, masking latency. `submit_bio` captures full top-to-bottom I/O execution time, accurately flagging synthetic `dm-delay` stalls (~500ms) and real physical disk bottlenecks. Added userspace ms conversion and ANSI color thresholds.
* **August 2026:** Completed Phase 2. Updated userspace app (`main.c`) to stream I/O events asynchronously into `events.csv`. Built `scripts/analyze_csv.py` to calculate summary metrics (min/max/average latency and process event counts). Validated that top-of-stack eBPF tracing captures synthetic `dm-delay` stalls (~500ms) alongside real background disk I/O. Shifted Phase 3 scope toward thesis benchmarking and field study experiments.

## eBPF Reference List
* [ebpf.io](https://ebpf.io/what-is-ebpf/) (General architecture)
* [Kernel.org BPF Maps](https://www.kernel.org/doc/html/latest/bpf/maps.html) (Hash map details)
* [Man7 bpf-helpers](https://man7.org/linux/man-pages/man7/bpf-helpers.7.html) (Helper function signatures)
* [eBPF Docs Reference](https://docs.ebpf.io/linux/helper-function/) (Interactive helper guide)
