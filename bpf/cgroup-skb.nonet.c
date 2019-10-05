// eBPF filter to disable network access except for IPv4/IPv6 localhost.

// Compile:
//  clang -O2 -fno-stack-protector -Wall \
//   -I/usr/lib/modules/$(uname -r)/build/include \
//   -target bpf -c cgroup-skb.nonet.c -o bpf.cgroup-skb.nonet.o
//  (note - gcc-10+ circa Q2-2020+ should also have BPF target)

// Load:
//  bpftool prog load bpf.cgroup-skb.nonet.o \
//   /sys/fs/bpf/cgroup-skb-nonet type cgroup/skb
//  (use "bpftool -d" to debug why stuff fails to load)


#include <linux/version.h>
#include <uapi/linux/bpf.h>

// From /usr/src/linux/tools/testing/selftests/bpf/bpf_helpers.h
#include "bpf_helpers.h"


#define ETH_P_IP 0x8
#define ETH_P_IPV6 0xdd86

struct iphdr {
	__u8 ihl : 4;
	__u8 version : 4;
	__u8 tos;
	__u16 tot_len;
	__u16 id;
	__u16 frag_off;
	__u8 ttl;
	__u8 protocol;
	__u16 check;
	__u32 saddr;
	__u32 daddr;
} __attribute__((packed));

struct ipv6hdr {
	__u8 ihl : 4;
	__u8 version : 4;
	__u8 flow_lbl[3];
	__be16 payload_len;
	__u8 nexthdr;
	__u8 hop_limit;
	__u64 saddr1;
	__u64 saddr2;
	__u64 daddr1;
	__u64 daddr2;
} __attribute__((packed));


SEC("cgroup/skb")
int drop_all_packets(struct __sk_buff *skb) {
	// See: bpf-helpers(7), tc-bpf(8)
	//   https://www.kernel.org/doc/Documentation/networking/filter.txt
	//   https://github.com/iovisor/bcc/blob/master/docs/reference_guide.md

	/* char fmt[] = "addr %x\n"; */
	/* bpf_trace_printk(fmt, sizeof(fmt), addr); */

	__u32 dst_ip = 0;
	__u64 dst_ip6a = 0, dst_ip6b = 0;

	// IPv4 localhost - 127.0.0.1
	if ( skb->protocol == ETH_P_IP
		&& !bpf_skb_load_bytes( skb,
			offsetof(struct iphdr, daddr), &dst_ip, sizeof(dst_ip) )
		&& dst_ip == 0x100007f ) return 1;

	// IPv6 localhost - [::1]
	if ( skb->protocol == ETH_P_IPV6
		&& !bpf_skb_load_bytes( skb,
			offsetof(struct ipv6hdr, daddr1), &dst_ip6a, sizeof(dst_ip6a) )
		&& !bpf_skb_load_bytes( skb,
			offsetof(struct ipv6hdr, daddr2), &dst_ip6b, sizeof(dst_ip6b) )
		&& dst_ip6a == 0 && dst_ip6b == 0x100000000000000 ) return 1;

	return 0; // block everything else
}


char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
