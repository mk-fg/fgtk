// Small standalone C binary based on xclip code to grab
//   primary X11 selection text (utf-8) from terminal (or whatever else)
//   and re-host it as primary/clipboard after some (optional) processing.
// Build with: gcc -O2 -lX11 -lXmu -Wall exclip.c -Wall -o exclip && strip exclip
// More info: ./exclip -h
// More info on X11 selections:
//   https://www.uninformativ.de/blog/postings/2017-04-02/0/POSTING-en.html

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/time.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xmu/Atoms.h>

#include <getopt.h>


#define P(err, fmt, arg...) do {\
	fprintf(stderr, "ERROR: " fmt "\n", ##arg);\
	if (err) exit(err); } while (0)



// xcout/xcin and helpers from xclib.c (xclip), uglified

static size_t mach_itemsize(int format) {
	if (format == 8) return sizeof(char);
	if (format == 16) return sizeof(short);
	if (format == 32) return sizeof(long);
	return 0;
}
void *xcmalloc(size_t size) {
	void *mem;
	if ((mem = malloc(size)) == NULL)
		P(1, "malloc(%ld) failed", size);
	return mem;
}
void *xcrealloc(void *ptr, size_t size) {
	void *mem;
	if ((mem = realloc(ptr, size)) == NULL)
		P(1, "realloc(%ld) failed", size);
	return mem;
}


#define XCLIB_XCOUT_NONE 0
#define XCLIB_XCOUT_SENTCONVSEL 1
#define XCLIB_XCOUT_INCR 2
#define XCLIB_XCOUT_BAD_TARGET 3

int xcout(
		Display *dpy, Window win, XEvent evt,
		Atom sel, Atom target, Atom * type,
		char **txt, unsigned long *len, unsigned int *context ) {
	static Atom pty;
	static Atom inc;
	int pty_format;
	unsigned char *buffer;
	unsigned long pty_size, pty_items, pty_machsize;
	unsigned char *ltxt = (unsigned char *) txt;
	if (!pty) pty = XInternAtom(dpy, "XCLIP_OUT", False);
	if (!inc) inc = XInternAtom(dpy, "INCR", False);

	switch (*context) {
		case XCLIB_XCOUT_NONE:
			if (*len > 0) { free(*txt); *len = 0; }
			XConvertSelection(dpy, sel, target, pty, win, CurrentTime);
			*context = XCLIB_XCOUT_SENTCONVSEL;
			return 0;

		case XCLIB_XCOUT_SENTCONVSEL:
			if (evt.type != SelectionNotify) return 0;

			if (evt.xselection.property == None) {
				*context = XCLIB_XCOUT_BAD_TARGET;
				return 0; }

			XGetWindowProperty( dpy, win, pty, 0, 0, False,
				AnyPropertyType, type, &pty_format, &pty_items, &pty_size, &buffer );
			XFree(buffer);
			if (*type == inc) {
				XDeleteProperty(dpy, win, pty);
				XFlush(dpy);
				*context = XCLIB_XCOUT_INCR;
				return 0; }
			XGetWindowProperty( dpy, win, pty, 0, (long) pty_size, False,
				AnyPropertyType, type, &pty_format, &pty_items, &pty_size, &buffer );
			XDeleteProperty(dpy, win, pty);

			pty_machsize = pty_items * mach_itemsize(pty_format);
			ltxt = (unsigned char *) xcmalloc(pty_machsize);
			memcpy(ltxt, buffer, pty_machsize);
			*len = pty_machsize;
			*txt = (char *) ltxt;
			XFree(buffer);

			*context = XCLIB_XCOUT_NONE;
			return 1; // yay!

		case XCLIB_XCOUT_INCR:
			if (evt.type != PropertyNotify) return 0;
			if (evt.xproperty.state != PropertyNewValue) return 0;

			XGetWindowProperty( dpy, win, pty, 0, 0, False, AnyPropertyType,
				type, &pty_format, &pty_items, &pty_size, (unsigned char **) &buffer );
			if (pty_size == 0) {
				XFree(buffer);
				XDeleteProperty(dpy, win, pty);
				*context = XCLIB_XCOUT_NONE;
				return 1; }
			XFree(buffer);
			XGetWindowProperty(
				dpy, win, pty, 0, (long) pty_size, False, AnyPropertyType,
				type, &pty_format, &pty_items, &pty_size, (unsigned char **) &buffer );

			pty_machsize = pty_items * mach_itemsize(pty_format);
			if (*len == 0) {
				*len = pty_machsize;
				ltxt = (unsigned char *) xcmalloc(*len);
			} else {
				*len += pty_machsize;
				ltxt = (unsigned char *) xcrealloc(ltxt, *len); }
			memcpy(&ltxt[*len - pty_machsize], buffer, pty_machsize);
			*txt = (char *) ltxt;
			XFree(buffer);

			XDeleteProperty(dpy, win, pty);
			XFlush(dpy);
			return 0;
	} // big switch
	return 0;
}


#define XCLIB_XCIN_NONE 0
#define XCLIB_XCIN_SELREQ 1
#define XCLIB_XCIN_INCR 2

int xcin(
		Display *dpy, Window *cwin, XEvent evt,
		Atom *pty, Atom target, unsigned char *txt,
		unsigned long len, unsigned long *pos, unsigned int *context ) {
	unsigned long chunk_len;
	XEvent res;
	static Atom inc;
	static Atom targets;
	static long chunk_size;

	target = XA_UTF8_STRING(dpy);

	if (!targets) targets = XInternAtom(dpy, "TARGETS", False);
	if (!inc) inc = XInternAtom(dpy, "INCR", False);

	if (!chunk_size) {
		chunk_size = XExtendedMaxRequestSize(dpy) / 4;
		if (!chunk_size) chunk_size = XMaxRequestSize(dpy) / 4;
	}

	switch (*context) {
		case XCLIB_XCIN_NONE:
			if (evt.type != SelectionRequest) return 0;

			*cwin = evt.xselectionrequest.requestor;
			*pty = evt.xselectionrequest.property;
			*pos = 0;

			if (evt.xselectionrequest.target == targets) {
				Atom types[2] = { targets, target };
				XChangeProperty(
					dpy, *cwin, *pty, XA_ATOM, 32, PropModeReplace,
					(unsigned char *) types, (int) (sizeof(types) / sizeof(Atom)) );
			} else if (len > chunk_size) {
				XChangeProperty(dpy, *cwin, *pty, inc, 32, PropModeReplace, 0, 0);
				XSelectInput(dpy, *cwin, PropertyChangeMask);
				*context = XCLIB_XCIN_INCR;
			} else
				XChangeProperty( dpy, *cwin, *pty, target,
					8, PropModeReplace, (unsigned char *) txt, (int) len );
			res.xselection.property = *pty;
			res.xselection.type = SelectionNotify;
			res.xselection.display = evt.xselectionrequest.display;
			res.xselection.requestor = *cwin;
			res.xselection.selection = evt.xselectionrequest.selection;
			res.xselection.target = evt.xselectionrequest.target;
			res.xselection.time = evt.xselectionrequest.time;
			XSendEvent(dpy, evt.xselectionrequest.requestor, 0, 0, &res);
			XFlush(dpy);

			if (evt.xselectionrequest.target == targets) return 0;
			return len > chunk_size ? 0 : 1;

		case XCLIB_XCIN_INCR:
			if (evt.type != PropertyNotify) return 0;
			if (evt.xproperty.state != PropertyDelete) return 0;

			chunk_len = chunk_size;
			if ((*pos + chunk_len) > len) chunk_len = len - *pos;
			if (*pos > len) chunk_len = 0;

			if (chunk_len)
				XChangeProperty( dpy, *cwin, *pty, target,
					8, PropModeReplace, &txt[*pos], (int) chunk_len );
			else XChangeProperty(dpy, *cwin, *pty, target, 8, PropModeReplace, 0, 0);
			XFlush(dpy);

			if (!chunk_len) *context = XCLIB_XCIN_NONE;
			*pos += chunk_size;

			return chunk_len > 0 ? 0 : 1;
	} // big switch
	return 0;
}



// X display/window have to be initialized in each
//   subprocess, which will be holding selection buffers.

char *dpy_name = NULL;
Display *dpy;
Window win;

void dpy_init() {
	if (!(dpy = XOpenDisplay(dpy_name)))
		P(1, "failed to open display: %s", dpy_name ? dpy_name : "[default]");
	win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
	XSelectInput(dpy, win, PropertyChangeMask);
}
void dpy_close() {
	XCloseDisplay(dpy);
}


static int read_selection( char **buff,
		unsigned long *buff_len, int sel_primary ) {
	dpy_init();

	XEvent evt;
	unsigned int context = XCLIB_XCOUT_NONE;
	Atom sel_src = sel_primary ? XA_PRIMARY : XA_CLIPBOARD(dpy);
	Atom sel_type = None;
	Atom target = XA_UTF8_STRING(dpy);
	int err = 0;
	char *xcbuff = NULL;

	*buff_len = 0;
	while (1) {
		if (context != XCLIB_XCOUT_NONE) XNextEvent(dpy, &evt);
		xcout( dpy, win, evt,
			sel_src, target, &sel_type, &xcbuff, buff_len, &context );

		if (context == XCLIB_XCOUT_BAD_TARGET) {
			if (target == XA_UTF8_STRING(dpy)) {
				context = XCLIB_XCOUT_NONE;
				target = XA_STRING; // fallback
				continue;
			} else {
				char *atom_name = XGetAtomName(dpy, target);
				P(0, "target %s not available", atom_name);
				XFree(atom_name);
				err = 1;
				break; } } // fail
		if (context == XCLIB_XCOUT_NONE) break;
	}

	dpy_close();

	// Next XOpenDisplay() will try to free() xcbuff, not sure why
	*buff = xcmalloc(*buff_len+1);
	memcpy(*buff, xcbuff, *buff_len);
	*(*buff + *buff_len) = '\0';
	free(xcbuff);

	return err;
}

void update_selection(
		char *buff, unsigned long buff_len, int sel_primary, float timeout ) {
	pid_t pid;
	pid = fork(); // child will own and hold selection buffer
	if (pid) return; // parent

	dpy_init();

	XEvent evt;
	int dloop = 0, sloop = 0;
	Atom target = XA_UTF8_STRING(dpy);
	Atom sel_dst = sel_primary ? XA_PRIMARY : XA_CLIPBOARD(dpy);
	XSetSelectionOwner(dpy, sel_dst, win, CurrentTime);

	if (timeout > 0) {
		struct itimerval interval;
		interval.it_value.tv_sec = (int) timeout;
		interval.it_value.tv_usec = (long) (timeout * 1000000.) % 1000000;
		interval.it_interval = interval.it_value;
		signal(SIGALRM, exit);
		if (setitimer(ITIMER_REAL, &interval, NULL) < 0)
			P(1, "setitimer(%.2f) failed", timeout); }

	while (dloop < sloop || sloop < 1) {
		while (1) {
			static unsigned int clear = 0;
			static unsigned int context = XCLIB_XCIN_NONE;
			static unsigned long sel_pos = 0;
			static Window cwin;
			static Atom pty;
			int finished;

			XNextEvent(dpy, &evt);
			finished = xcin( dpy, &cwin, evt,
				&pty, target, (unsigned char *) buff, buff_len, &sel_pos, &context );

			if (evt.type == SelectionClear) clear = 1;
			if ((context == XCLIB_XCIN_NONE) && clear) goto exit; // no longer needed
			if (finished) break; }
		dloop++; }

exit:
	dpy_close();
	exit(0);
}


void str_strip(char **s, unsigned long *len) {
	char *end;
	if (!*len) return;
	end = *s + *len - 1;
	while (*len && isspace(*end)) { end--; (*len)--; };
	*(end + 1) = '\0';
	while (*len && isspace(**s)) { (*s)++; (*len)--; }
}

void str_rmchar(char* s, unsigned long *len, char c) {
	char *pr = s, *pw = s; int n;
	for (n=0; n<*len; n++) { *pw = *pr++; pw += (*pw != c); }
	*pw = '\0'; *len -= (unsigned long) pr - (unsigned long) pw;
}

void str_subchar(char* s, unsigned long len, char c0, char c1) {
	int n;
	for (n=0; n<len; n++)
		if (*(s + n) == c0) *(s + n) = c1;
}

void str_replace(
		char **s, unsigned long *len,
		char *src, size_t len_src, char *dst, size_t len_dst ) {
	unsigned long n = 0, len_s = *len, len_diff = 0;
	char *m, *res = malloc(
		len_s * (float) strlen(dst) / (float) strlen(src) + 1 );
	while ((m = memmem(*s, len_s, src, len_src))) {
		memcpy(res + n, *s, m - *s);
		n += m - *s;
		len_s -= (m - *s) + len_src;
		*s = m + len_src;
		len_diff += len_dst - len_src;
		memcpy(res + n, dst, len_dst);
		n += len_dst; }
	strcpy(res + n, *s);
	*s = realloc(res, *len += len_diff);
}

void parse_opts( int argc, char *argv[],
		int *opt_verbatim, int *opt_slashes_to_dots, int *opt_tabs_to_spaces,
		int *opt_from_clip, int *opt_remove_prefix_byte, float *opt_timeout ) {
	extern char *optarg;
	extern int optind, opterr, optopt;

	void usage(int err) {
		FILE *dst = !err ? stdout : stderr;
		fprintf( dst,
"Usage: %s [-h|--help] [-c/--from-clip] [-x|--verbatim] [...other-opts]\n\n"
"\"Copies\" (actually forks pids"
	" to hold/own that stuff) primary X11 selection\n"
"  back to primary and clipboard, stripping start/end spaces,\n"
"  removing newlines and replacing tabs with spaces by default\n"
"  (unless -x/--verbatim is specified).\n"
"With -c/--from-clip option, clipboard selection will be used as a source instead.\n\n"
"Extra flags (can be used with(-out)"
	" -x/--verbatim to strip/keep other stuff):\n"
"  -p/--remove-prefix-byte - removes first byte from source buffer.\n"
"  -d/--slashes-to-dots - replaces all forward slashes [/] with dots [.].\n"
"  -t/--tabs-to-spaces N - replaces each tab char with N spaces.\n"
"    (default without -x/--verbatim is one space for each tab, overrides that)\n"
"  -b/--timeout S - drop selection after specified number of seconds.\n\n", argv[0] );
		exit(err); }

	int ch;
	static struct option opt_list[] = {
		{"help", no_argument, NULL, 1},
		{"verbatim", no_argument, NULL, 2},
		{"slashes-to-dots", no_argument, NULL, 3},
		{"tabs-to-spaces", required_argument, NULL, 4},
		{"from-clip", no_argument, NULL, 5},
		{"remove-prefix-byte", no_argument, NULL, 6},
		{"timeout", required_argument, NULL, 7} };
	while ((ch = getopt_long(argc, argv, ":hxdt:cpb:", opt_list, NULL)) != -1)
		switch (ch) {
			case 'x': case 2: *opt_verbatim = 1; break;
			case 'd': case 3: *opt_slashes_to_dots = 1; break;
			case 't': case 4: *opt_tabs_to_spaces = atoi(optarg); break;
			case 'c': case 5: *opt_from_clip = 1; break;
			case 'p': case 6: *opt_remove_prefix_byte = 1; break;
			case 'b': case 7: *opt_timeout = strtof(optarg, NULL); break;
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
	int opt_verbatim = 0, opt_slashes_to_dots = 0,
		opt_tabs_to_spaces = -1, opt_from_clip = 0, opt_remove_prefix_byte = 0;
	float opt_timeout = -1;
	parse_opts( argc, argv, &opt_verbatim, &opt_slashes_to_dots,
		&opt_tabs_to_spaces, &opt_from_clip, &opt_remove_prefix_byte, &opt_timeout );

	char *buff;
	unsigned long buff_len;

	if (chdir("/") == -1) P(1, "chdir(/) failed"); // for leftover child pids
	if (read_selection(&buff, &buff_len, !opt_from_clip))
		P(1, "failed to read source selection buffer");

	if (opt_tabs_to_spaces >= 0) {
		char *sub = memset(malloc(opt_tabs_to_spaces), ' ', opt_tabs_to_spaces);
		str_replace(&buff, &buff_len, "\t", 1, sub, opt_tabs_to_spaces);
		free(sub); }
	if (!opt_verbatim) {
		str_rmchar(buff, &buff_len, '\n');
		str_subchar(buff, buff_len, '\t', ' ');
		str_strip(&buff, &buff_len); }
	if (opt_slashes_to_dots) str_subchar(buff, buff_len, '/', '.');
	if (opt_remove_prefix_byte && buff_len > 0) { buff++; buff_len--; }

	update_selection(buff, buff_len, 1, opt_timeout);
	update_selection(buff, buff_len, 0, opt_timeout);

	return 0;
}
