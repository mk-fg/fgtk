#!/usr/bin/env python
from __future__ import print_function

import itertools as it, operator as op, functools as ft
from contextlib import contextmanager
from tempfile import NamedTemporaryFile
from os.path import dirname, basename, abspath
from fgc import sh
import os, sys, stat


class FSOps(object):

	supported = 'mv',

	def __init__(self, opts):
		self.opts = opts

	def __getitem__(self, k):
		if k not in self.supported: raise KeyError(k)
		return getattr(self, k)

	def opts_flow_parse(self):
		opts = self.opts
		if opts.src_opt:
			ops = zip(it.repeat(opts.src_opt), (opts.pos if not opts.dst_opt else [opts.dst_opt]))
		elif opts.dst_opt: ops = zip(opts.pos, it.repeat(opts.dst_opt))
		elif len(opts.pos) < 2:
			opts.error('Need at least two positional arguments or --src / --dst specification.')
		else: ops = zip(opts.pos[:-1], it.repeat(opts.pos[-1]))
		return ops if not opts.reverse else list((dst,src) for src,dst in ops)

	def mv(self):
		opts, ops = self.opts, self.opts_flow_parse()
		if not opts.relocate: mv_func = sh.mv
		else: # goal here is to create link in place of a file/dir asap
			def mv_func(src, dst, attrs):
				tmp = None
				try: os.rename(src, dst)
				except OSError:
					if not stat.S_ISDIR(os.lstat(src).st_mode):
						sh.cp_d(src, dst, attrs=attrs, dereference=False)
						os.unlink(src)
					else:
						sh.cp_r( src, dst, dereference=False,
							attrs=attrs, atom=ft.partial(sh.cp_d, skip_ts=False) )
						tmp = NamedTemporaryFile( dir=dirname(src),
							prefix='{}.'.format(basename(src)), delete=False )
						os.rename(src, tmp.name)
				sh.ln((abspath(dst) if not opts.relative else sh.relpath(dst, src)), src)
				if tmp: sh.rr(tmp.name, onerror=False)

		# attrs is implied if uid=0, otherwise disabled, unless specified explicitly in any way
		attrs = opts.attrs if opts.attrs is not None else not bool(os.getuid())

		for src,dst in ops:
			mv_func(src, sh.join(dst, basename(src)) if sh.isdir(dst) else dst, attrs=attrs)


def main(args=None):
	import argparse
	parser = argparse.ArgumentParser(
		description='Advanced/batched filesystem manipulation tool.')

	# XXX: implement/enable --dry-run
	# parser.add_argument('-n', '--dry-run', '--pretend',
	# 	action='store_true', help='Do not actually change anything on fs.')
	parser.add_argument('--debug', action='store_true', help='Verbose operation mode.')

	cmds = parser.add_subparsers(title='supported operations', help='Subcommand info.')

	@contextmanager
	def subcommand(name, **kwz):
		cmd = cmds.add_parser(name, **kwz)
		cmd.set_defaults(call=name)
		yield cmd

	# Command-specific options
	with subcommand('mv', help='Recursively move path(s).') as cmd:
		cmd.add_argument('src/dst', nargs='*',
			help='Files/dirs to move. If neither of --src / --dst option is'
				' specified, last argument will be treated as destination.')
		cmd.add_argument('-t', '--relocate', action='store_true',
			help='Leave symlinks in place of the original paths.')
		cmd.add_argument('-r', '--relative', action='store_true',
			help='Create symlinks to relative paths, only makes sense with --relocate.')

	# More generic opts
	for cmd in [op.itemgetter('mv')(cmds.choices)]:

		cmd.add_argument('-s', '--src', dest='src_opt', metavar='SRC',
			help='Source, positional argz will be treated as destination(s).')
		cmd.add_argument('-d', '--dst', dest='dst_opt', metavar='DST',
			help='Destination, positional argz will be treated as source(s).')

		cmd.add_argument('--reverse',
			action='store_true', help='Reverse source / destination targets.')

		cmd.add_argument('-P', '--attrs', action='store_true',
			help='force preserving fs metadata:'
				' uid/gid and timestamps, implied for some ops (which doesnt'
				' make sense w/o it, like cps), and conditionally implied for'
				' others (like mv), if user is root.')
		cmd.add_argument('-N', '--no-priv-attrs', action='store_true',
			help='Inhibit fs metadata copying (direct uid/gid/whatever setting will'
				' still work as requested) ops which may require elevated privileges.')

	opts = parser.parse_args(sys.argv[1:] if args is None else args)
	opts.error = parser.error
	opts.pos = getattr(opts, 'src/dst')
	if opts.attrs is None and opts.no_priv_attrs: opts.attrs = False

	global log
	import logging
	logging.basicConfig(level=logging.DEBUG if opts.debug else logging.WARNING)
	log = logging.getLogger()

	ops = FSOps(opts)
	ops[opts.call]()

if __name__ == '__main__': sys.exit(main())
