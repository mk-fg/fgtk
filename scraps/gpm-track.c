// Proxy for GPM [ https://github.com/telmich/gpm ] mouse events to shm/signals
// Build with: gcc -O2 gpm-track.c -o gpm-track -lgpm -lrt
// Usage example: ./gpm-track --pid 26629 --shm gpm-test < /dev/tty3
// More info: ./gpm-track -h
// See also: gpm-track.py in the same dir.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include <getopt.h>
#include <limits.h>
#include <errno.h>

// Good gpm tutorial/examples - https://www.linuxjournal.com/article/4600
// Docs: info gpm
#include <gpm.h>

#define P(err, fmt, arg...)\
	do {\
		fprintf(stderr, "ERROR: " fmt "\n", ##arg);\
		if (err) exit(err);\
	} while (0)

long shm_len;
void *shm;
int signal_pid = 0, signal_base = 40;

void *create_shared_memory(char *filename, size_t size) {
	void *shm = NULL;

	mode_t umask_mode = umask(0);
	int mmap_fd = shm_open(filename, O_CREAT|O_RDWR, 00600);
	umask(umask_mode);
	if (mmap_fd < 0) { perror("shm_open() failed"); return NULL; }

	int res = ftruncate(mmap_fd, size) == 0 ? 1 : 0;
	if (res) res = lseek(mmap_fd, size - 1, SEEK_SET) != -1 ? 1 : 0;
	if (res) res = write(mmap_fd, "", 1) != -1 ? 1 : 0;
	if (res) {
		shm = mmap( NULL, size,
			PROT_READ | PROT_WRITE, MAP_SHARED, mmap_fd, 0 );
		res = (shm != MAP_FAILED && shm != NULL) ? 1 : 0; }
	if (!res) { perror("ERROR: mmap failed"); close(mmap_fd); return NULL; }

	return shm;
}

int event_handler(Gpm_Event *event, void *data) {
	sprintf((char *) data, "%05d %05d\n", event->x, event->y);
	if (event->type & GPM_DOWN) {
		int click_t =
			event->type & GPM_SINGLE ? 1 :
			event->type & GPM_DOUBLE ? 2 :
			event->type & GPM_TRIPLE ? 3 : 0;
		int button =
			event->buttons & GPM_B_LEFT ? 1 :
			event->buttons & GPM_B_RIGHT ? 2 :
			event->buttons & GPM_B_MIDDLE ? 3 : 0;
		sprintf( ((char *) data) + 12,
			"{\"b\": %d, \"t\": %d, \"x\": %d, \"y\": %d}\n",
			button, click_t, event->x, event->y );
		if (signal_pid && button > 0)
			kill(signal_pid, signal_base + button + (click_t << 2));
	}
	return 0;
}

void parse_opts( int argc, char *argv[],
		char **opt_shm, int *opt_pid ) {
	extern char *optarg;
	extern int optind, opterr, optopt;

	int res = (*opt_shm = malloc(32)) != NULL ? 1 : 0;
	if (res) res = snprintf(*opt_shm, 32, "gpm-track.%d", getpid()) < 32 ? 1 : 0;
	if (!res) P(1, "failed init -s/--shm value");

	void usage(int err) {
		FILE *dst = !err ? stdout : stderr;
		fprintf( dst,
"Usage: %s [-h|--help] [-s|--shm file] [-p|--pid pid] < ttyX\n\n"
"Handle libgpm events and dump info on these"
	" to specified -s/--shm file (default: 'gpm-track.{pid}').\n\n"
"If -p/--pid is specified, it will be sent SIGRT-X on any mouse events,\n"
	" where 'X' is '%d + mask' and 'mask' is (button | 2 << clicks),\n"
	" button={1=left, 2=right, 3=middle},"
	" clicks={1=single, 2=double, 3=triple}.\n\n"
			, argv[0], signal_base );
		exit(err); }

	int ch;
	static struct option opt_list[] = {
		{"help", no_argument, NULL, 1},
		{"shm", required_argument, NULL, 2},
		{"pid", required_argument, NULL, 3} };
	while ((ch = getopt_long(argc, argv, ":hs:p:", opt_list, NULL)) != -1)
		switch (ch) {
			case 's': case 2: *opt_shm = strdup(optarg); break;
			case 'p': case 3:
				errno = 0;
				*opt_pid = (int) strtol(optarg, NULL, 10);
				if (*opt_pid <= 0 || (errno == ERANGE && (
						*opt_pid == (int) LONG_MIN || *opt_pid == (int) LONG_MAX )))
					P(1, "invalid pid value: %s", optarg);
				break;
			case 'h': case 1: usage(0);
			case '?':
				P(0, "unrecognized option - %s\n", argv[optind-1]);
				usage(1);
			case ':':
				if (optopt >= 32) P(0, "missing argument for -%c\n", optopt);
				else P(0, "missing argument for --%s\n", opt_list[optopt-1].name);
				usage(1);
			default: usage(1); }
	if (optind < argc) {
		P(0, "unrecognized argument value - %s\n", argv[optind]);
		usage(1); }
}

int main(int argc, char *argv[]) {
	char *opt_shm; int opt_pid = 0;
	parse_opts(argc, argv, &opt_shm, &opt_pid);
	if (opt_pid > 0) signal_pid = opt_pid;
	if ( SIGRTMIN > signal_base ||
			SIGRTMAX < signal_base + (1 << 4) )
		P( 1,
			"no SIGRT* space: range=[%d - %d], need=[%d - %d]",
			SIGRTMIN, SIGRTMAX, signal_base, signal_base + (1 << 4) );

	shm_len = sysconf(_SC_PAGESIZE);
	shm = create_shared_memory(opt_shm, (size_t) shm_len);
	if (!shm) P(1, "shm failed");

	Gpm_Connect conn; int c;

	conn.eventMask = ~0; // want to know about all the events
	conn.defaultMask = 0; // don't handle anything by default
	conn.minMod = 0; // want everything
	conn.maxMod = ~0; // all modifiers included

	setenv("TERM", "linux", 1); // Gpm_Open returns raw stdin if not linux/xterm
	if (Gpm_Open(&conn, 0) == -1)
		P(1, "failed to connect to gpm server");

	gpm_handler = event_handler;
	gpm_data = shm;

	while ((c = Gpm_Getc(stdin)) != EOF) printf("%c", c);
	Gpm_Close();

	return 0;
}
