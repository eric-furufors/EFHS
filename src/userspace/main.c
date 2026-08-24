#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include "sensor.skel.h"
#include "common.h"

static volatile bool exiting = false;

static void sig_handler(int sig)
{
    exiting = true;
}

static int handle_event(void *ctx, void *data, size_t data_sz) {
    const struct disk_io_event *e = data;

    // Convert nanoseconds to milliseconds
    double latency_ms = (double)e->latency_ns / 1000000.0;

    // Highlight latencies over 50ms in bold red using ANSI color codes
    const char *color_start = (latency_ms >= 50.0) ? "\033[1;31m" : "";
    const char *color_end   = (latency_ms >= 50.0) ? "\033[0m"    : "";

    printf("%-16s %-8d Latency: %s%8.2f ms%s   Sectors: %-5u\n",
           e->comm, e->pid, color_start, latency_ms, color_end, e->sector_count);

    return 0;
}

int main(int argc, char **argv) {
    struct sensor_bpf *skel;
    struct ring_buffer *rb = NULL;
    int err;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    skel = sensor_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to open and load BPF skeleton\n");
        return 1;
    }

    err = sensor_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF skeleton\n");
        goto cleanup;
    }

    rb = ring_buffer__new(bpf_map__fd(skel->maps.rb), handle_event, NULL, NULL);
    if (!rb) {
        fprintf(stderr, "Failed to create ring buffer\n");
        goto cleanup;
    }

    printf("%-16s %-6s %-18s %-8s\n", "COMM", "PID", "LATENCY", "SECTORS");
    printf("-------------------------------------------------------------------\n");

    while (!exiting) {
        err = ring_buffer__poll(rb, 100); // 100ms
        if (err < 0 && err != -EINTR) {
            fprintf(stderr, "Error polling ring buffer: %d\n", err);
            break;
        }
    }

cleanup:
    ring_buffer__free(rb);
    sensor_bpf__destroy(skel);
    return 0;
}
