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
	if (argc < 2 || argc > 4) {
		printf("Usage: %s /dev/sdX [interval=10] [bs=512]\n", argv[0]);
		printf("Writes 512B NUL-byte blocks to a device with 10-block intervals.\n");
		return -1; }
	setlocale(LC_ALL, ""); // user selected locale

	if (access(argv[1], W_OK)) {
		fprintf(stderr, "ERROR: access(%s, W_OK) failed - %s\n", argv[1], strerror(errno));
		return 33; }

	int interval = atoi(argc > 2 ? argv[2] : "10");
	if (interval <= 0) {
		fprintf(stderr, "ERROR: Failed to parse interval value '%s'\n", argv[2]);
		return 34; }

	int bs = atoi(argc > 3 ? argv[3] : "512");
	if (bs <= 0) {
		fprintf(stderr, "ERROR: Failed to parse block-size value '%s'\n", argv[3]);
		return 35; }

	void *block = malloc(bs);
	if (!block) {
		fprintf(stderr, "ERROR: Failed to allocate %'dB - %s\n", bs, strerror(errno));
		return 36; }
	memset(block, 0, bs);

	off_t n = 0, n_bytes = 0, res;
	int fd = open(argv[1], O_WRONLY);
	lseek(fd, 0, SEEK_SET);

	while (true) {
		if (write(fd, block, bs) < bs) break;
		n++; n_bytes += 1 * bs;
		if ((res = lseek(fd, interval * bs, SEEK_CUR)) < 0) break;
		n_bytes = res; }

	printf("Finished wiping %'d bytes with %'d x %'dB blocks.\n", n_bytes, n, bs);
	return 0;
}
