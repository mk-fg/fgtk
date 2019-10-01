#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// SUID wrapper to prevent rsyslog from using /dev/log via unshare.
// Simple suid unshare leaves real-uid as-is and mount checks that and fails.
//
// build: gcc -O2 -o rsyslogs rsyslogs.c && strip rsyslogs
// perms: chown root:user rsyslogs && chmod 4110 rsyslogs
// run: ./rsyslogs

int main(int argc, char **argv) {
	clearenv();
	int uid;

	if (argc == 1) {
		// unshare part
		uid = getuid();
		if (setreuid(0, 0)) exit(135);
		int n, bin_len = 250; char bin[bin_len]; char cmd[bin_len];
		if ((n = readlink("/proc/self/exe", bin, bin_len)) > 0) {
			bin[n] = 0;
			n = sprintf( cmd,
				"mount -o bind /dev/null /dev/log"
				" && exec %s rsyslog %d", bin, uid ); }
		if (n < 0) exit(135);
		execl("/usr/bin/unshare", "unshare", "-m", "sh", "-c", cmd, NULL);
		exit(135); }

	// rsyslog part
	if (argc < 3 || strcmp(argv[1], "rsyslog")) exit(136);
	if (!(uid = atoi(argv[2]))) exit(136);
	if (setreuid(uid, uid)) exit(137);
	execl( "/usr/bin/rsyslogd",
		"rsyslogd", "-n", "-iNONE", "-f", "rsyslog.conf", NULL );
	exit(138);
}
