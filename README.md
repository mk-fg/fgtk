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

##### fs

Complex tool for high-level fs operations. Reference is built-in.

Copy files, setting mode and ownership for the destination:

	fs -m600 -o root:wheel cp * /somepath

Temporarily (1hr) change attributes (i.e. to edit file from user's editor):

	fs -t3600 -m600 -o someuser expose /path/to/file

Copy ownership/mode from one file to another:

	fs cps /file1 /file2

##### at

Replacement for standard unix'ish "atd" daemon in the form of a bash script.

It just forks out and waits for however long it needs before executing the given
command. Unlike with atd, such tasks won't survive reboot, obviously.

	Usage: ./at [ -h | -v ] when < sh_script
	With -v flag ./at mails script output if it's not empty even if exit code is zero.

##### color

Outputs terminal color sequences, making important output more distinctive.

Also can be used to interleave "tail -f" of several logfiles in the same terminal:

	t -f /var/log/app1.log | color red - &
	t -f /var/log/app2.log | color green - &
	t -f /var/log/app2.log | color blue - &


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


### Desktop

Helpers for more interactive (client) machine and/or DE.

##### uri_handlers

Scripts to delegate downloads from firefox to a more sensible download managers.

Mostly I use remote mldonkey for ed2k and regular http downloads and rtorrent /
transmission for bittorrent (with some processing of .torrent files to drop
long-dead trackers from there and flatten tracker tiers, for reasons I blogged
about in some distant past).

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
