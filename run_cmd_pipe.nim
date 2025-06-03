#? replace(sub = "\t", by = "  ")
#
# Debug build/run: nim c -w=on --hints=on -r run_cmd_pipe.nim -h
# Final build: nim c -d:release -d:strip -d:lto_incremental --opt:size run_cmd_pipe.nim
# Usage info: ./run_cmd_pipe -h

import std/[ parseopt, os, posix, logging, re, osproc, strtabs,
	strformat, strutils, sequtils, times, monotimes, tables, selectors ]

template nfmt(v: untyped): string = ($v).insertSep # format integer with digit groups


type
	Conf = tuple[
		mtime: times.Time,
		delay: int,
		cooldown: int,
		rules: Table[string, Rule] ]
	Rule = tuple[
		name: string,
		regexp: Regex,
		regexp_env_var: string,
		regexp_env_group: int,
		regexp_run_group: int,
		run: seq[string] ]
const regexp_group_max = 19

proc parse_conf(conf_path: string): Conf =
	var
		mtime: times.Time
		delay = -1
		cooldown = -1
		rules = initTable[string, Rule]()
		re_comm = re"^\s*([#;].*)?$"
		re_name = re"^\s*\[(.*)\]\s*$"
		re_name_int_prefix = re"^--[^-]" # to avoid clashes with internal event name
		re_name_int_infix = re" (¦+) " # avoids clashes with regexp-run-group matches
		re_var = re"^\s*(\S.*?)\s*(=\s*(\S.*?)?\s*)?$"
		line_n = 0
		name = ""
		regexp: Regex
		run: seq[string]
		regexp_env_var = "RCP_MATCH"
		regexp_env_group = 0
		regexp_run_group = -1
	mtime = getLastModificationTime(conf_path)
	for line in (readFile(conf_path) & "\n[end]").splitLines:
		line_n += 1
		if line =~ re_comm: continue
		elif line =~ re_name:
			if name != "":
				if regexp == nil: warn(&"Ignoring rule with missing regexp: {name}")
				else:
					if rules.contains(name):
						warn(&"Replacing earlier rule with duplicate name: {name}")
					rules[name] = ( name, regexp,
						regexp_env_var, regexp_env_group, regexp_run_group, run )
			name = matches[0]; regexp = nil; run = @[]
			regexp_env_var = "RCP_MATCH"; regexp_env_group = 0; regexp_run_group = -1
			if name =~ re_name_int_prefix: name = "-" & name
			if name =~ re_name_int_infix: name = name.replacef(re_name_int_infix, " $1¦ ")
		elif line =~ re_var:
			if name == "":
				try:
					if matches[0] == "cooldown": cooldown = matches[2].parseInt
					elif matches[0] == "delay": delay = matches[2].parseInt
					else: warn(&"Ignoring unrecognized config-option line {line_n} [ {line} ]")
				except ValueError as err:
					warn(&"Failed to parse config value on line {line_n} [ {line} ] :: {err.msg}")
			else:
				try:
					let k = matches[0]; let v = matches[2]
					if k == "regexp": regexp = re(&"({v})")
					elif k == "regexp-ci":
						regexp = re(&"({v})", {reIgnoreCase, reStudy})
					elif k == "regexp-env-var": regexp_env_var = v.strip
					elif k in ["regexp-env-group", "regexp-run-group"]:
						let n = v.parseInt
						if n > regexp_group_max:
							raise newException( ValueError,
								&"Regexp match-group N must be < {regexp_group_max+1}" )
						if k == "regexp-env-group": regexp_env_group = n
						else: regexp_run_group = n
					elif k == "run": run = v.strip.split
					else: warn(&"Ignoring unrecognized rule-option line {line_n} [ {line} ]")
				except Exception as err:
					warn(&"Failed to parse config value on line {line_n} [ {line} ] :: {err.msg}")
		else: warn(&"Failed to parse config-file line {line_n} [ {line} ]")
	return (mtime, delay, cooldown, rules)


proc start_input_proc(argv: openArray[string]): (File, Pid) =
	# osproc in nim doesn't allow good-enough control to remap stdin/stdout correctly
	var
		cmd = argv[0]
		proc_pipe: array[0..1, cint]
		pid: Pid
		stream: File
	template chk(e: untyped) =
		if e != 0: raiseOSError(osLastError())
	chk pipe(proc_pipe)
	let (pipe_r, pipe_w) = (proc_pipe[0], proc_pipe[1])
	block start_proc:
		var
			attr: Tposix_spawnattr
			fops: Tposix_spawn_file_actions
			mask: Sigset
			flags = POSIX_SPAWN_USEVFORK or POSIX_SPAWN_SETSIGMASK
			args = allocCStringArray(argv)
			env = allocCStringArray( block:
				var env = newSeq[string]()
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
	chk close(pipe_w)
	if not stream.open(pipe_r): raiseOSError(osLastError())
	return (stream, pid)


var stream_pid: Pid = 0 # global for noconv from signal handlers

proc main_help(err="") =
	proc print(s: string) =
		let dst = if err == "": stdout else: stderr
		write(dst, s); write(dst, "\n")
	let app = getAppFilename().lastPathPart
	if err != "": print &"ERROR: {err}"
	print &"\nUsage: {app} [opts] rcp.conf [-- cmd [args...]]"
	if err != "": print &"Run '{app} --help' for more information"; quit 1
	print dedent(&"""

		{app} is a small tool to parse pairs of regexp-command from conf-file,
			then read input lines from stdin (or from specified subprocess' stdout),
			match those against regexps (PCRE), and run corresponding command(s) for them.
		Commands are expected to finish quickly and exit, use systemctl
			or systemd-run with --no-block to start long-running tasks instead.
		All numeric parameters can be set at the top of the config file too,
			before any rules, but command-line values always override those, if specified.
		SIGHUP can be used to reload all options and rules from the configuration file.

		Configuration file example (basic ini format):

			# Global opts can be set before rules. Comment lines can start with "#" or ";".
			delay = 1_000

			# All rule opts are listed below. Defaults: re-var=RCP_MATCH re-group=0
			[my-rule]
			regexp = systemd\[(\d+)\]: (.*)$
			# regexp-ci = ... -- same as regexp, but case-insensitive
			regexp-env-var = SD_LOG_MSG
			regexp-env-group = 2
			# regexp-run-group = 1 -- to run/delay/cooldown process for rule+match-group
			run = env
			# ...plus any more such rule blocks, any number of which can match each line.

		 -c / --cooldown milliseconds
			Min interval between running commands on events (default=300ms).
			If multiple events to trigger same rule (or rule + regexp-run-group)
				are detected, command will run after cooldown delay with last regexp-match.

		 -d / --delay milliseconds
			Add fixed time delay (in ms) from event to running a corresponding command.
			Can be used for debouncing events - e.g. if multiple events to trigger
				some shared action are common in rapid succession, delaying command
				by 1s can avoid running it multiple times for each of them needlessly.

		 --debug -- Verbose operation mode.
		""")
	quit 0

proc main(argv_full: seq[string]) =
	var
		argv = argv_full
		opt_debug = false
		opt_conf = ""
		opt_cooldown = -1
		opt_delay = -1
		opt_cmd: seq[string]

	let n = argv.find("--")
	if n != -1: opt_cmd = argv[n+1..<argv.len]; argv = argv[0..<n]

	block cli_parser:
		var opt_last = ""
		proc opt_fmt(opt: string): string =
			if opt.len == 1: &"-{opt}" else: &"--{opt}"
		proc opt_empty_check =
			if opt_last == "": return
			main_help &"{opt_fmt(opt_last)} option unrecognized or requires a value"
		proc opt_set(k: string, v: string) =
			if k in ["c", "cooldown"]: opt_cooldown = v.parseInt
			if k in ["d", "delay"]: opt_delay = v.parseInt
			else: main_help &"Unrecognized option [ {opt_fmt(k)} = {v} ]"
		for t, opt, val in getopt(argv):
			case t
			of cmdEnd: break
			of cmdShortOption, cmdLongOption:
				if opt in ["h", "help"]: main_help()
				elif opt in ["debug"]: opt_debug = true
				elif val == "": opt_empty_check(); opt_last = opt
				else: opt_set(opt, val)
			of cmdArgument:
				if opt_last != "": opt_set(opt_last, opt); opt_last = ""
				elif opt_conf != "": main_help &"Unrecognized argument: {opt}"
				else: opt_conf = opt
		opt_empty_check()
		if opt_conf == "": main_help &"Missing required config-file argument"

	var logger = newConsoleLogger(
		fmtStr="$levelid $datetime :: ", useStderr=true,
		levelThreshold=if opt_debug: lvlAll else: lvlInfo )
	addHandler(logger)

	var
		stream = stdin
		rules: Table[string, Rule]
		s = newSelector[string]()
		rule_last_ts = initTable[string, MonoTime]()
		rule_last_match = initTable[string, string]()
		rule_procs = initTable[string, Process]()
		rule_matches: array[regexp_group_max + 1, string]
		td_cooldown: Duration
		td_delay: Duration

	proc conf_load() =
		debug("(Re-)Loading config file: ", opt_conf)
		let conf = parse_conf(opt_conf)
		td_cooldown = initDuration(milliseconds=(
			if opt_cooldown > 0: opt_cooldown
			elif conf.cooldown >= 0: conf.cooldown else: 300 ))
		td_delay = initDuration(milliseconds=(
			if opt_delay > 0: opt_delay elif conf.delay >= 0: conf.delay else: 0 ))
		rules = conf.rules
		for name in toSeq(rule_last_ts.keys):
			if not rules.contains(name): continue
			rule_last_ts.del(name); rule_last_match.del(name); rule_procs.del(name)
	conf_load()

	if opt_cmd.len > 0:
		debug("Running input-command: ", opt_cmd.join(" "))
		(stream, stream_pid) = start_input_proc(opt_cmd)
	defer:
		if stream_pid <= 0: return
		stream.close
		if kill(stream_pid, SIGTERM) != 0'i32: raiseOSError(osLastError())
		onSignal(SIGALRM): discard kill(stream_pid, SIGKILL)
		var st = 0'i32; discard alarm(2'i32)
		if waitpid(stream_pid, st, 0) < 0: raiseOSError(osLastError())

	s.registerHandle(getOsFileHandle(stream), {Event.Read}, "--line")
	s.registerSignal(SIGHUP, "--reload")
	s.registerSignal(SIGINT, "--exit")
	s.registerSignal(SIGTERM, "--exit")
	defer: s.close

	debug("Starting main loop...")
	var
		stream_eof = false
		line: string
		ts_select = getMonoTime()
	while not stream_eof:

		### Build run_rules list to start commands
		var
			run_rules = newSeq[string]()
			evs = newSeq[ReadyKey]()
		try: evs = s.select(-1)
		except IOSelectorsException as err: # EAGAIN seem to happen on overflows
			if (getMonoTime() - ts_select).inMilliseconds >= 2_000: raiseOSError(
				OSErrorCode(EAGAIN), &"Persistent failures in event selector: {err.msg}" )
			sleep(50); continue
		ts_select = getMonoTime()
		for rk in evs:
			if Event.Error in rk.events:
				debug(&"-- close/err: [{rk.errorCode}] {osErrorMsg(rk.errorCode)}")
				stream_eof = true; break
			s.withData(rk.fd, ev_ptr):
				let ev = ev_ptr[]

				block non_input_evs:
					if ev == "--line": break non_input_evs
					debug("-- event [ ", ev, " ]")
					if ev == "--reload": conf_load()
					elif ev == "--exit": debug("Exiting on signal"); quit()
					elif ev.startswith("--proc "):
						let name = ev[7..<ev.len]
						rule_procs.withValue(name, pr_ptr):
							debug("++ process exited [ ", name, " ] code=", pr_ptr[].peekExitCode)
							rule_procs.del(name)
						do: debug("++ process exited [ ", name, " ] untracked")
					else: run_rules.add(ev) # delay timer
					continue

				try: line = stream.readLine.strip
				except EOFError: debug("-- end-of-file"); stream_eof = true; break
				debug("-- input line: ", line)

				for name, rule in rules.pairs:
					if not line.contains(rule.regexp, rule_matches): continue
					debug("Rule match [ ", rule.name, " ]")
					let ts_now = getMonoTime()
					rule_last_match[rule.name] = rule_matches[rule.regexp_env_group]
					var key = if rule.regexp_run_group < 0: rule.name
						else: &"{rule.name} ¦ {rule_matches[rule.regexp_run_group]}"

					rule_last_ts.withValue(key, ts_last_ptr):
						let ts_last = ts_last_ptr[]
						if ts_last > ts_now:
							debug("Rule already scheduled [ ", key, " ]")
							continue
						let delay = td_cooldown - (ts_now - ts_last)
						if delay > DurationZero:
							debug("Rule cooldown-delay [ ", key, " ] ms=", delay.inMilliseconds.nfmt)
							rule_last_ts[key] = ts_last + td_cooldown
							s.registerTimer(delay.inMilliseconds, true, key)
							continue

					if td_delay > DurationZero:
						rule_last_ts[key] = ts_now + td_delay
						debug("Rule fixed-delay [ ", key, " ] ms=", td_delay.inMilliseconds.nfmt)
						s.registerTimer(td_delay.inMilliseconds, true, key)

					else:
						rule_last_ts[key] = ts_now
						run_rules.add(key)

			do: warn("Event-poller unexpected ev={rk.events} fd={rk.fd}")

		### Check/start run_rules commands
		var
			rule: Rule
			env: StringTableRef
			pr: Process
			pid: int
			exit_code: int
		for key in run_rules:
			let name = key.split(" ¦ ", 1)[0]
			debug("Rule run [ ", key, " ]", if key == name: "" else: &" -> [ {name} ]")
			if not rules.contains(name): continue
			rule = rules[name]
			if rule.run.len == 0: continue

			rule_procs.withValue(name, pr_ptr):
				pr = pr_ptr[]; exit_code = pr.peekExitCode
				if exit_code < 0:
					debug("++ process already running [ ", name, " ] pid=[", pr.processID, " ]")
					continue
				else: # should normally be handled in selector loop
					debug("++ earlier process collected [ ", name, " ] code=", exit_code)
					pr.close

			rule_last_match.withValue(name, m_ptr):
				env = newStringTable(modeCaseSensitive)
				for k, v in envPairs(): env[k] = v
				env[rule.regexp_env_var] = m_ptr[]
			do: env = nil
			try:
				pr = startProcess( rule.run[0], env=env,
					args=rule.run[1..<rule.run.len], options={poUsePath, poParentStreams} )
				pid = pr.processID
			except OSError as err:
				warn(&"Failed to run rule command [ {name} ] :: {err.msg}")
				continue
			debug("++ started process [ ", name, " ] pid=[ ", pid, " ]")
			rule_procs[name] = pr
			s.registerProcess(pid, &"--proc {name}")

	debug("Finished")

when is_main_module: main(commandLineParams())
