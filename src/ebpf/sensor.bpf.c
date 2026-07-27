#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "../shared/common.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

// Struct to stash inside the hash map while the I/O is running
struct io_request_info {
    __u64 start_time;
    __u32 pid;
    char comm[16];
};

/* 
    I/O Start Map:
        Key:   The request sector address (__u64)
        Value: Struct containing start time, original PID, and original process name
 */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240); 
    __type(key, __u64);
    __type(value, struct io_request_info);
} io_start_map SEC(".maps");

// Ring Buffer Map
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24); /* 16MB */
} rb SEC(".maps");

// Issued by the initiating process
SEC("tp/block/block_rq_issue")
int handle_block_issue(struct trace_event_raw_block_rq *ctx)
{
    struct io_request_info info = {};
    __u64 sector = ctx->sector;

    info.start_time = bpf_ktime_get_ns();
    info.pid = bpf_get_current_pid_tgid() >> 32;
    bpf_get_current_comm(&info.comm, sizeof(info.comm));

    // Store the context using the sector number as the tracking ID
    bpf_map_update_elem(&io_start_map, &sector, &info, BPF_ANY);

    return 0;
}

// Completed by hardware interrupt
SEC("tp/block/block_rq_complete")
int handle_block_complete(struct trace_event_raw_block_rq *ctx)
{
    __u64 end_time = bpf_ktime_get_ns();
    __u64 sector = ctx->sector;
    struct io_request_info *info_ptr;

    // Look up original transaction info
    info_ptr = bpf_map_lookup_elem(&io_start_map, &sector);
    if (!info_ptr) {
        return 0; // Missed start event
    }

    // Reserve Ring Buffer slot
    struct disk_io_event *e;
    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e) {
        bpf_map_delete_elem(&io_start_map, &sector);
        return 0;
    }

    // Attribute data back to the ORIGINATING process
    e->pid = info_ptr->pid;
    e->timestamp_ns = end_time;
    e->latency_ns = end_time - info_ptr->start_time;
    e->sector_count = ctx->nr_sector;
    
    // Copy the original command name stashed during issue
    for (int i = 0; i < 16; i++) {
        e->comm[i] = info_ptr->comm[i];
    }

    bpf_ringbuf_submit(e, 0);

    // Clean up memory
    bpf_map_delete_elem(&io_start_map, &sector);

    return 0;
}
