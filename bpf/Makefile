# Makefile for BPFs in this dir

CC := clang
STRIP := llvm-strip

EBPF_CFLAGS := -I/usr/lib/modules/$(shell uname -r)/build/include \
	-O2 -fno-stack-protector -Wall -target bpf $(EBPF_EXTRA_CFLAGS)
EBPF_STRIP := $(STRIP) -g

all: bpf.cgroup-skb.nonet.o bpf.cgroup-connect.force-bind.o

clean:
	rm -rf bpf.cgroup-skb.nonet.o bpf.cgroup-connect.force-bind.o

.SUFFIXES:

bpf.cgroup-skb.nonet.o: cgroup-skb.nonet.c
	$(CC) $(EBPF_CFLAGS) -c -o $@ $<
	$(EBPF_STRIP) $@

bpf.cgroup-connect.force-bind.o: cgroup-connect.force-bind.c
	$(CC) $(EBPF_CFLAGS) -c -o $@ $<
	$(EBPF_STRIP) $@
