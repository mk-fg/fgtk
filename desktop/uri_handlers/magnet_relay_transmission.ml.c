#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>

#include <caml/mlvalues.h>
#include <caml/memory.h>
#include <caml/alloc.h>
#include <caml/fail.h>


int hdr_len = sizeof(struct inotify_event);

value mlin_watch_path(value path) {
	CAMLparam1(path);
	int fd = inotify_init();
	if (fd < 0) caml_failwith("init failed");
	int wd = inotify_add_watch( fd,
		String_val(path), IN_CLOSE_WRITE | IN_MOVED_TO );
	if (wd < 0) caml_failwith("add_watch failed");
	CAMLreturn(Val_int(fd));
}

value mlin_hdr_len(void) {
	CAMLparam0();
	CAMLreturn(Val_int(hdr_len));
}

value mlin_peek(value fd) {
	CAMLparam1(fd);
	int bytes, rc = ioctl(Int_val(fd), FIONREAD, &bytes);
	if (rc == -1) caml_failwith("ioctl fionread");
	CAMLreturn(Val_int(bytes));
}

value mlin_ev_name(value buff, value buff_len, value offset) {
	CAMLparam3(buff, buff_len, offset);
	char *name = "";
	int n = Int_val(offset);
	int len_max = Int_val(buff_len) - n - hdr_len;
	if (len_max >= 0) {
		char *ev = String_val(buff);
		struct inotify_event *hdr = (struct inotify_event *) ev+n;
		if (hdr->len > 0)
			name = strndup( ev + n + hdr_len,
				hdr->len > len_max ? len_max : hdr->len ); }
	CAMLreturn(caml_copy_string(name));
}
