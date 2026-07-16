#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "../shared/common.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240); 
    __type(key, __u64);
    __type(value, __u64);
} io_start_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24); /* 16MB */
} rb SEC(".maps");

// Define the arguments matching the block_rq_issue tracepoint
struct trace_event_raw_block_rq_issue {
    unsigned short common_type;
    unsigned char common_flags;
    unsigned char common_preempt_count;
    int common_pid;
    
    dev_t dev;
    sector_t sector;
    unsigned int nr_sector;
    unsigned int bytes;
    char rwbs[8];
    char comm[16];
};

SEC("tp/block/block_rq_issue")
int handle_block_issue(struct trace_event_raw_block_rq_issue *ctx)
{
    struct disk_io_event *e;

    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e) {
        return 0; // Buffer full drop event
    }

    /* 2. Populate the struct fields with real data */
    e->pid = bpf_get_current_pid_tgid() >> 32;
    e->timestamp_ns = bpf_ktime_get_ns();
    e->latency_ns = 0;
    e->sector_count = ctx->nr_sector;
    
    bpf_get_current_comm(&e->comm, sizeof(e->comm));

    bpf_ringbuf_submit(e, 0);

    return 0;
}
