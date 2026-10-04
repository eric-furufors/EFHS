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
EFHS is a small eBPF tool to measure block I/O latency and spot drive performance spikes. It uses libbpf and CO-RE to hook into the kernel block layer.

* **Goals:** Track I/O latency (submit to completion) per device and per read/write, report p50/p95/p99, and test it against simulated slow drives (`dm-delay`, later `null_blk`) using `fio`.
* **Non-Goals:** Hardware RAID, a fancy GUI, and perfect process attribution.
* **Thesis question:** How does the choice of measurement point affect the accuracy and overhead of eBPF-based block I/O latency monitoring in layered storage stacks?

## Architecture
The tool has two main parts:
* **Kernel (`src/ebpf/`):** Hooks `fentry/submit_bio` and `fentry/bio_endio`. Measuring at `submit_bio` captures the full latency through layers like LVM and Device Mapper, before the request reaches the driver queues. At submit the start time, PID, process name, size, sector, device and read/write are stored in a BPF LRU hash map. On completion the latency is added to a log2 histogram (all I/O), and if it is above the threshold the event is pushed to a BPF ring buffer.
* **Userspace (`src/userspace/`):** Loads the BPF program, reads the ring buffer, prints events (bold red at 50 ms or more) and writes them to `events.csv`. Every 10 seconds it reads the histogram, prints p50/p95/p99 for reads and writes and appends the buckets to `hist.csv`.

```text
EFHS
├── Makefile
├── README.md
├── src/
│   ├── ebpf/           # sensor.bpf.c (eBPF C code)
│   ├── userspace/      # main.c (loader and output)
│   └── shared/         # common.h (shared structs), vmlinux.h
├── scripts/            # lab setup/teardown and CSV analysis
└── tools/              # cmake files and vmlinux.h generator
```

## Current Status & Roadmap
* **Phase 1: The Skeleton - COMPLETED**
  * [x] Set up standalone libbpf-bootstrap project.
  * [x] Hook into `block_rq_issue` and verify events show up.
  * [x] Move data tracking from `trace_pipe` to a BPF Ring Buffer.
  * [x] Calculate basic latency delta (completion time - issue time).
* **Phase 2: The Latency Engine & Virtual Lab - COMPLETED**
  * [x] Latency threshold flagging in the kernel (default 20 ms, changeable).
  * [x] `dm-delay` lab script to simulate slow drives.
  * [x] Visual latency alerts (bold red at 50 ms or more).
  * [x] CSV logging and analysis script (per device and read/write, exact percentiles).
  * [x] In-kernel log2 histogram of all I/O with p50/p95/p99 output.
  * [x] Device, read/write and start sector in every event.
  * [x] Runs on kernels before and after 5.12 (CO-RE fallback for the device field).
* **Phase 3: Empirical Benchmarking & Comparative Study - CURRENT**
  * [ ] Automated `fio` workload runs as ground truth (accuracy of EFHS vs fio, vs a `block_rq_*` variant, vs biolatency).
  * [ ] Overhead of the three modes: export everything, threshold filter, histogram only (CPU, throughput, latency, on `null_blk`).
  * [ ] Histogram per device.
  * [ ] Optional: fault injection comparison against iostat (constant stall, intermittent spikes).

### Out of Scope
* MD RAID topology mapping
* TUI/dashboard interface

## Installation & Dependencies
Requires `clang`, `llvm`, `libelf-dev`, `libbpf-dev`, `zlib1g-dev` and `bpftool`.

The kernel needs BTF (`/sys/kernel/btf/vmlinux`). Tested on kernel 5.15 (WSL2) and on a Linux server running a kernel older than 5.12, both x86_64. The device is read from `bio->bi_bdev` on newer kernels and from `bi_disk`/`bi_partno` on older ones (chosen at load time with CO-RE). The ring buffer needs kernel 5.8 or newer, older kernels are untested.

To compile the eBPF bytecode and the userspace app use `make`

The virtual lab needs the `dm-delay` device mapper target. The stock WSL2 kernel does not have it, so use a normal Linux VM or a custom WSL kernel with `CONFIG_DM_DELAY` enabled.

## Usage & Simulation
### 1. Run the loader
```bash
sudo ./efhs-app        # report I/O slower than 20 ms
sudo ./efhs-app 5      # 5 ms threshold
sudo ./efhs-app 0      # export every I/O (use > /dev/null to keep the terminal clean)
```
It writes `events.csv` (one row per exported event) and `hist.csv` (histogram buckets every 10 s) in the current directory. Both are appended to, so use a new folder for each experiment.

### 2. Virtual Latency Lab (Simulating Slow Drives)
`scripts/setup_lab.sh` creates a virtual 100MB disk mapped through Device Mapper (`dm-delay`) with an injected 500ms delay:

**1. Create the delayed virtual disk (/dev/mapper/bad-disk)**
```bash
sudo ./scripts/setup_lab.sh
```

**2. Start EFHS in Terminal 1**
```bash
sudo ./efhs-app
```

**3. Trigger reads on the bad disk in Terminal 2**
```bash
for i in $(seq 0 19); do
  sudo dd if=/dev/mapper/bad-disk of=/dev/null bs=4K count=1 skip=$i iflag=direct status=none
done
```

**4. Tear down the virtual disk when finished**
```bash
sudo ./scripts/teardown_lab.sh
```

### 3. Analyze the CSV
```bash
python3 scripts/analyze_csv.py                      # everything, grouped by device and read/write
python3 scripts/analyze_csv.py --dev 251:0 --op R   # only reads on one device
```

## Known Limitations
* The percentiles in the live output come from log2 buckets, so they are rough (up to a factor of 2 in bucket width). Use `efhs-app 0` and the CSV for exact numbers.
* The histogram currently mixes all devices together.
* The process name is whoever called `submit_bio`. For buffered writes that is usually `kworker` or `jbd2`, not the real application. On software RAID it is the md kernel thread (for example `md0_raid5`).
* Only bios that go through `submit_bio` are tracked. Clones and splits made by Device Mapper are not.
* Results from WSL2 should not be used for the thesis measurements, since its virtual disk makes latency numbers hard to trust.
* Only built and tested for x86 (the Makefile sets `-D__TARGET_ARCH_x86`).

## Development Log & Findings
* **June 2026:** Got the build system working. Verified that the `block_rq_issue` hook is successfully catching background disk I/O from system daemons like `kworker` and `jbd2`.
* **July 2026:** Streamlined the build system to link against global system dependencies (`-lbpf` and system `bpftool`) rather than compiling them locally. Kernel code made to push live structures into a BPF Ring Buffer, eliminating the need to read `trace_pipe`.
* **August 2026:** Replaced lower-level `block_rq_*` tracepoints with `fentry/submit_bio` and `fentry/bio_endio`. Lower-level tracepoints fired after Device Mapper delays slept, masking latency. `submit_bio` captures the full top-to-bottom I/O time, flagging synthetic `dm-delay` stalls (~500ms) and real physical disk bottlenecks. Added userspace ms conversion, ANSI color thresholds, CSV logging and a first analysis script.
* **October 2026:** Fixed the sector count (size is now read at submit), added kernel timestamps, device, read/write and start sector to events, switched to an LRU map, and added an in-kernel log2 histogram with p50/p95/p99 output. The threshold is now an argument. Rewrote `analyze_csv.py` to group by device and read/write with exact percentiles. WSL2's stock kernel has no `dm-delay`, so the lab needed a custom kernel. First check on the lab: 20 direct 4K reads on a 500 ms `dm-delay` disk were reported at 505-510 ms by EFHS (median about 505.9 ms). Exported-all mode (`efhs-app 0`) showed that most events by count come from `kworker` and `jbd2`, not the application.
* **October 2026:** Ran the same binary on a second machine (a Linux server with older kernel) and the BPF program failed to load. The verifier log ended in `invalid func unknown#195896080`, which is the marker libbpf leaves when a CO-RE relocation cannot be resolved. Cause: `bio->bi_bdev` was added in kernel 5.12, and older kernels have `bi_disk` and `bi_partno` instead. CO-RE handles moved fields but not missing ones. Fixed with a second struct definition for the old layout (`struct bio___old`) and a `bpf_core_field_exists()` check, so one binary now runs on both kernels. On the server (which has a software RAID5 array) EFHS reports slow I/O attributed to `mdadm` and the `md0_raid5` kernel thread on the member disks.

## Lessons Learned (things that went wrong)
* **Request-level tracepoints hide stacked-device delays.** `block_rq_*` events fire when a request reaches the underlying device, after `dm-delay` has already held the bio. Hooking `submit_bio` instead sees the full delay.
* **CO-RE does not fix missing fields.** `bi_bdev` does not exist before kernel 5.12. A "flavor" struct plus `bpf_core_field_exists()` solved it. The sign of an unresolved relocation in the verifier log is `unknown#195896080`.
* **BPF globals need plain C types.** Using the kernel type `u64` in a global variable broke the generated skeleton header in userspace, so those use `unsigned long long`.
* **WSL2's stock kernel has no `dm-delay`.** The virtual lab needs a different kernel (custom WSL kernel with `CONFIG_DM_DELAY`, or a normal Linux VM).
* **`comm` is not the application.** For buffered writes the process at `submit_bio` is usually `kworker` or `jbd2`, so per-process attribution is limited.

## eBPF Reference List
* [ebpf.io](https://ebpf.io/what-is-ebpf/) (General architecture)
* [Kernel.org BPF Maps](https://www.kernel.org/doc/html/latest/bpf/maps.html) (Hash map details)
* [Man7 bpf-helpers](https://man7.org/linux/man-pages/man7/bpf-helpers.7.html) (Helper function signatures)
* [eBPF Docs Reference](https://docs.ebpf.io/linux/helper-function/) (Interactive helper guide)
