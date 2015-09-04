#!/bin/bash

usage() {
	bin=$(basename $0)
	echo >&2 "Usage: $bin machine"
	exit ${1:-0}
}
[[ -z "$1" || "$1" = -h || "$1" = --help ]] && usage


set -e -o pipefail

ns_init_pid=( $( ps -eo pid,machine,comm,args |
	awk '$2=="'"$1"'" && $3=="systemd" && $5!="--user" {print $1}' ) )

[[ ${#ns_init_pid[@]} -eq 1 ]] || {
	echo >&2 "Failed to uniquely identify systemd pid for specified machine: $1"
	echo >&2 "pids found (${#ns_init_pid[@]}): ${ns_init_pid[@]}"
	echo >&2
	echo >&2 "Full list of processes for the machine:"
	ps -eo pid,machine,args | awk '$2=="'"$1"'"'
	exit 1
}

exec nsenter --target "${ns_init_pid[0]}" --mount --uts --ipc --net --pid /bin/su -
