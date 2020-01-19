(* Command-line tool to hash strings/data to words from cached aspell dictionary.
 *
 * Build with:
 *   % ocamlopt -o hhash -O2 unix.cmxa str.cmxa -cclib -lsodium hhash.ml hhash.ml.c
 *   % strip hhash
 *
 * Usage:
 *   % ./hhash some-fingerprint other-fp-string
 *   % ./hhash -e <<< file-contents
 *)

let cli_dict_cmd = ref "/usr/bin/aspell -d en dump master"
let cli_strip_re = ref "'s$"
let cli_cache_file = ref "~/.cache/hhash.dict"
let cli_entropy_est = ref false
let cli_strings = ref []
let cli_word_count = ref 5
let cli_dict_words_max = ref (int_of_float ((2. ** 30.) /. 10.)) (* ~1 GiB of ~10-char words *)

exception HHash_fail of string


(* Command-line args processing *)
let () =
	Arg.parse
		[ ("-w", Arg.Set_int cli_word_count,
				"-- Output word count. Default: " ^ (string_of_int !cli_word_count));
			("-d", Arg.Set_string cli_dict_cmd,
				"-- Dict-dump-command to run to get list of hash-component words on stdout.\n" ^
				"        Command will be split on spaces. Default: " ^ !cli_dict_cmd ^ "\n" ^
				"        Output words are split on spaces/newlines, will be sorted, with duplicates removed.\n");
			("-s", Arg.Set_string cli_strip_re,
				"-- Strip specified regexp-match(es) from each input word. Default: " ^ !cli_strip_re);
			("-c", Arg.Set_string cli_cache_file,
				"-- File to store aspell cache into. Should be persistent for consistent outputs.\n" ^
				"        Can be empty to re-run dict-dump cmd every time. Default: " ^ !cli_cache_file);
			("-e", Arg.Set cli_entropy_est,
				"-- Print entropy estimate for resulting hash value.") ]
		(fun arg -> cli_strings :=  arg :: !cli_strings)
		("Usage: " ^ Sys.argv.(0) ^ " [opts] [string ...]\
			\n\nOutput word-hashes for each specified string(s) (same order, one per line), or use stdin if none are specified.\
			\nSame idea as in e.g. base32 or base64, but with \"alphabet\" being word-dictionary.\"
			\nThis is NOT cryptographic hash (wrt entropy, dsitribution, etc), and should not be used as such.\n")


(* Build word_arr alphabet *)
let word_count, word_bits, word_arr =
	let cache_file = if Str.string_match (Str.regexp "^~/\\(.*\\)$") !cli_cache_file 0
		then Sys.getenv "HOME" ^ "/" ^ (Str.matched_group 1 !cli_cache_file) else !cli_cache_file in

	(* Lookup binary in PATH for Unix.open_process_args_in, if necessary *)
	let dict_cmd = (String.split_on_char ' ' (String.trim !cli_dict_cmd)) in
	let dict_cmd_bin = List.hd dict_cmd in
	let dict_cmd = if dict_cmd_bin.[0] = '/' then dict_cmd else
		let rec find_in_path bin path =
			if (List.length path) = 0
				then raise (Sys_error (Printf.sprintf "Failed to find binary in PATH: %s" dict_cmd_bin))
				else let bin_abs = Filename.concat (List.hd path) bin in
					if bin_abs.[0] = '/' && Sys.file_exists bin_abs
						then bin_abs else find_in_path bin (List.tl path) in
		(find_in_path dict_cmd_bin (String.split_on_char ':' (Sys.getenv "PATH"))) :: (List.tl dict_cmd) in

	(* Open cache-file or command output *)
	(* src_is_proc is used for closing and checked for whether to create cache-file later *)
	let src, src_is_proc =
		try (open_in cache_file, false)
		with Sys_error err ->
			if Str.string_match (Str.regexp "^.*: No such file or directory$") err 0
			then (Unix.open_process_args_in (List.hd dict_cmd) (Array.of_list dict_cmd), true)
			else raise (Sys_error err) in

	(* Read words from command output or cache-file, sort/dedup *)
	let words =
		List.sort_uniq (fun a b -> if a == b then 0 else if a < b then -1 else 1)
		(let strip_re = Str.regexp !cli_strip_re in
			let rec read_lines list n = if n <= 0
				then raise (HHash_fail (Printf.sprintf "Too many input words (max=%d)" !cli_dict_words_max))
				else
					let list =
						let words = String.split_on_char ' ' (input_line src) in
						(List.map (fun w -> Str.global_replace
							strip_re "" (String.lowercase_ascii w)) words) @ list in
					try read_lines list (n - 1)
					with End_of_file -> if not src_is_proc then (close_in src; list) else
						match Unix.close_process_in src with
							Unix.WEXITED code -> if code = 0 then list else
								raise (HHash_fail (Printf.sprintf "Input subprocess failed (exit-code=%d)" code))
							| Unix.WSIGNALED code | Unix.WSTOPPED code ->
								raise (HHash_fail (Printf.sprintf "Input subprocess killed (signal=%d)" code)) in
			read_lines [] !cli_dict_words_max) in

	(* Write resulting words to cache-file *)
	let () = if not src_is_proc then () else
		let cache = open_out cache_file in
		List.iter (fun w -> output_string cache (w ^ "\n")) words;
		close_out cache in

	(* Pad words from the beginning to binary-power size, if it's almost there, truncate otherwise *)
	(* This potentially makes words non-unique *)
	let n = List.length words in
	let n_bits = (log (float_of_int n)) /. (log 2.) in
	let words =
		let n_floor = 2. ** (floor n_bits) in
		let n_rem = (float_of_int n) -. n_floor in
		let rec sublist a b list =
			match list with [] -> failwith "sublist" | e :: tail ->
				let tail = if b <= 1 then [] else sublist (a-1) (b-1) tail in
				if a > 0 then tail else e :: tail in
		if (n_rem /. n_floor) < 0.7
			then sublist 0 (int_of_float n_floor) words
			else words @ (sublist 0 (int_of_float (n_floor -. n_rem)) words) in

	let n_bits = if (List.length words) < n then (floor n_bits) else n_bits in
	(n, n_bits, (Array.of_list words))


let read_byte_iter_func s =
	let n = ref 0 in let n_max = Bytes.length s in
	(fun () -> if !n < n_max
		then (n := !n + 1; Bytes.get s (!n-1)) else raise End_of_file)

let hash_to_words read_byte =
	let n_bits = int_of_float (* n_bits here will be int with padded array *)
		((log (float_of_int (Array.length word_arr))) /. (log 2.)) in
	let rec read_input hash n bits =
		let b =
			try int_of_char (read_byte ())
			with End_of_file -> -1 in
		if b < 0 then
			if bits = 0 || bits = n_bits then hash else (n :: hash)
		else
			let shift = min 8 bits in (* n of bits from b to current n *)
			let rem = 8 - shift in
			(* Printf.printf "input[%d] = %x [n=%x bits=%d shift=%d rem=%d]\n"
			 * 	(List.length hash) b n bits shift rem; *)
			let n = (Int.shift_left n shift) lor (Int.shift_right b rem) in (* extend n by "shift" bits *)
			let b = b land (int_of_float (2. ** (float_of_int rem)) - 1) in (* remaining unused bits *)
			let bits = bits - shift in
			if bits = 0
				then read_input (n :: hash) b (n_bits - rem)
				else read_input hash n bits in
	let hash = List.tl (read_input [] 0 n_bits) in (* always drop final word *)
	List.map (fun n -> Array.get word_arr n) hash

let hash_str read_byte = String.concat " " (hash_to_words read_byte)
let hash_print read_byte = Printf.printf "%s\n%!" (hash_str read_byte)


external hash_raw : string -> int -> bytes = "mls_hash_string"
external hash_raw_stdin : int -> bytes = "mls_hash_stdin"

let () =
	let n = float_of_int (Array.length word_arr) in
	let n_bits = int_of_float ((log n) /. (log 2.)) in
	let hash_bits = !cli_word_count * n_bits in
	(* hash_len gets +1 because last incomplete word is always dropped *)
	let hash_len = (int_of_float (floor ((float_of_int hash_bits) /. 8.))) + 1 in
	if (List.length !cli_strings) > 0
		then List.iter (fun s ->
			let hash = hash_raw s hash_len in
			hash_print (read_byte_iter_func hash)) !cli_strings
		else hash_print (read_byte_iter_func (hash_raw_stdin hash_len))

let () =
	if not !cli_entropy_est then () else Printf.printf
		"entropy-stats: word-count=%d dict-words=%d word-bits=%.1f total-bits=%.1f\n"
		!cli_word_count word_count word_bits ((float_of_int !cli_word_count) *. word_bits)
