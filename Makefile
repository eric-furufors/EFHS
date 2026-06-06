OUTPUT := .output
CLANG := clang
BPFTOOL_SRC := $(abspath $(HOME)/libbpf-bootstrap/bpftool/src)
# Point directly to the working library asset compiled by bpftool
LIBBPF_OBJ := $(OUTPUT)/bpftool-dir/bootstrap/libbpf/libbpf.a
BPFTOOL := $(OUTPUT)/bpftool-local

INCLUDES := -I$(OUTPUT) -I./src/shared -I./src/userspace -I$(OUTPUT)/bpftool-dir/bootstrap/libbpf/include

CFLAGS := -g -Wall
ALL_LDFLAGS := -lelf -lz -lpthread

.PHONY: all clean
all: $(OUTPUT) efhs-app

clean:
	rm -rf $(OUTPUT) efhs-app src/userspace/sensor.skel.h

$(OUTPUT):
	mkdir -p $(OUTPUT)

$(BPFTOOL): | $(OUTPUT)
	mkdir -p $(OUTPUT)/bpftool-dir
	cd $(BPFTOOL_SRC) && $(MAKE) OUTPUT=$(abspath $(OUTPUT)/bpftool-dir)/ bootstrap
	cp $(OUTPUT)/bpftool-dir/bootstrap/bpftool $(BPFTOOL)
	chmod +x $(BPFTOOL)

$(OUTPUT)/sensor.bpf.o: src/ebpf/sensor.bpf.c src/shared/vmlinux.h | $(OUTPUT)
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_x86 -I./src/shared -c $< -o $@

src/userspace/sensor.skel.h: $(OUTPUT)/sensor.bpf.o $(BPFTOOL)
	$(BPFTOOL) gen skeleton $< > $@

# Link efhs-app using the bpftool-compiled libbpf.a dependency tracker
efhs-app: src/userspace/main.c src/userspace/sensor.skel.h $(BPFTOOL)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIBBPF_OBJ) $(ALL_LDFLAGS) -o $@
