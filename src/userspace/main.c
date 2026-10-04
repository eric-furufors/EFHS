#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "sensor.skel.h"
#include "common.h"

static volatile bool exiting = false;
static FILE *csv_file = NULL;
static FILE *hist_file = NULL;

static void sig_handler(int sig)
{
    exiting = true;
}

static int handle_event(void *ctx, void *data, size_t data_sz) {
    const struct disk_io_event *e = data;

    // Convert nanoseconds to milliseconds
    double latency_ms = (double)e->latency_ns / 1000000.0;
    unsigned int major = e->dev >> 20;
    unsigned int minor = e->dev & 0xfffff;
    const char *op = (e->op == 0) ? "R" : (e->op == 1) ? "W" : "O";

    // Highlight latencies over 50ms in bold red using ANSI color codes
    const char *color_start = (latency_ms >= 50.0) ? "\033[1;31m" : "";
    const char *color_end   = (latency_ms >= 50.0) ? "\033[0m"    : "";

    printf("%-16s %-8d %u:%-4u %s Latency: %s%8.2f ms%s   Sectors: %-5u\n",
           e->comm, e->pid, major, minor, op, color_start, latency_ms, color_end, e->sector_count);

    // Append event to CSV file
    if (csv_file) {
        fprintf(csv_file, "%llu,%d,%s,%u:%u,%s,%.3f,%u,%llu\n",
                e->timestamp_ns, e->pid, e->comm, major, minor, op,
                latency_ms, e->sector_count, e->sector);
    }

    return 0;
}

// Adds up the per-cpu histogram
static void read_hist(int fd, int ncpu, unsigned long long out[2][MAX_SLOTS])
{
    unsigned long long vals[ncpu];

    for (int rw = 0; rw < 2; rw++) {
        for (int s = 0; s < MAX_SLOTS; s++) {
            unsigned int key = rw * MAX_SLOTS + s;
            unsigned long long sum = 0;

            if (bpf_map_lookup_elem(fd, &key, vals) == 0) {
                for (int c = 0; c < ncpu; c++)
                    sum += vals[c];
            }
            out[rw][s] = sum;
        }
    }
}

// Percentile in ms, guessed inside the bucket so it is not exact
static double percentile(unsigned long long *counts, double p)
{
    unsigned long long total = 0, cum = 0;

    for (int i = 0; i < MAX_SLOTS; i++)
        total += counts[i];
    if (total == 0)
        return 0.0;

    double target = p * total;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (counts[i] > 0 && cum + counts[i] >= target) {
            double lo = (i == 0) ? 0.0 : (double)(1ULL << i);
            double hi = (double)(1ULL << (i + 1));
            double frac = (target - cum) / counts[i];
            return (lo + (hi - lo) * frac) / 1000.0;
        }
        cum += counts[i];
    }
    return 0.0;
}

// Prints p50/p95/p99 for the last interval and saves buckets to hist.csv
static void report(int fd, int ncpu, unsigned long long prev[2][MAX_SLOTS])
{
    const char *names[2] = {"READ", "WRITE"};
    unsigned long long cur[2][MAX_SLOTS];
    unsigned long long delta[2][MAX_SLOTS];

    read_hist(fd, ncpu, cur);

    for (int rw = 0; rw < 2; rw++) {
        unsigned long long n = 0;
        for (int s = 0; s < MAX_SLOTS; s++) {
            delta[rw][s] = cur[rw][s] - prev[rw][s];
            prev[rw][s] = cur[rw][s];
            n += delta[rw][s];

            if (hist_file && delta[rw][s] > 0) {
                fprintf(hist_file, "%ld,%s,%llu,%llu\n", time(NULL), names[rw],
                        (s == 0) ? 0ULL : (1ULL << s), delta[rw][s]);
            }
        }
        if (n > 0) {
            printf("[hist] %-5s n=%-8llu p50=%.3f p95=%.3f p99=%.3f ms\n", names[rw], n,
                   percentile(delta[rw], 0.50), percentile(delta[rw], 0.95),
                   percentile(delta[rw], 0.99));
        }
    }

    if (hist_file) fflush(hist_file);
    if (csv_file) fflush(csv_file);
}

int main(int argc, char **argv) {
    struct sensor_bpf *skel;
    struct ring_buffer *rb = NULL;
    unsigned long long prev[2][MAX_SLOTS] = {};
    double threshold_ms = 20.0;
    int err;

    // Optional first argument is the threshold in ms, 0 = send every I/O
    if (argc > 1)
        threshold_ms = atof(argv[1]);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    int ncpu = libbpf_num_possible_cpus();
    if (ncpu < 1) {
        fprintf(stderr, "Could not get cpu count\n");
        return 1;
    }

    // Open CSV file in append mode and add header if file is empty
    csv_file = fopen("events.csv", "a");
    if (csv_file) {
        fseek(csv_file, 0, SEEK_END);
        if (ftell(csv_file) == 0) {
            fprintf(csv_file, "timestamp_ns,pid,comm,dev,op,latency_ms,sectors,sector\n");
        }
    }

    hist_file = fopen("hist.csv", "a");
    if (hist_file) {
        fseek(hist_file, 0, SEEK_END);
        if (ftell(hist_file) == 0) {
            fprintf(hist_file, "timestamp,op,bucket_lo_us,count\n");
        }
    }

    skel = sensor_bpf__open();
    if (!skel) {
        fprintf(stderr, "Failed to open BPF skeleton\n");
        if (csv_file) fclose(csv_file);
        return 1;
    }

    // Has to be set before load
    skel->rodata->slow_io_ns = (unsigned long long)(threshold_ms * 1000000.0);

    err = sensor_bpf__load(skel);
    if (err) {
        fprintf(stderr, "Failed to load BPF skeleton\n");
        goto cleanup;
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

    int hist_fd = bpf_map__fd(skel->maps.hist);

    printf("Threshold: %.1f ms\n", threshold_ms);
    printf("%-16s %-6s %-8s %-2s %-18s %-8s\n", "COMM", "PID", "DEV", "OP", "LATENCY", "SECTORS");
    printf("-------------------------------------------------------------------\n");

    time_t last_report = time(NULL);

    while (!exiting) {
        err = ring_buffer__poll(rb, 100); // 100ms
        if (err < 0 && err != -EINTR) {
            fprintf(stderr, "Error polling ring buffer: %d\n", err);
            break;
        }

        // Print histogram every 10 seconds
        if (time(NULL) - last_report >= 10) {
            report(hist_fd, ncpu, prev);
            last_report = time(NULL);
        }
    }

    report(hist_fd, ncpu, prev);
    if (skel->bss->rb_drops > 0)
        printf("Warning: %llu events were dropped\n", (unsigned long long)skel->bss->rb_drops);

cleanup:
    if (csv_file) {
        fclose(csv_file);
    }
    if (hist_file) {
        fclose(hist_file);
    }
    ring_buffer__free(rb);
    sensor_bpf__destroy(skel);
    return 0;
}
