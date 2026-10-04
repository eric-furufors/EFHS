#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "../shared/common.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

// Threshold, set from userspace (default 20ms)
const volatile unsigned long long slow_io_ns = 20000000ULL;

// Counts events we lost because the ring buffer was full
unsigned long long rb_drops = 0;

struct io_info {
    u64 start_ts;
    u64 sector;
    u32 pid;
    u32 sectors;
    u32 dev;
    u32 op;
    char comm[16];
};

struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 10240);
    __type(key, u64); // Pointer to bio struct
    __type(value, struct io_info);
} start_bios SEC(".maps");

// Histogram of all I/O, index = op * MAX_SLOTS + bucket
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 2 * MAX_SLOTS);
    __type(key, u32);
    __type(value, u64);
} hist SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24);
} rb SEC(".maps");

static __always_inline u32 log2_slot(u64 v)
{
    u32 r = 0;
    for (int i = 0; i < MAX_SLOTS - 1; i++) {
        if (v <= 1)
            break;
        v >>= 1;
        r++;
    }
    return r;
}

// 1. Captured when I/O enters the kernel block layer (BEFORE dm-delay)
SEC("fentry/submit_bio")
int BPF_PROG(submit_bio_entry, struct bio *bio)
{
    u64 bio_ptr = (u64)bio;
    struct io_info info = {};

    info.start_ts = bpf_ktime_get_ns();
    info.pid = bpf_get_current_pid_tgid() >> 32;
    info.sectors = bio->bi_iter.bi_size >> 9; // size has to be read here
    info.sector = bio->bi_iter.bi_sector;
    info.dev = bio->bi_bdev->bd_dev;
    info.op = bio->bi_opf & 0xff;
    bpf_get_current_comm(&info.comm, sizeof(info.comm));

    bpf_map_update_elem(&start_bios, &bio_ptr, &info, BPF_ANY);
    return 0;
}

// 2. Captured when I/O finishes completely back to the caller
SEC("fentry/bio_endio")
int BPF_PROG(bio_endio_entry, struct bio *bio)
{
    u64 bio_ptr = (u64)bio;
    struct io_info *info_ptr;

    info_ptr = bpf_map_lookup_elem(&start_bios, &bio_ptr);
    if (!info_ptr)
        return 0;

    u64 now = bpf_ktime_get_ns();
    u64 delta = now - info_ptr->start_ts;

    // Add to histogram (only reads and writes)
    if (info_ptr->op <= 1) {
        u32 idx = info_ptr->op * MAX_SLOTS + log2_slot(delta / 1000);
        u64 *count = bpf_map_lookup_elem(&hist, &idx);
        if (count)
            __sync_fetch_and_add(count, 1);
    }

    if (delta >= slow_io_ns) {
        struct disk_io_event *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
        if (e) {
            e->pid = info_ptr->pid;
            e->latency_ns = delta;
            e->timestamp_ns = now;
            e->sector = info_ptr->sector;
            e->sector_count = info_ptr->sectors;
            e->dev = info_ptr->dev;
            e->op = info_ptr->op;

            for (int i = 0; i < 16; i++) {
                e->comm[i] = info_ptr->comm[i];
            }

            bpf_ringbuf_submit(e, 0);
        } else {
            __sync_fetch_and_add(&rb_drops, 1);
        }
    }

    bpf_map_delete_elem(&start_bios, &bio_ptr);
    return 0;
}
