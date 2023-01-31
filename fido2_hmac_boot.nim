#? replace(sub = "\t", by = "  ")
# Small binary to be compiled with fido2 assertion parameters, to prompt for PIN
#   on /dev/console and produce secret for something on early boot, like cryptsetup/dm-crypt.
#
# Build with: nim c -w=on --hints=on -o=fhb -r fido2_hmac_boot.nim -h && strip fhb
# Usage info: ./fhb -h
# Intended to complement libfido2 cli tools, like fido2-token and fido2-cred.

import strformat, os, strutils, parseopt, base64, posix


{.passl: "-lfido2 -lcrypto"}

const FHB_RPID {.strdefine.} = "" # Relying Party ID string, e.g. fhd.mysite.com
const FHB_Salt {.strdefine.} = "" # 32B HMAC salt base64 that corresponds to same unique output
const FHB_Dev {.strdefine.} = "" # default device, e.g. "/dev/yubikey" or "pcsc://slot0"
const FHB_CID {.strdefine.} = "" # Credential ID base64 blob from fido2-cred
const FHB_Ask_Cmd {.strdefine.} = "systemd-ask-password" # cmd + args to return pin on stdout
const FHB_Ask_Attempts {.intdefine.} = 3 # number of pin-entry attempts
const FHB_Ask_Bypass {.intdefine.} = "e end s skip x" # space-separated words to skip pin entry
const FHB_Timeout {.intdefine.} = 30 # timeout for user presence check (touch)
const FHB_UP {.booldefine.}: int = -1 # user presence check (touch), up to device by default
const FHB_UV {.booldefine.}: int = -1 # user verification via PIN, up to device by default
const client_data = "fido2-hmac-boot.client-data.0001" # challenge value is not used here


let
	FIDO_OK {.importc, nodecl.}: cint
	FIDO_DEBUG {.importc, nodecl.}: cint
	FIDO_EXT_HMAC_SECRET {.importc, nodecl.}: cint

type
	FidoError = object of CatchableError
	Assert {.importc: "fido_assert_t*", final.} = distinct pointer
	Dev {.importc: "fido_dev_t*", final.} = distinct pointer

proc fido_strerr(err: cint): cstring {.importc, header: "<fido.h>".}
proc fido_init(flags: cint) {.importc, header: "<fido.h>".}

proc fido_assert_new: Assert {.importc, header: "<fido.h>".}
proc fido_assert_set_clientdata(a: Assert, cdata: cstring, cdata_len: cint): cint {.importc, header: "<fido.h>".}
proc fido_assert_set_rp(a: Assert, rp_id: cstring): cint {.importc, header: "<fido.h>".}
proc fido_assert_set_extensions(a: Assert, flags: cint): cint {.importc, header: "<fido.h>".}
proc fido_assert_set_up(a: Assert, up: cint): cint {.importc, header: "<fido.h>".}
proc fido_assert_set_uv(a: Assert, uv: cint): cint {.importc, header: "<fido.h>".}
proc fido_assert_set_hmac_salt(a: Assert, salt: cstring, salt_len: cint): cint {.importc, header: "<fido.h>".}
proc fido_assert_allow_cred(a: Assert, cred: cstring, cred_len: cint): cint {.importc, header: "<fido.h>".}
proc fido_assert_count(a: Assert): cint {.importc, header: "<fido.h>".}
proc fido_assert_hmac_secret_len(a: Assert, n: cint): cint {.importc, header: "<fido.h>".}
proc fido_assert_hmac_secret_ptr(a: Assert, n: cint): cstring {.importc, header: "<fido.h>".}
proc fido_assert_free(a: ptr Assert) {.importc, header: "<fido.h>".}

proc fido_dev_new: Dev {.importc, header: "<fido.h>".}
proc fido_dev_cancel(d: Dev) {.importc, header: "<fido.h>".}
proc fido_dev_set_timeout(d: Dev, timeout_ms: cint): cint {.importc, header: "<fido.h>".}
proc fido_dev_open(d: Dev, spec: cstring): cint {.importc, header: "<fido.h>".}
proc fido_dev_get_assert(d: Dev, a: Assert, pin: cstring): cint {.importc, header: "<fido.h>".}
proc fido_dev_close(d: Dev): cint {.importc, header: "<fido.h>".}
proc fido_dev_free(d: ptr Dev) {.importc, header: "<fido.h>".}

template fido(call: untyped, args: varargs[untyped]) =
	r = `fido call`(args)
	if r != FIDO_OK: raise newException(
		FidoError, "fido_" & astToStr(call) & " call failed = " & $fido_strerr(r) )


type EVP_MD = distinct pointer
proc EVP_sha256: EVP_MD {.importc, header: "<openssl/evp.h>".}
proc HMAC( md: EVP_MD, key: cstring, key_len: cint, data: cstring, data_len: cint,
	digest: cstring, digest_len: ptr cint ): cstring {.importc, header: "<openssl/hmac.h>".}

type HMACError = object of CatchableError

proc hmac_sha256(key: string, data: string): string =
	var
		hmac = newString(32)
		hmac_len: cint
		res = HMAC( EVP_sha256(), key.cstring, key.len.cint,
			data.cstring, data.len.cint, hmac.cstring, hmac_len.addr )
	if hmac_len != 32'i32 or res != hmac.cstring:
		raise newException(HMACError, "HMAC call failed")
	return hmac


proc p_err(s: string) = write(stderr, s); write(stderr, "\n")


type AskPassFail = object of CatchableError
var run_ask_pass_done: proc () # global needed for indirect call from sigaction

proc run_ask_pass(argv: openArray[string]): string =
	# osproc in nim doesn't allow to only pass stdin through, hence all this
	var
		cmd = argv[0]
		proc_pipe: array[0..1, cint]
		pid: Pid
	template chk(e: untyped) =
		if e != 0: raiseOSError(osLastError())
	chk pipe(proc_pipe)
	let (pipe_r, pipe_w) = (proc_pipe[0], proc_pipe[1])
  defer: chk (close(pipe_r) or close(pipe_w))

	block start_proc:
		var
			attr: Tposix_spawnattr
			fops: Tposix_spawn_file_actions
			mask: Sigset
			flags = POSIX_SPAWN_USEVFORK or POSIX_SPAWN_SETSIGMASK
			args = allocCStringArray(argv)
			env = allocCStringArray( block:
				var env = newSeq[string](0)
				for key, val in envPairs(): env.add(&"{key}={val}")
				env )
		defer: deallocCStringArray(args); deallocCStringArray(env)
		chk posix_spawn_file_actions_init(fops)
		defer: discard posix_spawn_file_actions_destroy(fops)
		chk posix_spawnattr_init(attr)
		defer: discard posix_spawnattr_destroy(attr)
		chk sigemptyset(mask)
		chk posix_spawnattr_setsigmask(attr, mask)
		chk posix_spawnattr_setflags(attr, flags)
		chk posix_spawn_file_actions_addclose(fops, pipe_r)
		chk posix_spawn_file_actions_adddup2(fops, pipe_w, 1)
		chk posix_spawnp(pid, cmd.cstring, fops, attr, args, env)

	run_ask_pass_done = proc =
		run_ask_pass_done = nil
		if write(pipe_w, "\0".cstring, 1) != 1:
			raiseOSError(osLastError(), &"{cmd}: exit handler failure")
	defer: (if run_ask_pass_done != nil: discard kill(pid, SIGKILL))

	block read_output:
		var
			bs: int
			buff = newString(128)
			buff_bs: string
			sa = Sigaction( # should always be fired w/ SIGKILL
				sa_flags: SA_RESETHAND or SA_NOCLDSTOP,
				sa_handler: proc (sig: cint) {.noconv.} = run_ask_pass_done() )
		chk sigemptyset(sa.sa_mask)
		chk sigaddset(sa.sa_mask, SIGPIPE)
		chk sigaction(SIGCHLD, sa, nil)
		while true:
			bs = read(pipe_r, buff.cstring, buff.len).int
			if bs < 0:
				let err = osLastError()
				if err == OSErrorCode(EINTR): continue
				raiseOSError(err, &"{cmd}: stdout read failed")
			elif bs == 0: continue
			buff_bs = buff[0 ..< bs].strip(leading=false, chars={'\0'})
			result.add buff_bs
			if buff_bs.len < bs: break

	block check_exit_code:
		var st = 0.cint
		if waitpid(pid, st, 0) < 0: raiseOSError(osLastError(), &"{cmd}: waitpid")
		if st.int != 0: raise newException(AskPassFail, &"{cmd} = {st.int}")


proc main_help(err="") =
	proc bool_val(v: int): string =
		case v
		of -1: result = ""
		of 0: result = "false"
		of 1: result = "true"
		else: result = ""
	proc print(s: string) =
		let dst = if err == "": stdout else: stderr
		write(dst, s); write(dst, "\n")
	let app = getAppFilename().lastPathPart
	if err != "": print &"ERROR: {err}\n"
	print &"Usage: {app} [options] [file]"
	if err != "":
		print &"Run '{app} --help' for more information"
		quit 0
	let fhb_cid_info = if FHB_CID != "": &"<{FHB_CID.len}B-base64-value>" else: ""
	let fhb_salt_info = if FHB_Salt != "": &"<{FHB_Salt.len}B-base64-value>" else: ""
	print dedent(&"""

		Wait for user input on /dev/console, and run HMAC operation on
			FIDO2 device, outputting resulting data to specified file or stdout.
		Most options can be set at build-time via -d:FHB_* to "nim c ..." command.

		Input/output options:

			-r/--rpid <relying-party-id>
				Hostname-like Relying Party ID value, that was used with
					fido2-cred when creating the authenticator credential to produce hmac for.
				Compiled-in default: {FHB_RPID=}

			-d/--dev <dev-spec>
				FIDO2 device path or URL-like spec like pcsc://slot0 that libfido2 supports.
				Compiled-in default: {FHB_Dev=}

			-c/--cred-id <base64-blob>
				Credential ID value returned by fido2-cred or similar tool when creating FIDO2
					credential, if it's not Discoverable/Resident (in which case -r/--rpid will select it).
				Compiled-in default: FHB_CID={fhb_cid_info}

			-s/--hmac-salt <base64-blob>
				32B base64-encoded HMAC salt value.
				Same unique output will always be produced for unique salt/credential combination.
				Compiled-in default: FHB_Salt={fhb_salt_info}

			-a/--ask-cmd <command>
				Command + args to ask for password/pin/etc somehow and print on its stdout.
				systemd-ask-password can be used on systemd-enabled system/initramfs.
				If this command is set to an empty string, no attempt to ask anything is made.
				Otherwise, prompt is always presented, but if User Verification is not needed,
					only to delay actual fido2 token operations, without using whatever is entered.
				Any leading/trailing whitespace is always stripped from entered string.
				Compiled-in default: {FHB_Ask_Cmd=}

			-n/--ask-attempts <n>
				Number of attempts to ask for pw/pin. 0 or less - disable prompting.
				Empty entry can be used to skip it cleanly when User Verification
					(--uv) is explicitly enabled, or -b/--ask-bypass words to give up.
				Compiled-in default: {FHB_Ask_Attempts=}

			-b/--ask-bypass <space-separated-words>
				Space-separated list of words that can be entered to give up on trying to enter it.
				If User Verification (--uv) is enabled, giving up on pin entry exits with an error.
				Compiled-in default: {FHB_Ask_Bypass=}

			-t/--timeout <seconds>
				Device presence/verification check timeout, in seconds.
				Tool does not wait for device to be plugged-in, only for checks on one.
				Compiled-in default: {FHB_Timeout=}

			--up <y/n/true/false/0/1>
				Check for User Presence (UP), usually via device-button touch.
				Default is leave it up to parameters set on device/credential.
				Compiled-in default: FHB_UP={bool_val(FHB_UP)}

			--uv <y/n/true/false/0/1>
				Check for User Verification (UV), requiring a PIN to be entered correctly.
				Default is leave it up to parameters set on device/credential.
				Compiled-in default: FHB_UV={bool_val(FHB_UV)}

			--out-b64
				Output base64-encoded HMAC value instead of raw bytes.

			--fido2-debug
				Enable debug output from libfido2.

		Special usage modes:

			{app} [options] {{ -h | --help }} ...
				Print this usage information and exit.

			{app} [--out-b64] --hmac key-file non-secret-string [output-file]
				Run HMAC-SHA256 operation with secret key read from specified key-file,
					and non-secret-string specifying the hashed input as a command-line argument.
				Contents of key-file are used for HMAC key as-is without any processing.
				Prints raw (binary) 32B hash output to stdout by default and exits.
				Specifying --out-b64 option will wrap output into base64 encoding.
				Adding output-file argument will write output to that file instead of stdout.
		""")
	quit 0

proc main(argv: seq[string]) =
	var
		fhb_rpid = FHB_RPID
		fhb_salt = FHB_Salt
		fhb_dev = FHB_Dev
		fhb_cred = FHB_CID
		fhb_ask_cmd = FHB_Ask_Cmd
		fhb_ask_attempts = FHB_Ask_Attempts
		fhb_ask_bypass = FHB_Ask_Bypass
		fhb_timeout = FHB_Timeout
		fhb_up = FHB_UP
		fhb_uv = FHB_UV
		opt_file = ""
		opt_debug = false
		opt_out_b64 = false
		opt_hmac = false
		opt_hmac_keyfile = ""
		opt_hmac_data = ""

	block cli_parser:
		var opt_last = ""

		proc opt_bool_int(k: string, v: string): int =
			if v in ["y", "yes", "t", "true", "1"]: return 1
			elif v in ["n", "no", "f", "false", "0"]: return 0
			elif v == "": return -1
			let opt = if k.len == 1: &"-{k}" else: &"--{k}"
			main_help(&"Failed to parse {opt} to boolean value [ {v} ]")

		proc opt_empty_check =
			if opt_last == "": return
			let opt = if opt_last.len == 1: &"-{opt_last}" else: &"--{opt_last}"
			main_help(&"{opt} requires a value")

		proc opt_set(k: string, v: string) =
			if k in ["r", "rpid"]: fhb_rpid = v
			elif k in ["s", "hmac-salt"]: fhb_salt = v
			elif k in ["d", "dev"]: fhb_dev = v
			elif k in ["c", "cred-id"]: fhb_cred = v
			elif k in ["a", "ask-cmd"]: fhb_ask_cmd = v
			elif k in ["n", "ask-attempts"]: fhb_ask_attempts = parseInt(v)
			elif k in ["b", "ask-bypass"]: fhb_ask_bypass = v
			elif k in ["t", "timeout"]: fhb_timeout = parseInt(v)
			elif k == "up": fhb_up = opt_bool_int(k, v)
			elif k == "uv": fhb_uv = opt_bool_int(k, v)
			else: quit(&"BUG: no type info for option [ {k} = {v} ]")

		for t, opt, val in getopt(argv):
			case t
			of cmdEnd: break
			of cmdShortOption, cmdLongOption:
				if opt in ["h", "help"]: main_help()
				elif opt == "fido2-debug": opt_debug = true
				elif opt == "out-b64": opt_out_b64 = true
				elif opt == "hmac": opt_hmac = true
				elif val == "": opt_empty_check(); opt_last = opt
				else: opt_set(opt, val)
			of cmdArgument:
				if opt_last != "": opt_set(opt_last, opt); opt_last = ""
				elif opt_hmac and opt_hmac_keyfile == "": opt_hmac_keyfile = opt
				elif opt_hmac and opt_hmac_data == "": opt_hmac_data = opt
				elif opt_file == "": opt_file = opt
				else: main_help(&"Unrecognized argument: {opt}")
		opt_empty_check()

		if not opt_hmac:
			if fhb_rpid == "":
				main_help( "-d:FHB_RPID=some.host.name" &
					" must be set at build-time or via -r/--rpid option" )
			if fhb_dev == "":
				main_help( "-d:FHB_DEV=<dev-node-or-spec>" &
					" must be set at build-time or via -d/--dev option" )
			if fhb_salt == "":
				main_help( "-d:FHB_SALT=... must" &
					" be set at build-time or via -s/--salt option" )

		else:
			if opt_hmac_keyfile == "":
				main_help("key-file argument is required when using --hmac mode")
			if opt_hmac_data == "":
				main_help("non-secret-string/data argument is required with --hmac mode")


	var hmac: string

	if opt_hmac:
		hmac = hmac_sha256(readFile(opt_hmac_keyfile), opt_hmac_data)

	block fido2_mode:
		if opt_hmac: break fido2_mode

		fido_init(if opt_debug: FIDO_DEBUG else: 0)
		var a = fido_assert_new()
		var r: cint

		block fido_assert_setup:
			fido(assert_set_clientdata, a, client_data.cstring, client_data.len.cint)
			fido(assert_set_rp, a, fhb_rpid.cstring)
			fido(assert_set_extensions, a, FIDO_EXT_HMAC_SECRET)
			if fhb_up >= 0: fido(assert_set_up, a, fhb_up.cint)
			if fhb_uv >= 0: fido(assert_set_uv, a, fhb_uv.cint)
			if fhb_cred != "":
				fhb_cred = decode(fhb_cred)
				fido(assert_allow_cred, a, fhb_cred.cstring, fhb_cred.len.cint)
			fhb_salt = decode(fhb_salt)
			fido(assert_set_hmac_salt, a, fhb_salt.cstring, fhb_salt.len.cint)
		defer: fido_assert_free(addr(a))

		block fido_assert_attempts:
			fhb_ask_cmd = fhb_ask_cmd.strip
			if fhb_ask_cmd == "" or fhb_ask_attempts <= 0:
				fhb_ask_cmd = ""; fhb_ask_attempts = 1
			var
				pin: string
				fhb_ask_bypass_words = fhb_ask_bypass.splitWhitespace

			for i in 1 .. fhb_ask_attempts:

				if fhb_ask_cmd != "":
					try: pin = run_ask_pass(fhb_ask_cmd.split(' ')).strip
					except AskPassFail:
						p_err &"FAIL: Password/PIN entry command failed"
						pin = "" # will proceed with assertion attempt, if it's not required
					if pin in fhb_ask_bypass_words:
						if fhb_uv > 0: quit("ERROR: UV PIN entry cancelled")
						p_err "EXIT: HMAC-secret generation cancelled"
						quit 0
					elif pin == "" and fhb_uv > 0: continue
					if fhb_uv == 0: pin = ""

				block fido_assert_send:
					var dev = fido_dev_new()
					defer: fido_dev_free(addr(dev))
					r = fido_dev_open(dev, fhb_dev.cstring)
					if r != FIDO_OK:
						p_err &"FAIL: fido-open failed [ {fhb_dev} ] - {fido_strerr(r.cint)}"
						continue
					defer: fido(dev_close, dev)
					fido(dev_set_timeout, dev, cint(fhb_timeout * 1000))
					r = fido_dev_get_assert(dev, a, if pin == "": nil else: pin.cstring)
					if r != FIDO_OK:
						fido_dev_cancel(dev)
						p_err &"FAIL: fido-assert failed - {fido_strerr(r.cint)}"
					else: break fido_assert_attempts

			quit("ERROR: Failed to get HMAC value from the device")

		hmac = block fido_get_hmac:
			r = fido_assert_count(a)
			if r != 1: raise newException(FidoError, &"fido-assert failed - multiple results [{r.int}]")
			let
				hmac_len = fido_assert_hmac_secret_len(a, 0)
				hmac_ptr = fido_assert_hmac_secret_ptr(a, 0)
				hmac = newString(hmac_len)
			copyMem(hmac.cstring, hmac_ptr, hmac_len)
			hmac


	block output:
		if opt_out_b64: hmac = encode(hmac)
		if opt_file != "": writeFile(opt_file, hmac)
		else: write(stdout, hmac)

when is_main_module: main(os.commandLineParams())
