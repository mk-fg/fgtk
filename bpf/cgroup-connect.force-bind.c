// eBPF cgroup-connect hooks to force-bind socket to a specific IPv4/IPv6 address.
// Port is left at 0 to be picked automatically.

// Compile:
//  clang -O2 -fno-stack-protector -Wall \
//   -I/usr/lib/modules/$(uname -r)/build/include \
//   -target bpf -c cgroup-connect.force-bind.c -o bpf.cgroup-connect.force-bind.o
//  (note - gcc-10+ circa Q2-2020+ should also have BPF target)

// Load - swap 4/6 for IPv4/IPv6 ones:
//  bpftool prog load bpf.cgroup-connect.force-bind.o \
//   /sys/fs/bpf/cgroup-connect-force-bind4 type cgroup/connect4
//  (use "bpftool -d" to debug why stuff fails to load)

#include <linux/version.h>
#include <uapi/linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>


// From uapi/linux/in.h uapi/linux/in6.h
#define AF_INET 2
#define AF_INET6 10

struct in_addr {
	__be32 s_addr;
};
struct sockaddr_in {
	unsigned short int sin_family;
	__be16 sin_port;
	struct in_addr sin_addr;
	unsigned char __pad[8];
};

struct in6_addr {
	__be32 s6_addr32[4];
};
struct sockaddr_in6 {
	unsigned short int sin6_family;
	__be16 sin6_port;
	__be32 sin6_flowinfo;
	struct in6_addr sin6_addr;
	__u32 sin6_scope_id;
};



SEC("cgroup/connect4")
int connect4_force_bind(struct bpf_sock_addr *ctx) {
	// s_addr can be generated via: ip.ip_address('10.16.0.17').packed.hex()
	struct sockaddr_in sa = {};
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = bpf_htonl(0x0a100011);
	if (bpf_bind(ctx, (struct sockaddr *)&sa, sizeof(sa)) != 0) return 0;
	return 1;
}

SEC("cgroup/connect6")
int connect6_force_bind(struct bpf_sock_addr *ctx) {
	// s6_addr can be generated via: ip.ip_address('fd10::17').packed.hex()
	struct sockaddr_in6 sa = {};
	sa.sin6_family = AF_INET6;
	sa.sin6_addr.s6_addr32[0] = bpf_htonl(0xfd100000);
	sa.sin6_addr.s6_addr32[1] = 0;
	sa.sin6_addr.s6_addr32[2] = 0;
	sa.sin6_addr.s6_addr32[3] = bpf_htonl(0x00000017);
	if (bpf_bind(ctx, (struct sockaddr *)&sa, sizeof(sa)) != 0) return 0;
	return 1;
}

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
