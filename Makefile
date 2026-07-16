OUTPUT := .output
CLANG := clang
BPFTOOL := /usr/sbin/bpftool

INCLUDES := -I$(OUTPUT) -I./src/shared -I./src/userspace

CFLAGS := -g -Wall
ALL_LDFLAGS := -lbpf -lelf -lz -lpthread

.PHONY: all clean
all: $(OUTPUT) efhs-app

clean:
	rm -rf $(OUTPUT) efhs-app src/userspace/sensor.skel.h

$(OUTPUT):
	mkdir -p $(OUTPUT)

$(OUTPUT)/sensor.bpf.o: src/ebpf/sensor.bpf.c src/shared/vmlinux.h | $(OUTPUT)
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_x86 -I./src/shared -c $< -o $@

# Generates the skeleton using the global system bpftool
src/userspace/sensor.skel.h: $(OUTPUT)/sensor.bpf.o | $(OUTPUT)
	$(BPFTOOL) gen skeleton $< > $@

# Link efhs-app using the system's global libbpf (-lbpf in ALL_LDFLAGS)
efhs-app: src/userspace/main.c src/userspace/sensor.skel.h
	$(CC) $(CFLAGS) $(INCLUDES) $< $(ALL_LDFLAGS) -o $@
