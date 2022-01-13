// Small standalone C binary, kinda like xprintidle, but prints time
//   until "dpms off" (in seconds) or 0 if monitor is already disabled by it.
// Build with: gcc -O2 -lX11 -lXss -lXext -Wall xdpms.c -o xdpms && strip xdpms
// Usage info: ./xdpms -h
// More info on dpms settings: xset q

#include <stdio.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>
#include <X11/extensions/scrnsaver.h>

int main(int argc, char *argv[]) {
	if (argc != 1) {
		printf("Usage: %s\n\n", argv[0]);
		printf("Prints seconds from now until dpms-off is supposed to happen to stdout.\n");
		printf("'0' means that monitor is already off.\n");
		printf("Dash ('-') is printed if there's no such timeout, e.g. dpms-off is disabled.\n");
		printf("Does not print anything to stdout and exits with error in case of any issues.\n");
		return 1; }

	long seconds = -1;
	char *err = NULL;
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

	if (DPMSQueryExtension(dpy, &dummy, &dummy) && DPMSCapable(dpy)) {
		DPMSGetTimeouts(dpy, &delay_standby, &delay_suspend, &delay_off);
		DPMSInfo(dpy, &state, &dpms_enabled);
		if (dpms_enabled)
			seconds = state == DPMSModeOff ? 0 :
				delay_off > 0 ? delay_off - ssi->idle / 1000 : -1; }

	if (seconds >= 0) printf("%ld\n", seconds);
	else printf("-\n");

	cleanup:
	if (ssi) XFree(ssi);
	if (dpy) XCloseDisplay(dpy);
	if (err) { fprintf(stderr, "ERROR: %s\n", err); return 1; }
	return 0;
}
