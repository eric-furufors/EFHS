#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

SEC("tp/block/block_rq_issue")
int handle_block_issue(void *ctx)
{
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    bpf_printk("EFHS SENSOR: I/O triggered by PID %d\n", pid);
    return 0;
}
