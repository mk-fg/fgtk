/*
	Based on: https://raw.githubusercontent.com/yongboy/bindp/
	Original Copyright (C) 2014 nieyong
	email: nieyong@staff.weibo.com
	web: http://www.blogjava.net/yongboy
	License: LGPL-2.1
*/

/*
	LD_PRELOAD library to override bind() and sendto(),
		forcing bind() to use specific options depending on env vars:

	- MNB_IPV4=1.2.3.4 - specified IPv4 address.
	- MNB_PORT=34730 - specified port.
	- MNB_REUSE_ADDR=1 - SO_REUSEADDR option - socket(7).
	- MNB_REUSE_PORT=1 - SO_REUSEPORT option - socket(7).
	- MNB_IP_TRANSPARENT=1 - IP_TRANSPARENT option - ip(7).

	Limitations (hacks):
	- Only binds IPv4 (AF_INET) sockets.
	- Tracks last fd used in sendto(fd, ...) and does bind() for it once.

	Here to force mosh-client to connect from specified local port.

	Compile on Linux (>=3.9) with:
		gcc -nostartfiles -fpic -shared \
			-ldl -D_GNU_SOURCE mosh-nat-bind.c -o mnb.so

	Usage example relevant to mosh-client:
		MNB_PORT=34731 LD_PRELOAD=./mnb.so \
			MOSH_KEY=KBwxklPHaRqV7l5OgE3OsA mosh-client 10.0.1.13 34732
	(connecting from 10.0.1.1:3731 to 10.0.1.13:3732)
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>


int (*real_bind)(int, const struct sockaddr *, socklen_t);
int (*real_sendto)( int fd, const void *message,
	size_t length, int flags, const struct sockaddr *sk, socklen_t dest_len );

unsigned long int bind_addr_saddr = 0;
struct sockaddr_in local_sockaddr_in[] = { 0 };

unsigned int bind_port_saddr = 0;
unsigned int reuse_port = 0;
unsigned int reuse_addr = 0;
unsigned int ip_transparent = 0;

int bind_last_fd = -1;


void _init(void){
	const char *err;

	real_bind = dlsym(RTLD_NEXT, "bind");
	if ((err = dlerror()) != NULL) fprintf(stderr, "dlsym (bind): %s\n", err);
	real_sendto = dlsym(RTLD_NEXT, "sendto");
	if ((err = dlerror()) != NULL) fprintf(stderr, "dlsym (sendto): %s\n", err);

	char *bind_addr_env;
	if ((bind_addr_env = getenv("MNB_IPV4"))) {
		bind_addr_saddr = inet_addr(bind_addr_env);
		local_sockaddr_in->sin_family = AF_INET;
		local_sockaddr_in->sin_addr.s_addr = bind_addr_saddr;
		local_sockaddr_in->sin_port = htons(0);
	}

	char *bind_port_env;
	if ((bind_port_env = getenv("MNB_PORT"))) {
		bind_port_saddr = atoi(bind_port_env);
		local_sockaddr_in->sin_port = htons(bind_port_saddr);
	}

	char *reuse_addr_env;
	if ((reuse_addr_env = getenv("MNB_REUSE_ADDR")))
		reuse_addr = atoi(reuse_addr_env);

	char *reuse_port_env;
	if ((reuse_port_env = getenv("MNB_REUSE_PORT")))
		reuse_port = atoi(reuse_port_env);

	char *ip_transparent_env;
	if ((ip_transparent_env = getenv("MNB_IP_TRANSPARENT")))
		ip_transparent = atoi(ip_transparent_env);
}

int bind(int fd, const struct sockaddr *sk, socklen_t sl) {
	static struct sockaddr_in *lsk_in;
	lsk_in = (struct sockaddr_in *)sk;

	if (bind_addr_saddr) lsk_in->sin_addr.s_addr = bind_addr_saddr;
	if (bind_port_saddr) lsk_in->sin_port = htons(bind_port_saddr);

	if (reuse_addr)
		setsockopt( fd, SOL_SOCKET,
			SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr) );
	if (reuse_port)
		setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse_port, sizeof(reuse_port));
	if (ip_transparent)
		setsockopt( fd, SOL_IP,
			IP_TRANSPARENT, &ip_transparent, sizeof(ip_transparent) );

	return real_bind(fd, sk, sl);
}

ssize_t sendto(
		int fd, const void *message, size_t length,
		int flags, const struct sockaddr *sk, socklen_t dest_len ) {
	static struct sockaddr_in *rsk_in;
	rsk_in = (struct sockaddr_in *)sk;

	if (bind_last_fd != fd) {
		bind_last_fd = fd;
		if ( (rsk_in->sin_family == AF_INET)
				&& (bind_addr_saddr || bind_port_saddr) )
			bind(fd, (struct sockaddr *)local_sockaddr_in, sizeof (struct sockaddr));
	}

	return real_sendto(fd, message, length, flags, sk, dest_len);
}
