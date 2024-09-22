// Small standalone C binary to query/print keyboard LED state(s)
// Build with: gcc -O2 -lX11 -Wall xkbledq.c -o xkbledq && strip xkbledq
// Usage info: ./xkbledq -h

#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/XKBlib.h>

#define err_unless(chk) if (!(chk)) { err = "'" #chk "' failed"; goto cleanup; }

int main(int argc, char *argv[]) {
	if (argc > 2) {
		printf("Usage: %s [-h/--help] [led]\n", argv[0]);
		printf("Show/query named keyboard LED state(s).\n");
		printf( "When querying, returns 1 for e.g."
			" \"%s scroll\" if scroll lock LED is lit, 0 otherwise.\n", argv[0] );
		return 1; }
	char *query = "";
	if (argc == 2) query = argv[1];

	char *err = NULL;
	Display *dpy = NULL;
	int xkb_opcode, xkb_event, xkb_error;
	int xkb_lmaj = XkbMajorVersion, xkb_lmin = XkbMinorVersion;
	err_unless(dpy = XOpenDisplay(NULL));
	err_unless(XkbLibraryVersion(&xkb_lmaj, &xkb_lmin));
	err_unless(XkbQueryExtension(dpy, &xkb_opcode, &xkb_event, &xkb_error, &xkb_lmaj, &xkb_lmin));

	char atoms[][12] = {"Caps Lock", "Num Lock", "Scroll Lock"};
	char leds[][7] = {"caps", "num", "scroll"};

	Bool st;
	for (size_t n = 0; n <= 2; n++) {
		XkbGetNamedIndicator(dpy, XInternAtom(dpy, atoms[n], False), NULL, &st, NULL, NULL);
		if (!st) continue;
		if (strlen(query)) { if (!strcmp(query, leds[n])) return 43; }
		else puts(leds[n]); }

	cleanup:
	if (dpy) XCloseDisplay(dpy);
	if (err) { fprintf(stderr, "ERROR: %s\n", err); return 1; }
	return 0;
}
