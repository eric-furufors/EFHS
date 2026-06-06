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
* **Kernel (`src/ebpf/`):** Hooks into `block_rq_issue` and `block_rq_complete`. It saves the start time of an I/O request in a BPF Hash Map using the `struct request *` pointer as the key, then calculates the duration on completion.
* **Userspace (`src/userspace/`):** Loads the BPF program, reads the timestamps via a BPF Ring Buffer, and prints the metrics.

EFHS
├── Makefile
├── README.md
├── src/
│   ├── ebpf/           # sensor.bpf.c (eBPF C code)
│   ├── userspace/      # main.c (Loader and dashboard)
│   └── shared/         # common.h (Shared structs)
├── include/
├── docs/
└── tests/              # Test scripts and virtual disk setup

## Current Status & Roadmap
* **Phase 1: The Skeleton (June 2026) - CURRENT**
  * [x] Set up standalone libbpf-bootstrap project.
  * [x] Hook into `block_rq_issue` and verify events show up.
  * [ ] Move data tracking from `trace_pipe` to a BPF Ring Buffer.
  * [ ] Calculate basic latency delta (completion time - issue time).
* **Phase 2: The Engine & Virtual Lab (July 2026)**
  * [ ] Add log-scale histograms and percentile math.
  * [ ] Add Linux MD RAID mapping logic.
  * [ ] Set up `dm-delay` to simulate a stuttering drive.
* **Phase 3: Dashboard & Cleanup (August 2026)**
  * [ ] Write a simple ncurses TUI dashboard.
  * [ ] Benchmark overhead to ensure it stays under 1%.

## Installation & Dependencies
Requires `clang`, `llvm`, `libelf-dev`, and `bpftool`.

To compile the eBPF bytecode and userspace app use `make`

## Usage & Simulation
Run the userspace loader:
```bash
sudo ./efhs-app
```

To see raw debug logs out of the kernel sensor during development:
```bash
sudo cat /sys/kernel/debug/tracing/trace_pipe
```

## Development Log & Findings
* **June 2026:** Got the build system working. Verified that the `block_rq_issue` hook is successfully catching background disk I/O from system daemons like `kworker` and `jbd2`.

## eBPF Reference List
* [ebpf.io](https://ebpf.io/what-is-ebpf/) (General architecture)
* [Kernel.org BPF Maps](https://www.kernel.org/doc/html/latest/bpf/maps.html) (Hash map details)
* [Man7 bpf-helpers](https://man7.org/linux/man-pages/man7/bpf-helpers.7.html) (Helper function signatures)
* [eBPF Docs Reference](https://docs.ebpf.io/linux/helper-function/) (Interactive helper guide)
