#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "../shared/common.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

#define SLOW_IO_NS 20000000ULL // 20ms threshold

struct io_info {
    u64 start_ts;
    u32 pid;
    char comm[16];
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, u64); // Pointer to bio struct
    __type(value, struct io_info);
} start_bios SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24);
} rb SEC(".maps");

// 1. Captured when I/O enters the kernel block layer (BEFORE dm-delay)
SEC("fentry/submit_bio")
int BPF_PROG(submit_bio_entry, struct bio *bio)
{
    u64 bio_ptr = (u64)bio;
    struct io_info info = {};

    info.start_ts = bpf_ktime_get_ns();
    info.pid = bpf_get_current_pid_tgid() >> 32;
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

    u64 delta = bpf_ktime_get_ns() - info_ptr->start_ts;

    if (delta >= SLOW_IO_NS) {
        struct disk_io_event *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
        if (e) {
            e->pid = info_ptr->pid;
            e->latency_ns = delta;
            
            // Get number of sectors from bio size
            e->sector_count = bio->bi_iter.bi_size >> 9; 

            for (int i = 0; i < 16; i++) {
                e->comm[i] = info_ptr->comm[i];
            }

            bpf_ringbuf_submit(e, 0);
        }
    }

    bpf_map_delete_elem(&start_bios, &bio_ptr);
    return 0;
}
