#include <linux/version.h>
#include <uapi/linux/bpf.h>
/* #include "bpf_helpers.h" */

// Compile:
//  clang -O2 -Wall -I/usr/lib/modules/$(uname -r)/build/include \
//   -target bpf -c cgroup-skb.drop-all.c -o bpf.cgroup-skb.drop-all.o
//  (note - gcc-10+ circa Q2-2020+ should also have BPF target)
// Load:
//  bpftool prog load bpf.cgroup-skb.drop-all.o \
//   /sys/fs/bpf/cgroup-skb-drop-all type cgroup/skb
//  (use "bpftool -d" to debug why stuff fails to load)

// See tools/testing/selftests/bpf/bpf_helpers.h
#define SEC(NAME) __attribute__((section(NAME), used))

SEC("cgroup/skb")
int drop_all_packets(struct __sk_buff *skb) {
	// See: bpf-helpers(7), tc-bpf(8)
	//   https://www.kernel.org/doc/Documentation/networking/filter.txt
	//   https://github.com/iovisor/bcc/blob/master/docs/reference_guide.md
	return 0; // block
}

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
