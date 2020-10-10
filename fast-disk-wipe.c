//
// Build with: gcc -O2 -o fast-disk-wipe fast-disk-wipe.c
// Usage (print usage info): ./fast-disk-wipe
//

#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <locale.h>

int main(int argc, char *argv[]) {
	if (argc < 2 || argc > 3) {
		printf("Usage: %s /dev/sdX [interval=10]\n", argv[0]);
		printf("Writes 512B blocks of 0-bytes to a device with intervals.\n");
		return -1; }

	int interval = atoi(argc > 2 ? argv[2] : "10");
	if (!interval) {
		fprintf(stderr, "ERROR: Failed to parse interval value '%s'\n", argv[2]);
		return 33; }
	if (access(argv[1], W_OK)) {
		fprintf( stderr,
			"ERROR: access(%s, W_OK) failed - %s\n", argv[1], strerror(errno) );
		return 34; }
	setlocale(LC_ALL, ""); // user selected locale

	off_t n = 0;
	char block[512] = {0,};
	int fd = open(argv[1], O_WRONLY);
	lseek(fd, 0, SEEK_SET);

	while (true) {
		if (write(fd, (void*) block, 512) < 512) break;
		if (lseek(fd, interval * 512, SEEK_CUR) < 0) break;
		n += (interval + 1) * 512; }

	printf("Finished wiping %'d bytes.\n", n);
	return 0;
}
