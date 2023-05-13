// Small standalone C binary, which waits until any xinput events (mouse/kb) and exits.
//   Main purpose is to run it as a "wait until user activity" sleep-tool.
// Build with: gcc -O2 -lX11 -lXi -Wall xiwait.c -o xiwait && strip xiwait
// Usage info: ./xiwait -h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

int main(int argc, char *argv[]) {
	if (argc != 1) {
		printf("Usage: %s [-h/--help]\n", argv[0]);
		printf("Wait until any xinput events (user kb/mouse) and exit.\n");
		return 1; }

	char *err = NULL;
	Display *dpy = NULL;
	int res, xv1 = 2, xv2 = 0;
	if (!(dpy = XOpenDisplay(NULL)))
		{ err = "failed to open display"; goto cleanup; }
	if (!XQueryExtension(dpy, "XInputExtension", &res, &res, &res))
		{ err = "XInput ext unavailable"; goto cleanup; }
	res = XIQueryVersion(dpy, &xv1, &xv2);
	if (res != Success) { err = "XIQueryVersion failed"; goto cleanup; }

	XIEventMask masks[1];
	unsigned char mask[(XI_LASTEVENT + 7)/8];

	memset(mask, 0, sizeof(mask));
	XISetMask(mask, XI_RawMotion);
	XISetMask(mask, XI_RawButtonPress);
	XISetMask(mask, XI_RawKeyPress);

	masks[0].deviceid = XIAllMasterDevices;
	masks[0].mask_len = sizeof(mask);
	masks[0].mask = mask;

	XISelectEvents(dpy, DefaultRootWindow(dpy), masks, 1);
	XFlush(dpy);

	XEvent ev;
	XNextEvent(dpy, &ev);

	cleanup:
	if (dpy) XCloseDisplay(dpy);
	if (err) { fprintf(stderr, "ERROR: %s\n", err); return 1; }
	return 0;
}
