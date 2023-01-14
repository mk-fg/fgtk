#? replace(sub = "\t", by = "  ")
# Small binary to be compiled with fido2 assertion parameters, to prompt for PIN
#   on /dev/console and produce secret for something on early boot, like cryptsetup/dm-crypt.
#
# Build with: nim c -w=on --hints=on -o=fhb -r fido2_hmac_boot.nim -h && strip fhb
# Usage info: ./fhb -h
# Intended to complement libfido2 cli tools, like fido2-token and fido2-cred.

import std/strformat
import std/macros
import std/os
import std/strutils
import std/parseopt
import std/base64


{.passl: "-lfido2"}

const FHB_RPID {.strdefine.} = "" # Relying Party ID string, e.g. fhd.mysite.com
const FHB_Salt {.strdefine.} = "" # 32B HMAC salt base64 that corresponds to same unique output
const FHB_Dev {.strdefine.} = "" # default device, e.g. "/dev/yubikey" or "pcsc://slot0"
const FHB_CID {.strdefine.} = "" # Credential ID base64 blob from fido2-cred
const FHB_Timeout {.intdefine.} = 30 # timeout for user presence check (touch)
const FHB_UP {.booldefine.}: int = -1 # user presence check (touch), up to device by default
const FHB_UV {.booldefine.}: int = -1 # user verification via PIN, up to device by default
const client_data = "fido2-hmac-boot.client-data.0001" # challenge value is not used here


var
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
		quit(1)
	print dedent(&"""

		Wait for user input on /dev/console, and run HMAC operation on
			FIDO2 device, outputting resulting data to specified file or stdout.
		Most options can be set at build-time via -d:FHB_* to "nim c ..." command.

		Options:

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
				Compiled-in default: {FHB_CID=}

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

			-s/--hmac-salt <base64-blob>
				32B base64-encoded HMAC salt value.
				Same unique output will always be produced for unique salt/credential combination.
				Compiled-in default: {FHB_Salt=}

			--out-b64
				Output base64-encoded HMAC value instead of raw bytes.

			--fido2-debug
				Enable debug output from libfido2.
		""")
	quit(0)

proc main(argv: seq[string]) =
	var
		fhb_rpid = FHB_RPID
		fhb_salt = FHB_Salt
		fhb_dev = FHB_Dev
		fhb_cred = FHB_CID
		fhb_timeout = FHB_Timeout
		fhb_up = FHB_UP
		fhb_uv = FHB_UV
		opt_file = ""
		opt_debug = false
		opt_out_b64 = false

	block cli_parser:
		var opt_last = ""

		proc opt_bool_int(k: string, v: string): int =
			if v in ["y", "yes", "t", "true", "1"]: return 1
			elif v in ["n", "no", "f", "false", "0"]: return 0
			elif v == "": return -1
			let opt = if k.len == 1: &"-{k}" else: &"--{k}"
			main_help(&"Failed to parse {opt} to boolean value [ {v} ]")

		proc opt_set(k: string, v: string) =
			if k in ["r", "rpid"]: fhb_rpid = v
			elif k in ["s", "hmac-salt"]: fhb_salt = v
			elif k in ["d", "dev"]: fhb_dev = v
			elif k in ["c", "cred-id"]: fhb_cred = v
			elif k in ["t", "timeout"]: fhb_timeout = parseInt(v)
			elif k == "up": fhb_up = opt_bool_int(k, v)
			elif k == "uv": fhb_up = opt_bool_int(k, v)
			else: quit(&"BUG: no type info for option [ {k} = {v} ]", 1)

		proc opt_empty_check =
			if opt_last == "": return
			let opt = if opt_last.len == 1: &"-{opt_last}" else: &"--{opt_last}"
			main_help(&"{opt} requires a value")

		for t, opt, val in getopt(argv):
			case t
			of cmdEnd: break
			of cmdShortOption, cmdLongOption:
				if opt in ["h", "help"]: main_help()
				elif opt == "fido2-debug": opt_debug = true
				elif opt == "out-b64": opt_out_b64 = true
				elif val == "": opt_empty_check(); opt_last = opt
				else: opt_set(opt, val)
			of cmdArgument:
				if opt_last != "": opt_set(opt_last, opt); opt_last = ""
				elif opt_file == "": opt_file = opt
				else: main_help(&"Unrecognized argument: {opt}")
		opt_empty_check()

		if fhb_rpid == "":
			main_help( "-d:FHB_RPID=some.host.name" &
				" must be set at build-time or via -r/--rpid option" )
		if fhb_salt == "":
			main_help( "-d:FHB_SALT=... must" &
				" be set at build-time or via -s/--salt option" )

	fido_init(if opt_debug: FIDO_DEBUG else: 0)
	var a = fido_assert_new()
	var r: cint
	var pin: cstring = nil # XXX

	block fido_assert_setup:
		fido(assert_set_clientdata, a, client_data.cstring, cint(client_data.len))
		fido(assert_set_rp, a, fhb_rpid.cstring)
		fido(assert_set_extensions, a, FIDO_EXT_HMAC_SECRET)
		if fhb_up >= 0: fido(assert_set_up, a, cint(fhb_up))
		if fhb_uv >= 0: fido(assert_set_uv, a, cint(fhb_uv))
		if fhb_cred != "":
			fhb_cred = decode(fhb_cred)
			fido(assert_allow_cred, a, fhb_cred.cstring, cint(fhb_cred.len))
		fhb_salt = decode(fhb_salt)
		fido(assert_set_hmac_salt, a, fhb_salt.cstring, cint(fhb_salt.len))

	block fido_assert_send:
		var dev = fido_dev_new()
		fido(dev_set_timeout, dev, cint(fhb_timeout * 1000))
		if fhb_dev != "": fido(dev_open, dev, fhb_dev.cstring)
		r = fido_dev_get_assert(dev, a, pin)
		if r != FIDO_OK:
			fido_dev_cancel(dev)
			raise newException(FidoError, "fido-assert failed - " & $fido_strerr(cint(r)))
		fido(dev_close, dev)
		fido_dev_free(addr(dev))

	var hmac = block fido_get_hmac:
		r = fido_assert_count(a)
		if r != 1: raise newException(FidoError, &"fido-assert failed - multiple results [{int(r)}]")
		let
			hmac_len = fido_assert_hmac_secret_len(a, 0)
			hmac_ptr = fido_assert_hmac_secret_ptr(a, 0)
			hmac = newString(hmac_len + 1)
		copyMem(hmac.cstring, hmac_ptr, hmac_len)
		hmac

	fido_assert_free(addr(a))

	block output:
		if opt_out_b64: hmac = encode(hmac)
		if opt_file != "": writeFile(opt_file, hmac)
		else: write(stdout, hmac)

when is_main_module: main(os.commandLineParams())
