(* Simple app to watch directory for new files,
 *   read magnet links from them and run "transmission-remote -a" on these.
 *
 * Build with:
 *   % ocamlopt -o transmission-magnet -O2 unix.cmxa str.cmxa \
 *       magnet_relay_transmission.ml magnet_relay_transmission.ml.c
 *   % strip transmission-magnet
 *
 * Usage:
 *   % ./transmission-magnet -h
 *   % ./transmission-magnet -d -c './test.sh -a --'
 *)

let cli_debug = ref false
let cli_path_specified = ref false
let cli_path = ref "."
let cli_path_suffix = ref ".magnet"
let cli_cmd = ref "transmission-remote -a"
let cli_cmd_max = ref 3
let cli_link_len_max = ref (20 * int_of_float (2. ** 10.))


(* Command-line args processing *)
let () =
	let debug_desc = "-- Verbose operation mode." in
	Arg.parse
		[ ("-c", Arg.Set_string cli_cmd,
				"-- Command to run with magnet link as a last argument.\n" ^
				"        Will be split on spaces. Default: " ^ !cli_cmd ^ "\n" ^
				"        Note: never specify passwords on command-line (visible to all pids).");
			("-s", Arg.Set_string cli_path_suffix,
				"-- Suffix for files to be processed." ^
					" Can be empty to process all.\n" ^
				"        Case-sensitive. Default: " ^ !cli_path_suffix);
			("-d", Arg.Set cli_debug, "     " ^ debug_desc);
			("--debug", Arg.Set cli_debug, debug_desc) ]
		(fun arg ->
			if !cli_path_specified
				then raise (Arg.Bad ("Bogus extra arg : " ^ arg))
				else (cli_path := arg; cli_path_specified := true))
		("Usage: " ^ Sys.argv.(0) ^ " [-d|--debug] [opts] [path]\
			\n\nWatch path for new .magnet files and run transmission-remote with link from each one.\
			\nUses current directory if path is not specified.\n")

let debug_print line =
	if !cli_debug then print_endline line; flush stdout


(* Simple inotify bindings from magnet_relay_transmission_glue.c *)

external in_watch_path : string -> Unix.file_descr = "mlin_watch_path"
external in_hdr_len : unit -> int = "mlin_hdr_len"
external in_peek : Unix.file_descr -> int = "mlin_peek"
external in_ev_name : bytes -> int -> int -> bytes = "mlin_ev_name"

let in_read_paths =
	(* Note: buffer doesn't persist between reads, assuming that events are read all-or-nothing *)
	let hdr_len = in_hdr_len () in
	(fun fd ->
		let buff_len = in_peek fd in
		let buff = Bytes.make buff_len '\000' in
		let buff_len = Unix.read fd buff 0 buff_len in
		let rec parse_ev n path_list =
			let m = n + hdr_len in
			if m >= buff_len then path_list else
				let ev_name = in_ev_name buff buff_len n in
				parse_ev (m + Bytes.length ev_name) (ev_name :: path_list) in
		List.rev (parse_ev 0 []))


let watch_path () =
	let try_finally f x finally y =
		let res = try f x with e -> finally y; raise e in finally y; res in

	let re_link = Str.regexp "[^ \000\012\n\r\t]+" in
	let re_path = Str.regexp ((Str.quote !cli_path_suffix) ^ "$") in

	let path_queue = ref [] in
	let fd = in_watch_path !cli_path in
	Unix.set_close_on_exec(fd);


	let read_buff = Bytes.make !cli_link_len_max ' ' in
	let read_link path =
		let fd = ref Unix.stdin in
		let link =
			try
				fd := Unix.openfile path [Unix.O_RDONLY] 0o644;
				let buff_len = min !cli_link_len_max (Unix.lseek !fd 0 Unix.SEEK_END) in
				ignore (Unix.lseek !fd 0 Unix.SEEK_SET);
				let buff_len = Unix.read !fd read_buff 0 buff_len in
				Bytes.sub_string read_buff 0 buff_len
			with Unix.Unix_error _ -> "" in
		if !fd = Unix.stdin then () else Unix.close !fd;
		if Str.string_match re_link link 0
			then Str.matched_string link else "" in


	let cmd = List.filter
		(fun v -> String.length v > 0)
		(String.split_on_char ' ' (String.trim !cli_cmd)) in
	let cmd_check_needed = ref false in
	let cmd_pids = ref [] in
	let cmd_pipe = Unix.openfile "/dev/null" [Unix.O_RDWR;Unix.O_CLOEXEC] 0o666 in

	let cmd_spawn () =
		if (List.length !cmd_pids) >= !cli_cmd_max || List.length !path_queue = 0 then () else
		let path = List.hd !path_queue in path_queue := List.tl !path_queue;
		let path = Filename.concat !cli_path path in
		let path_link = read_link path in
		debug_print (Printf.sprintf "--- link: %s" path_link);
		if (String.length path_link) = 0 then () else
			let cmd = cmd @ [path_link] in
			let cmd_func = Unix.create_process (List.hd cmd) (Array.of_list cmd) in
			cmd_pids := (if !cli_debug
				then cmd_func Unix.stdin Unix.stdout Unix.stderr
				else cmd_func cmd_pipe cmd_pipe cmd_pipe) :: !cmd_pids;
			debug_print (Printf.sprintf
				"--- - new-pid=%d [path-q=%d cmd-q=%d]: %s"
				(List.hd !cmd_pids) (List.length !path_queue)
				(List.length !cmd_pids) path); in
	(* XXX: cleanup link-files if command exits with 0 *)
	(* XXX: log non-clean pid exits to stderr for systemd *)

	let rec cmd_check () =
		cmd_check_needed := false;
		cmd_pids := List.filter
			(fun pid ->
				let pid, status =
					try Unix.waitpid [Unix.WNOHANG] pid
					with Unix.Unix_error (Unix.ECHILD, _, _) -> (1, Unix.WEXITED 0)
				in pid == 0)
			!cmd_pids;
		cmd_spawn ();
		if !cmd_check_needed then cmd_check () else
			if (List.length !path_queue) = 0 && (List.length !cmd_pids) = 0
				then debug_print (Printf.sprintf "--- idle") else () in

	(* Signal handlers are run synchronously from same loop below *)
	Sys.set_signal Sys.sigchld
		(Sys.Signal_handle (fun sig_n -> cmd_check_needed := true));


	let watch_loop () =
		let rec select () =
			try Unix.select [fd] [] [fd] (-1.)
			with Unix.Unix_error (Unix.EINTR, _, _) ->
				if !cmd_check_needed then cmd_check () else ();
				select () in
		let rec ev_process () =
			let r, w, x = select () in
			if List.length x == 0 && List.length r >= 1 then
				let path_list = in_read_paths fd in
				List.iter
					(fun path ->
						let path = Bytes.to_string path in
						let path_match =
							try ignore (Str.search_forward re_path path 0); true
							with Not_found -> false in
						if path_match then (
							debug_print (Printf.sprintf "--- file: %s" path);
							path_queue := !path_queue @ [path]
						) else debug_print (Printf.sprintf "--- file [skip]: %s" path);
						cmd_check ())
					path_list;
				 ev_process () in
		ev_process () in

	let watch_close () = Unix.close cmd_pipe; Unix.close fd in

	try_finally watch_loop () watch_close ()


let () =
	let sig_done = Sys.Signal_handle (fun sig_n -> exit 0) in
		Sys.set_signal Sys.sigterm sig_done;
		Sys.set_signal Sys.sigint sig_done;
	Unix.handle_unix_error watch_path ()
