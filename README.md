fgtk: a set of a misc tools to work with files and processes
--------------------

Various oldish helper binaries I wrote to help myself with day-to-day tasks.


### Files

#### scim set

A set of tools to bind a bunch of scattered files to a single path, with
completely unrelated internal path structure. Intended usage is to link
configuration files to scm-controlled path (repository).

Actually started as [cfgit project](http://fraggod.net/code/git/configit/), but
then evolved away from git vcs into a more generic, not necessarily vcs-related,
solution.

##### scim-ln

Adds a new link (symlink or catref) to a manifest (links-list), also moving file
to scim-tree (repository) on fs-level.

##### scim

Main tool to check binding and metadata of files under scim-tree. Basic
operation boils down to two (optional) steps:

* Check files' metadata (uid, gid, mode, acl, posix capabilities) against
  metadata-list (.scim_meta, by default), if any, updating the metadata/list if
  requested, except for exclusion-patterns (.scim_meta_exclude).

* Check tree against links-list (.scim_links), warning about any files / paths
  in the same root, which aren't on the list, yet not in exclusion patterns
  (.scim_links_exclude).

##### pyacl

Tool to restore POSIX ACLs on paths, broken by chmod or similar stuff without
actually changing them.

##### rsync_diff

Tool to sync paths, based on berkley db and rsync.

Keeps b-tree of paths (files and dirs) and corresponding mtimes in berkdb,
comparing state when ran and building a simple merge-filter for rsync ("+ /path"
line for each changed file/dir, including their path components, ending with "-
*").
Then it runs a single rsync with this filter to efficiently sync the paths.

Note that the only difference from "rsync -a src dst" here is that "dst" tree
doesn't have to exist on fs, otherwise scanning "dst" should be pretty much the
same (and probably more efficient, depending on fs implementation) b-tree
traversal as with berkdb.

Wrote it before realizing that it's quite pointless for my mirroring use-case -
I do have full source and destination trees, so rsync can be used to compare (if
diff file-list is needed) or sync them.

##### fs

Complex tool for high-level fs operations. Reference is built-in.

Copy files, setting mode and ownership for the destination:

	fs -m600 -o root:wheel cp * /somepath

Temporarily (1hr) change attributes (i.e. to edit file from user's editor):

	fs -t3600 -m600 -o someuser expose /path/to/file

Copy ownership/mode from one file to another:

	fs cps /file1 /file2

##### fatrace_pipe

[fatrace](https://launchpad.net/fatrace)-based script to read filesystem write
events via linux [fanotify](http://lwn.net/Articles/339253/) system and match
them against specific path and app name, sending matches to a FIFO pipe.

Use-case is to, for example, setup watcher for development project dir changes,
sending instant "refresh" signals to something that renders the project or shows
changes' results otherwise.

FIFO is there because fanotify requires root privileges, and running some
potentially-rm-rf-/ ops as uid=0 is a damn bad idea.
User's pid can read lines from the fifo and react to these safely instead.

Example - run "make" on any change to `~user/hatch/project` files:

	(root) ~# fatrace_pipe ~user/hatch/project
	(user) project% xargs -in1 </tmp/fatrace.fifo make


### Content

##### pysed

This one is for simple pcre-based text replacement, basically a sed's
"s/from/to/" command with lookahead/lookbehind assertions.

Example, to replace all two-space indents with tabs and drop space-based inline
alignment:

	pysed '(?<=\w)\s+(?=\w)' ' ' '^\s*  ' '\t' -i10 -b somecode.py

##### pysort

Sort file contents, based on some key-field with support for multiple field
delimeters. Wrote this one when my /etc/passwd got messy and I just wanted to
sort its contents by uid.

##### color

Outputs terminal color sequences, making important output more distinctive.

Also can be used to interleave "tail -f" of several logfiles in the same terminal:

	t -f /var/log/app1.log | color red - &
	t -f /var/log/app2.log | color green - &
	t -f /var/log/app2.log | color blue - &


### Dev

##### tabs_filter

My secret weapon in tabs-vs-spaces holywar.

In my emacs, tab key always inserts "\t", marking spaces as a bug with
develock-mode.
This script transparently converts all indent-tabs into spaces and back,
designed to be used from git content filters, and occasionally by hand.

.git/config:

	[filter "tabs"]
		clean = tabs_filter clean %f
		smudge = tabs_filter smudge %f

.git/info/attributes or .gitattributes:

	*.py filter=tabs
	*.tac filter=tabs

Not sure why people have such strong opinions on that trivial matter, but I find
it easier never to mention that I use such script ;)

##### golang_filter

Same idea as in "tabs_filter", but on a larger scale - basically does to
[Go](http://golang.org/) what
[coffee-script](http://jashkenas.github.com/coffee-script/) does to the syntax
of javascript - drops all the unnecessary brace-cancer, with the ability to
restore original perfectly ("diff -u reverse original" is checked upon
transformation to make sure of that), as long as code intentation is correct.

.git/config:

	[filter "golang"]
		clean = golang_filter git-clean %f
		smudge = golang_filter git-smudge %f

.git/info/attributes or .gitattributes:

	*.go filter=golang

Again, ideally no one should even notice that I actually don't have that crap in
the editor, while repo and compiler will see the proper (bloated) code.

##### distribute_regen

Tool to auto-update python package metadata in setup.py and README files.

Uses python ast module to parse setup.py to find "version" keyword there and
update it (via simple regex replacement, not sure if ast can be converted back
to code properly), based on date and current git revision number, producing
something like "12.04.58" (year.month.revision-since-month-start).

Also generates (and checks with docutils afterwards) README.txt (ReST) from
README.md (Markdown) with pandoc, if both are present and there's no README or
README.rst.

Designed to be used from pre-commit hook, like `ln -s /path/to/distribute_regen
.git/hooks/pre-commit`, to update version number before every commit.

##### darcs_bundle_to_diff

Ad-hoc tool to dissect and convert darcs bundles into a sequence of unified diff
hunks. Handles file creations and all sorts of updates, but probably not moves
and removals, which were outside my use-case at the moment.

Was written for just one occasion (re-working old bundles attached to tahoe-lafs
tickets, which crashed darcs on "darcs apply"), so might be incomplete and a bit
out-of-date, but I imagine it shouldn't take much effort to make it work with
any other bundles.


### Misc

##### systemd-dashboard

[There's a post](http://blog.fraggod.net/2011/2/Dashboard-for-enabled-services-in-systemd)
with more details.

	root@damnation:~# systemd-dashboard -h
	usage: systemd-dashboard [-h] [-s] [-u] [-n] [-x]

	Tool to compare the set of enabled systemd services against currently running
	ones. If started without parameters, it'll just show all the enabled services
	that should be running (Type != oneshot) yet for some reason they aren't.

	optional arguments:
	  -h, --help            show this help message and exit
	  -s, --status          Show status report on found services.
	  -u, --unknown         Show enabled but unknown (not loaded) services.
	  -n, --not-enabled     Show list of services that are running but are not
	                        enabled directly.
	  -x, --systemd-internals
	                        Dont exclude systemd internal services from the
	                        output.

	root@damnation:~# systemd-dashboard
	smartd.service
	systemd-readahead-replay.service
	apache.service

##### at

Replacement for standard unix'ish "atd" daemon in the form of a bash script.

It just forks out and waits for however long it needs before executing the given
command. Unlike with atd, such tasks won't survive reboot, obviously.

	Usage: ./at [ -h | -v ] when < sh_script
	With -v flag ./at mails script output if it's not empty even if exit code is zero.

##### mail

Simple bash wrapper for sendmail command, generating From/Date headers and
stuff, just like mailx would do, but also allowing to pass custom headers
(useful for filtering error reports by-source), which some implementations of
"mail" fail to do.

##### passgen

Uses adict english dictionaly to generate easy-to-remember passphrase.
Should be weak if bruteforce attack picks words instead of individual lettters.

##### ssh_tunnel

Script to keep persistent, unique and reasonably responsive ssh tunnel.

##### task

Wrapper scripts to run stuff from cron:

* Introducing controllable random delays (so the same crontab line on multiple
  servers won't be ran in sync, introducing unnecessary load spikes on any
  shared resources).
* Adding syslog entries about jobs' start/stop/delay.
* Running job from a oneshot systemd service, to enforce any arbitrary cgroup
  limits via unit file, dependencies and prevent parallel execution.

##### exec_notify

Wrapper to run specified command and notify (via
[desktop-notifications](http://developer.gnome.org/notification-spec/) only atm)
if it fails (including "no such binary" errors) or produces any stderr.
Optionally produces notification in any case.

Useful mainly for wrapping hooks in desktop apps like firefox, to know if click
on some "magnet:..." link was successfully processed or discarded.

	% exec_notify -h --
	usage: exec_notify: [ options... -- ] command [ arguments... ]

	Wrapper for command execution results notification.

	optional arguments:
	  -h, --help            show this help message and exit
	  -e, --exit-code-only  Issue notification only if exit code not equals zero,
	                        despite stderr.
	  -v, --notify-on-success
	                        Issue notification upon successful execution as well.
	  -d, --dump            Include stdou/stderr for all notifications.

##### urlparse

Simple script to parse long URL with lots of parameters, decode and print it out
in an easily readable ordered YAML format or diff (that is, just using "diff"
command on two outputs) with another URL.

No more squinting at some huge incomprehensible ecommerce URLs before scraping
the hell out of them!


### Desktop

Helpers for more interactive (client) machine and/or DE.

##### uri_handlers

Scripts to delegate downloads from firefox to a more sensible download managers.

Mostly I use remote mldonkey for ed2k and regular http downloads and rtorrent /
transmission for bittorrent (with some processing of .torrent files to drop
long-dead trackers from there and flatten tracker tiers, for reasons I blogged
about in some distant past).

##### pick_tracks

A simple tool to randomly pick and copy files (intended usage is music tracks)
from source to destination.

Difference from "cp" is that it will stop when destination will be filled (to
the configurable --min-df threshold) and will pick files in arbitrary order from
arbitrary path hierarchy.

Use-case is simple - insert an SD card from a player and do:

	% mount /mnt/sd_card
	% rm -rf /mnt/sd_card/music
	% pick_tracks -s 200 /mnt/music/OverClocked_Remix /mnt/sd_card/music
	INFO:root:Done: 1673.1 MiB, rate: 1.29 MiB/s

"--debug" also keeps track of what's being done and calculates how much time is
left based on df-goal and median rate.

Source dir has like [3k files](http://ocremix.org/torrents/) in many dirs, and
cp/rsync will do the dumb "we'll copy same first things every time", while this
tool will create the dst path for you, copy always-new selection there and - due
to "-s 200" - leave 200 MiB there for podcasts you might want to also upload.

As with "cp", "pick_tracks /path1 /path2 /dst" is perfectly valid.

And there are neat cleaup flags for cases when I need to cram something new to
the destination, preserving as much of the stuff that's already there as
possible (and removing least important stuff).
Cleanup (if requested) also picks stuff at random up to necessary df.

Uses [plumbum](http://plumbum.readthedocs.org) to call "rsync --inplace" (faster
than "cp" in most cases) and "find" to do the actual copy/listing.

##### link

ssh wrapper, to save time on typing something like `exec ssh -X -A -p3542
root@1.2.3.4 'screen -DR'`, especially for N remote hosts.

Also has the ability to "keep trying to connect", useful (to me, at least) for
crappy shared-hosting servers, where botnets flood ssh with slowloris-like
attacks on it's authentication, exceeding limit on unauthorized connections in
sshd.

##### power_alarm

Script to spam sounds and desktop-notifications upon detecting low battery
level.
Not the only one to do somethng like that on my system, but saved me some work
on many occasions.

##### skype_notify

Script to make Skype use desktop-notification protocol.

* open Skype
* open the menu and press 'Options' or press Ctrl-O
* go to 'Notifications' tab
* hit the 'Advanced' button and check 'Execute the following script on _any_ event'
* paste: skype_notify "%type" "%sname" "%fname" "%fpath" "%smessage" "%fsize" "%sskype"
* disable or enable the notifications you want to receive.

Imported from [this gist](https://gist.github.com/1958564).

##### logtail_notify

Script to watch log files (as many as necessary) for changes with inotify and
report any new lines appearing there via desktop notifications.

Can remember last position in file either by recording it in file's xattrs or in
a shelve db (specified via -x option).
Doesn't do much with it by default though, starting to read files from the end,
but that can be fixed by passing --keep-pos.

Somewhat advanced usage example:

	logtail_notify\
	  -i ~/media/appz/icons/biohazard_48x.png\
	  -x "$XDG_RUNTIME_DIR"/logtail_notify.db\
	  /var/log/messages

##### firefox_tgm2_tool

Tool to work with data (list of tabs, basically) stored by Firefox
TabGroupManager (2+) extension in firefox profile dir.

Default acton is to produce [pretty ordered YAML](https://github.com/mk-fg/pretty-yaml)
of group-tabname-url structure, which I backup on a daily basis, if only to
remember which tabs I've looked at, related to events of some specific day
(e.g. remember seeing useful thing while implementing X - look up time of
X-related commits in git-log and see which tabs were open at the time).

Also helpful if other people tend to use your browser, closing and overriding
(potentially useful) tabs at random.

##### e_config_backup

[Yapps2](https://github.com/mk-fg/yapps)-based (grammar as-is in *.g file)
parser script for Enlightenment (E17) config file (doing eet-decoding
beforehand) for the purposes of it's backup [in
git](https://github.com/mk-fg/de-setup) alongside other DE-related
configuration.

Whole purpose of decoding/encoding dance is to sort the sections (which E orders
at random) and detect/filter-out irrelevant changes like remembered window
positions or current ([auto-rotated](http://desktop-aura.sourceforge.net/))
wallpaper path.


### UFS

A few tools to work with a unionfs-based distributed filesystem.

Basically it's a few dumb nfs shares on different hosts, some of them being a
replicas, maintained by csync2, unified via some sort of unionfs.

So far, seem to be the simpliest and by far the most robust, predictable and
reliable distributed filesystem configuration for my simple use-case.
