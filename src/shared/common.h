#ifndef __COMMON_H__
#define __COMMON_H__


// This struct represents a singular block I/O event.

struct disk_io_event {
    unsigned long long latency_ns;    // Time delta between issue and completion
    unsigned long long timestamp_ns;  // Absolute time when the event completed
    unsigned int pid;           // The process ID that triggered the I/O
    unsigned int sector_count;  // Size of the I/O request in disk sectors
    char comm[16];       // Process name
};

#endif
