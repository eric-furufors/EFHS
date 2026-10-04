#ifndef __COMMON_H__
#define __COMMON_H__

#define MAX_SLOTS 27   // histogram buckets, bucket k = 2^k to 2^(k+1) microseconds

// This struct represents a singular block I/O event.

struct disk_io_event {
    unsigned long long latency_ns;    // Time delta between issue and completion
    unsigned long long timestamp_ns;  // Time when the event completed
    unsigned long long sector;        // Start sector of the request
    unsigned int pid;           // The process ID that triggered the I/O
    unsigned int sector_count;  // Size of the I/O request in disk sectors
    unsigned int dev;           // Device number (major/minor packed)
    unsigned int op;            // 0 = read, 1 = write
    char comm[16];       // Process name
};

#endif
