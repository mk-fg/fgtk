// Safer "rm" tool for restricting all file removals to a specific dir.
// Build: gcc -Wall -O2 -o rmx rmx.c && strip rmx
// Usage info: ./rmx -h

#include <fcntl.h>
#include <linux/openat2.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <libgen.h>
#include <errno.h>
#include <err.h>


void print_usage(char *prog, int code) {
	printf("Usage: %s [-h/--help] [-d <dir>] [-x] [-f/--force] [--] <files...>\n\n", prog);
	printf(
		"Remove specified files, same as rm(1) would do, but with some extra options:\n\n"

		"  -d - Base directory which all specified files must be under.\n"
		"     This includes absolute file paths, after resolving symlinks in those.\n"
		"     Relative file paths will be interpreted to be under this directory.\n\n"

		"  -x - Signal error for file paths which cross mountpoints.\n"
		"     I.e. files must not be mountpoints or have any\n"
		"      mountpoints in-between base directory and final filename.\n"
		"     If base-dir (-d) isn't specified, matches mountpoint to cwd.\n\n"

		"  -f/--force - Skip non-existent files, including ones with bogus paths.\n"
		"     Errors for files outside base-dir (-d) or cross-mount removals (-x)\n"
		"      will still be report and set non-zero exit code, but won't stop operation.\n"
		"     Normally everything stops immediately at any detected error.\n\n"

		"  -h/--help - print this usage info.\n\n" );
	exit(code); }


// Example: openat2(dir_fd, path, .flags=O_RDONLY, .resolve=RESOLVE_NO_SYMLINKS);
#define openat2(dir_fd, path, ...) rmx_openat2(dir_fd, path, (struct open_how){__VA_ARGS__});
int rmx_openat2(int dir_fd, const char* path, struct open_how how) {
	return syscall(SYS_openat2, dir_fd, path, &how, sizeof(struct open_how)); }

int main(int argc, char *argv[]) {
	if (argc <= 1) print_usage(argv[0], 1);

	int idx = 0;
	int file_dir_fds[argc-1];
	char *file_names[argc-1];
	char *file_paths[argc-1];

	char *dir_check = NULL;
	bool dev_check = false, force = false, args = false;
	for (int n = 1; n < argc; n++) {
		char *p = argv[n];
		if (!args) {
			if (!strcmp(p, "-h") || !strcmp(p, "--help")) print_usage(argv[0], 0);
			if (!strcmp(p, "-d")) { dir_check = argv[++n]; continue; }
			if (!strcmp(p, "-x")) { dev_check = true; continue; }
			if (!strcmp(p, "-f")) { force = true; continue; }
			if (!strcmp(p, "--")) { args = true; continue; } }
		file_paths[idx++] = p; }

	int open_resolve = RESOLVE_NO_SYMLINKS;
	if (dev_check) open_resolve |= RESOLVE_NO_XDEV;

	int dir_fd, dir_offset;
	if (dir_check) {
		char *dir = realpath(dir_check, NULL);
		if (!dir) err(1, "ERROR: Base-dir is missing/inaccessible [ %s ]", dir_check);
		dir_fd = openat2( AT_FDCWD, dir,
			.flags=O_RDONLY|O_DIRECTORY, .resolve=RESOLVE_NO_SYMLINKS );
		if (dir_fd < 0) err(1, "ERROR: Base-dir open failed [ %s ]", dir_check);
		if (fchdir(dir_fd)) err(1, "ERROR: Base-dir chdir failed [ %s ]", dir_check);
		dir_check = dir; dir_offset = strlen(dir_check); }
	else dir_fd = AT_FDCWD;

	int res = 0, nn = idx; idx = 0;
	for (int n = 0; n < nn; n++) {
		char *p0 = file_paths[n], *p = realpath(p0, NULL);
		if (!p) {
			if (!(force && errno == ENOENT)) {
				fprintf( stderr, "ERROR: File access error"
					" [ %s ]: %s\n", p0, strerror(errno) );
				res |= 1; }
			continue; }

		if (dir_check) {
			if (strncmp(p, dir_check, dir_offset) || p[dir_offset] != '/') {
				fprintf(stderr, "ERROR: Path not inside specified dir [ %s ]\n", p0);
				res |= 2; continue; }
			p += dir_offset + 1; }

		file_paths[idx] = p0;
		file_dir_fds[idx] = openat2(
			dir_fd, dirname(strdup(p)),
			.flags=O_RDONLY|O_DIRECTORY, .resolve=open_resolve );
		file_names[idx++] = basename(p); }
	if (res) return res;

	for (int n = 0; n < idx; n++)
		if (unlinkat(file_dir_fds[n], file_names[n], 0))
			if (!(force && errno == ENOENT))
				err(res | 4, "ERROR: Failed to remove file [ %s ]", file_paths[n]);
	return res;
}
