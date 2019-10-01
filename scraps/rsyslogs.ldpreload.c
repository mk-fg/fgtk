/*
	LD_PRELOAD library to disable syslog() call, overriding it with no-op.
	Use-case is testing apps without them spamming local syslog.

	Compile with:
		gcc -nostartfiles -fpic -shared \
			-ldl -D_GNU_SOURCE rsyslogs.ldpreload.c -o sd.so
	Usage: LD_PRELOAD=./sd.so logger test
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <errno.h>

#include <sys/socket.h>


int (*real_connect)(int, const struct sockaddr *, socklen_t);
int (*real_sendto)( int fd, const void *message,
	size_t length, int flags, const struct sockaddr *sk, socklen_t dest_len );
int (*real_sendmsg)(int fd, const struct msghdr *msg, int flags);

void _init(void) {
	const char *err;
	real_connect = dlsym(RTLD_NEXT, "connect");
	if ((err = dlerror()) != NULL) fprintf(stderr, "dlsym (connect): %s\n", err);
	real_sendto = dlsym(RTLD_NEXT, "sendto");
	if ((err = dlerror()) != NULL) fprintf(stderr, "dlsym (sendto): %s\n", err);
	real_sendmsg = dlsym(RTLD_NEXT, "sendmsg");
	if ((err = dlerror()) != NULL) fprintf(stderr, "dlsym (sendmsg): %s\n", err);
}


int dev_log_fd = 0;

int connect(int fd, const struct sockaddr *addr, socklen_t addr_len) {
	if (addr->sa_family == AF_UNIX && !strcmp(addr->sa_data, "/dev/log"))
		{ dev_log_fd = fd; return 0; }
	return real_connect(fd, addr, addr_len);
}

ssize_t sendto(
		int fd, const void *message, size_t length,
		int flags, const struct sockaddr *sk, socklen_t dest_len ) {
	if (dev_log_fd && fd == dev_log_fd) return length;
	return real_sendto(fd, message, length, flags, sk, dest_len);
}

ssize_t sendmsg(int fd, const struct msghdr *msg, int flags) {
	if (dev_log_fd && fd == dev_log_fd) return msg->msg_iovlen;
	return real_sendmsg(fd, msg, flags);
}
