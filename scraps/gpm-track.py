#!/usr/bin/env python3

import os, sys, mmap, time
import signal, subprocess, contextlib, collections

def retries_within_timeout( tries, timeout,
		backoff_func=lambda e,n: ((e**n-1)/e), slack=1e-2 ):
	'Return list of delays to make exactly n tires within timeout, with backoff_func.'
	a, b = 0, timeout
	while True:
		m = (a + b) / 2
		delays = list(backoff_func(m, n) for n in range(tries))
		error = sum(delays) - timeout
		if abs(error) < slack: return delays
		elif error > 0: b = m
		else: a = m

class GPMError(Exception): pass

def main(args=None):
	import argparse
	parser = argparse.ArgumentParser(
		description='Track and print mouse events from gpm.')
	parser.add_argument('tty', help='TTY name without /dev prefix (example: tty3).')
	parser.add_argument('-i', '--interval',
		metavar='seconds', default=1.0, type=float,
		help='Interval between reading x/y position. Default: %(default)s')
	parser.add_argument('-s', '--shm', metavar='name',
		help='/dev/shm name to use for same gpm-track option (empty: do not pass).')
	parser.add_argument('-b', '--gpm-track-binary',
		metavar='path', default='./gpm-track',
		help='Path to compiled gpm-track binary Default: %(default)s.')
	opts = parser.parse_args(sys.argv[1:] if args is None else args)

	cmd = [opts.gpm_track_binary, '--pid', str(os.getpid())]
	if opts.shm: cmd += ['--shm', opts.shm]

	for sig in range(40, 57): signal.signal(sig, signal.SIG_IGN)

	with contextlib.ExitStack() as ctx:
		tty = ctx.enter_context(open(f'/dev/{opts.tty}', 'rb'))
		proc = ctx.enter_context(subprocess.Popen(cmd, stdin=tty))
		ctx.callback(proc.terminate)

		shm = '/dev/shm/{}'.format(opts.shm or f'gpm-track.{proc.pid}')
		for delay in retries_within_timeout(10, 5):
			if os.path.exists(shm): break
			try: err = proc.wait(delay)
			except subprocess.TimeoutExpired: pass
			else: raise GPMError(f'gpm-track pid crashed (code={err})')
		else: raise GPMError(f'gpm-track pid failed to create shm file: {shm}')

		fd = os.open(shm, os.O_RDWR | os.O_SYNC)
		ctx.callback(os.close, fd)
		mem = mmap.mmap( fd, mmap.PAGESIZE,
			mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=0 )

		pos_t = collections.namedtuple('pos', 'x y')
		def read_pos():
			mem.seek(0)
			pos = mem.read(12)
			if not pos.endswith(b'\n'): return
			return pos_t(*map(int, pos.decode().split()))

		click_report = None
		click_t = collections.namedtuple('click', 't btn pos')
		def read_click( sig, frm,
				_button=['none', 'left', 'right', 'middle'],
				_click_t=['none', 'single', 'double', 'triple'] ):
			nonlocal click_report
			sig -= 40
			click_report = click_t(
				_click_t[(sig >> 2) & 3],
				_button[sig & 3], read_pos() )
		for sig in range(40, 57): signal.signal(sig, read_click)

		pos_last, delay = None, opts.interval
		while True:
			if click_report:
				print('click', click_report)
				click_report = None
			else:
				pos = read_pos()
				if pos != pos_last:
					pos_last = pos
					print('pos:', pos)
			time.sleep(delay)

if __name__ == '__main__': sys.exit(main())
