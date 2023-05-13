// Small standalone C binary, kinda like xprintidle, but prints time or waits
//   until "dpms off" (in seconds) or 0 if display is already disabled by it.
// Build with: gcc -O2 -lX11 -lXss -lXext -Wall xdpms.c -o xdpms && strip xdpms
// Usage info: ./xdpms -h
// More info on dpms settings: xset q

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h>

#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>
#include <X11/extensions/scrnsaver.h>

int main(int argc, char *argv[]) {
	int mode_print = argc == 1;
	int mode_check = argc == 2 && !strcmp(argv[1], "check");
	int mode_wait = argc == 2 && !strcmp(argv[1], "wait");

	if (!(mode_print || mode_check || mode_wait)) {
		printf("Usage: %s [-h/--help] [ check | wait ]\n", argv[0]);
		printf(
			"\nWithout arguments:\n"
			"  Prints seconds from now until dpms-off is supposed to happen to stdout.\n"
			"  '0' means that monitor is already off.\n"
			"  Dash ('-') is printed if there's no such timeout, e.g. dpms-off is disabled.\n"
			"  Does not print anything to stdout and exits with error in case of any issues.\n"
			"\ncheck - exit with 0 if seconds to dpms-off is >0, 1 otherwise.\n"
			"\nwait - wait-until-idle mode:\n"
			"  Sleeps until system is idle, making checks proportional to dpms timeouts.\n"
			"  Exits with status=0 upon detecting dpms-off state.\n"
			"  Intended use is like a 'sleep' command to delay until desktop idleness.\n"
			"  Will exit with error if dpms-off delay is <1min (assuming disabled).\n" );
		return 1; }

	char *err = NULL; int ret = 0;
	Display *dpy = NULL;
	XScreenSaverInfo *ssi = NULL;
	int dummy;

	if (!(dpy = XOpenDisplay(NULL)))
		{ err = "failed to open display"; goto cleanup; }
	if (!XScreenSaverQueryExtension(dpy, &dummy, &dummy))
		{ err = "screen saver extension not supported"; goto cleanup; }
	if (!(ssi = XScreenSaverAllocInfo()))
		{ err = "couldn't allocate screen saver info"; goto cleanup; }
	if (!XScreenSaverQueryInfo(dpy, DefaultRootWindow(dpy), ssi))
		{ err = "couldn't query screen saver info"; goto cleanup; }

	CARD16 state, delay_standby, delay_suspend, delay_off;
	BOOL dpms_enabled;
	long seconds = -1; unsigned int timeout;

	while (1) {
		if (DPMSQueryExtension(dpy, &dummy, &dummy) && DPMSCapable(dpy)) {
			DPMSGetTimeouts(dpy, &delay_standby, &delay_suspend, &delay_off);
			DPMSInfo(dpy, &state, &dpms_enabled);
			if (dpms_enabled)
				seconds = state == DPMSModeOff ? 0 :
					delay_off > 0 ? delay_off - ssi->idle / 1000 : -1; }

		if (mode_print) {
			if (seconds >= 0) printf("%ld\n", seconds);
			else printf("-\n");
			break; }

		if (mode_check) {
			if (seconds < 0) ret = 1;
			break; }

		if (mode_wait) {
			if (seconds < 0) break;
			if (delay_off < 60)
				{ err = "dpms-off delay is <1min"; goto cleanup; }
			timeout = MIN(seconds, delay_off / 2) + 3;
			while (timeout > 1) timeout = sleep(timeout); }
	}

	cleanup:
	if (ssi) XFree(ssi);
	if (dpy) XCloseDisplay(dpy);
	if (err) { fprintf(stderr, "ERROR: %s\n", err); return 1; }
	return ret;
}
