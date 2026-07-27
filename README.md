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
└── tests/              # Test scripts and virtual disk setup
```

## Current Status & Roadmap
* **Phase 1: The Skeleton - COMPLETED**
  * [x] Set up standalone libbpf-bootstrap project.
  * [x] Hook into `block_rq_issue` and verify events show up.
  * [x] Move data tracking from `trace_pipe` to a BPF Ring Buffer.
  * [x] Calculate basic latency delta (completion time - issue time).
### Phase 2: The Latency Engine & Virtual Lab (Current Phase)
- [ ] Implement latency threshold flagging (e.g., mark requests exceeding 100ms as degraded).
- [ ] Build `dm-delay` lab script to simulate bad sectors and drive stalls.
- [ ] Add basic latency aggregation (tracking min/max/average and histogram buckets).
- [ ] Export structured metrics/events for external consumers (JSON/CLI streaming).
### Out of Scope
*The following features are for extended work:*
* **MD RAID Topology Mapping:** Correlating physical block layer delays to Linux MD software RAID arrays.
* **TUI/Dashboard Interface:** Custom `ncurses`-based terminal UI for live drive health monitoring.
* **Overhead Benchmarking:** Micro-benchmarking eBPF ring buffer strain under extreme I/O workloads.

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
* **July 2026:** Streamlined the build system to link against global system dependencies (`-lbpf` and system `bpftool`) rather than compiling them locally. Kernel code made to push live structures into a BPF Ring Buffer, eliminating the need to read `trace_pipe` and allowing the userspace C app to process and print structured I/O events in actively.

## eBPF Reference List
* [ebpf.io](https://ebpf.io/what-is-ebpf/) (General architecture)
* [Kernel.org BPF Maps](https://www.kernel.org/doc/html/latest/bpf/maps.html) (Hash map details)
* [Man7 bpf-helpers](https://man7.org/linux/man-pages/man7/bpf-helpers.7.html) (Helper function signatures)
* [eBPF Docs Reference](https://docs.ebpf.io/linux/helper-function/) (Interactive helper guide)
