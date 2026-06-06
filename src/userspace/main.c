#include <stdio.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include "sensor.skel.h" 

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
    return vfprintf(stderr, format, args);
}

int main(int argc, char **argv)
{
    struct sensor_bpf *skel;
    int err;

    libbpf_set_print(libbpf_print_fn);

    skel = sensor_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to load BPF skeleton\n");
        return 1;
    }

    err = sensor_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF probes\n");
        goto cleanup;
    }

    printf("EFHS Engine started! Monitoring block I/O...\n");
    printf("Run 'sudo cat /sys/kernel/debug/tracing/trace_pipe' in another tab.\n");
    printf("Press Ctrl+C to exit.\n");

    while (1) {
        sleep(1);
    }

cleanup:
    sensor_bpf__destroy(skel);
    return -err;
}
