#!/usr/bin/env python
from __future__ import print_function

import itertools as it, operator as op, functools as ft
from contextlib import contextmanager
from tempfile import NamedTemporaryFile
from collections import defaultdict
from fgc import sh
import os, sys, stat, types

from os.path import (
	dirname, basename, abspath, normpath, exists,
	isdir, isfile, islink )
join = sh.join


def opts_parse_uid(spec):
	uid = gid = mode = None
	if not spec: return uid, gid, mode
	try: uid, spec = spec.split(':', 1)
	except ValueError: uid, spec = spec, None
	if uid:
		try: uid = int(uid)
		except ValueError:
			import pwd
			uid = pwd.getpwnam(uid).pw_uid
	else: uid = None
	if spec is None: return uid, gid, mode
	try: gid, spec = spec.split(':', 1)
	except ValueError: gid, spec = spec, None
	if not gid:
		if uid is not None: # "user:" spec
			import pwd
			gid = pwd.getpwuid(uid).pw_gid
		else: gid = None
	else: gid = opts_parse_gid(gid)
	if spec: mode = opts_parse_mode(spec)
	return uid, gid, mode

def opts_parse_gid(spec):
	try: gid = int(spec)
	except ValueError:
		import grp
		gid = grp.getgrnam(spec).gr_gid
	return gid

def opts_parse_mode(spec):
	'''Parses either octal int mode or something like "rwxr-x---".

		String notations can be 9-letters, 3-letters long or anything in-between.
		Simple 9-letter version can have "rwx" or "-" in proper places, e.g. "rwxr-x---".
			It also supports capital R/W/X letters to set bit only
				if any r/w/x bits were set on the path (i.e. 0700 + rwXr-Xr-X = 0755).
			Lowercase u/g/o letters can be used there to copy bit from
				user/group/others perms part of the source path (i.e. 0700 + rwxuuur-X = 0775).
		Any triplet of letters in such "full" notation can be replaced by an octal digit (e.g. 75ooo).
		3-letter non-octal notation is just a simplification of e.g. "75ooo" above to "75o".

		Returns function to return new mode value, given the old one.'''
	if spec == 'help':
		raise ValueError(opts_parse_mode.__doc__.replace('\t', '  '))

	bits, masks = list(), list(
		(t, rwx, getattr(stat, 'S_I{}{}'.format(rwx, t)))
		for t in ['USR', 'GRP', 'OTH'] for rwx in 'RWX' )

	try:
		mode_oct = int(spec, 8)
		if mode_oct > 0777: raise ValueError()

	except ValueError:
		spec, spec_short = list(spec), len(spec) == 3
		for pos, n in enumerate(spec):
			if n.isdigit():
				n = int(n)
				if n > 7: continue
				spec[pos] = ''.join((rwx.lower() if m & n else '-') for t, rwx, m in masks[-3:])
			elif spec_short and n in 'ugo': spec[pos] = n*3
		spec = ''.join(spec)

		if len(spec) != 9:
			raise ValueError( 'Mode spec should be either octal lesser than 0777,'
				' 9-char string (with possible octal substs), specified: {!r}'.format(spec) )
		for pos, (t, rwx, m) in enumerate(masks):
			c = spec[pos]
			if c == rwx.lower(): f = lambda n,v=m: v
			elif c == rwx: f = lambda n,v=m: n & v
			elif c == '-': f = lambda n: 0
			elif c in 'ugo':
				shift = 'ugo'.find(c)
				mn = getattr(stat, 'S_I{}{}'.format(rwx, ['USR', 'GRP', 'OTH'][shift]))
				shift = lambda m,s=(pos // 3) - shift: (m >> (3*s)) if s > 0 else (m << (3*-s))
				assert shift(mn) == m, [mn, m]
				f = lambda n,v=mn,s=shift: s(n & v)
			else: raise ValueError('Invalid mode spec char: {!r} (spec: {!r})'.format(c, spec))
			bits.append(f)

	else:
		for t, rwx, m in masks:
			v = mode_oct & m
			bits.append(lambda n,v=v: v)

	return lambda mode:\
		sum(f(mode) for f, (t, rwx, m) in zip(bits, masks))


class FSOps(object):

	supported = 'mv', 'cp', 'ch', 'case', 'ln'

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

	def opts_walk_paths(self, paths=None, recursive=None, follow_links=False, **walk_kws):
		if paths is None: paths = self.opts.paths
		if recursive is None: recursive = self.opts.recursive
		if isinstance(paths, types.StringTypes): paths = [paths]
		if recursive:
			return it.chain.from_iterable(
				sh.walk(p, follow_links=follow_links, **walk_kws) for p in paths )
		else: return paths


	def mv(self):
		opts, ops = self.opts, self.opts_flow_parse()
		if not opts.relocate: mv_func = sh.mv
		else:
			def mv_func(src, dst, attrs):
				# Goal here is to create link in place of a file/dir asap
				tmp = None
				try: os.rename(src, dst)
				except OSError:
					if not stat.S_ISDIR(os.lstat(src).st_mode):
						sh.cp_d(src, dst, attrs=attrs, dereference=False)
						os.unlink(src)
					else:
						# src is a dir, so copy it (to dst) first, then quickly
						#  rename to a tmp name, creating link in the old place,
						#  and then proceed with (possibly quite slow) removal
						sh.cp_r( src, dst, dereference=False,
							attrs=attrs, atom=ft.partial(sh.cp_d, skip_ts=False) )
						tmp = NamedTemporaryFile( dir=dirname(src),
							prefix='{}.'.format(basename(src)), delete=False )
						os.unlink(tmp.name) # only purpose of tmpfile here is to claim unique name
						os.rename(src, tmp.name)
				sh.ln((abspath(dst) if not opts.relative else sh.relpath(dst, src)), src)
				if tmp: sh.rr(tmp.name, onerror=False)

		# attrs is implied if uid=0, otherwise disabled, unless specified explicitly in any way
		attrs = opts.attrs if opts.attrs is not None else (os.getuid() == 0)

		for src, dst in ops:
			dst = sh.join(dst, basename(src)) if sh.isdir(dst) else dst
			mv_func(src, dst, attrs=attrs)
			if opts.new_owner:
				dst_stat = os.stat(dirname(dst))
				sh.chown( dst,
					dst_stat.st_uid, dst_stat.st_gid,
					recursive=True, dereference=False )
			if opts.ch: self.ch_sub(dst)


	def cp(self):
		opts, ops = self.opts, self.opts_flow_parse()

		cp_func = ft.partial(sh.cp, sync=opts.sync, skip_ts=opts.update_times)\
			if not opts.recursive else\
			ft.partial( sh.cp_r, onerror=None,
				atom=ft.partial(sh.cp_d, sync=opts.sync, skip_ts=opts.update_times) )

		# attrs is implied if uid=0, otherwise disabled, unless specified explicitly in any way
		attrs = opts.attrs if opts.attrs is not None else (os.getuid() == 0)

		for src, dst in ops:
			dst = sh.join(dst, basename(src)) if sh.isdir(dst) else dst
			cp_func(src, dst, attrs=attrs, dereference=opts.dereference)
			if opts.new_owner:
				dst_stat = os.stat(dirname(dst))
				sh.chown( dst,
					dst_stat.st_uid, dst_stat.st_gid,
					recursive=True, dereference=False )
			if opts.ch: self.ch_sub(dst)


	def ch(self):
		opts = self.opts
		if opts.new_owner:
			for path in opts.paths:
				path_stat = os.stat(dirname(path))
				sh.chown( path,
					path_stat.st_uid, path_stat.st_gid,
					recursive=opts.recursive, dereference=False )
		self.ch_sub(opts.paths, opts.recursive)

	def ch_sub(self, paths, recursive=True):
		opts = self.opts
		for path in self.opts_walk_paths(paths, recursive=bool(recursive)):
			path_stat, nid = os.lstat(path), lambda n: -1 if n is None else n
			if (opts.uid is not None and path_stat.st_uid != opts.uid)\
					or (opts.gid is not None and path_stat.st_gid != opts.gid):
				os.lchown(path, nid(opts.uid), nid(opts.gid))
			if opts.mode:
				mode = opts.mode(stat.S_IMODE(path_stat.st_mode))
				assert mode < 0777, oct(mode)
				sh.chmod(path, mode, dereference=False)


	def case(self):
		opts = self.opts
		act = sum(map(bool, [opts.check, opts.lower, opts.upper]))
		if act > 1:
			opts.error('Only one case-changing action (or check) should be specified.')
		elif act == 0:
			opts.error('At least one case-changing action or check should be specified.')

		try: enc, err = opts.encoding.split('/', 1)
		except IndexError: enc, err = opts.encoding, 'strict'
		dec = lambda b,enc=enc,err=err: b.decode(enc, err)
		enc = lambda b,enc=enc: b.encode(enc)

		if opts.check or opts.lower: cc = op.methodcaller('lower')
		elif opts.upper: cc = op.methodcaller('upper')
		cc = lambda n,cc=cc: enc(cc(dec(n)))

		conflicts = False

		if opts.check:
			paths = defaultdict(dict)
			for path in self.opts_walk_paths():
				name, names = cc(basename(path)), paths[dirname(path)]
				if name in names:
					print('{} {}'.format(names[name], path))
					conflicts = True
				else: names[name] = path

		else:
			for path in self.opts_walk_paths(depth=True):
				name, path_dir = basename(path), dirname(path)
				name_new = cc(name)
				path_new = join(path_dir, name_new)
				path, path_new = map(normpath, [path, path_new])
				if path == path_new: continue
				if exists(path_new):
					log.warn('Same-case path conflict in directory %r: %r vs %r', path_dir, name, name_new)
					conflicts = True
					if opts.force: sh.rr(path_new)
					else: continue
				os.rename(path, path_new)

		return int(conflicts)


	def ln(self):
		opts, ops = self.opts, self.opts_flow_parse()

		rec = bool(opts.recursive_files)
		rec_links = rec and opts.recursive_files_follow_links

		for src, dst in ops:
			src = abspath(src).rstrip(os.sep)
			if not isdir(dst):
				if len(ops) > 1: opts.error('Multiple src paths provided with non-dir dst: {}'.format(dst))
				if exists(dst): opts.error('Refusing to overwrite non-dir dst path: {}'.format(dst))
			else: dst = join(dst, basename(src))
			dst = dst.rstrip(os.sep)

			for p in self.opts_walk_paths(src, recursive=rec, follow_links=rec_links, depth=False):
				if not rec:
					if opts.relative: p = sh.relpath(p, dst)
					os.symlink(p, dst)
					continue

				p = abspath(p)
				assert p.startswith(src), [src, p]
				p_rel = join('.', p[len(src)+1:]).rstrip(os.sep)

				p_src, p_dst = p, join(dst, p_rel)
				if isdir(p) and not (not rec_links and islink(p)): os.makedirs(p_dst)
				else:
					p_rel_in_tree = False
					if islink(p): # copy in-tree-relative symlinks
						p_rel_in_tree = os.readlink(p)
						p_ln_abs = abspath(join(dirname(p), p_rel_in_tree))
						if p_ln_abs.startswith(src) and p_ln_abs[len(src)] == os.sep:
							p_src, p_rel_in_tree = p_rel_in_tree, True
						else: p_rel_in_tree = False
					if not p_rel_in_tree and opts.relative: p_src = sh.relpath(p, p_dst)
					os.symlink(p_src, p_dst)
					# assert os.path.samefile(p, p_dst), [p, p_src, p_dst] # sanity check


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
		kwz.setdefault('usage', 'fs {} [opts] [[--] args]'.format(name))
		cmd = cmds.add_parser(name, **kwz)
		cmd.set_defaults(call=name)
		yield cmd

	## Command-specific options

	with subcommand('mv', help='Recursively move path(s).') as cmd:
		cmd.add_argument('src/dst', nargs='*',
			help='Files/dirs to move. If neither of --src / --dst option is'
				' specified, last argument will be treated as destination.')
		cmd.add_argument('-t', '--relocate', action='store_true',
			help='Leave symlinks in place of the original paths.')
		cmd.add_argument('-r', '--relative', action='store_true',
			help='Create symlinks to relative paths, only makes sense with --relocate.')

	with subcommand('cp', help='Recursively copy path(s).') as cmd:
		cmd.add_argument('src/dst', nargs='*',
			help='Files/dirs to copy. If neither of --src / --dst option is'
				' specified, last argument will be treated as destination.')
		cmd.add_argument('-l', '--no-dereference',
			dest='dereference', action='store_false', default=True,
			help='Copy symlinks as files/dirs they point to.')
		cmd.add_argument('--sync', action='store_true',
			help='Do fsync() before closing destination files.')
		cmd.add_argument('--update-times', action='store_true',
			help='Do not preserve timestamps, which are preserved by default.')

	with subcommand('ch', help='Change attributes of a specified path(s).') as cmd:
		cmd.add_argument('paths', nargs='+', help='Paths to operate on.')
		cmd.add_argument('-r', '--recursive', action='store_true', help='Recursive operation.')

	with subcommand('case',
			help='Operations with path(s) case. Recursive by default.') as cmd:
		cmd.add_argument('paths', nargs='+', help='Paths to operate on.')
		cmd.add_argument('-f', '--force', action='store_true',
			help='Proceed with renaming even if there'
				' are conflicts (some files will be lost in this case!).')
		cmd.add_argument('-e', '--encoding',
			default='utf-8/strict', metavar='encoding[/errors]',
			help='Decode paths using specified encoding'
					' before upper/lowercasing them (default: %(default)s).'
				' Required if operation is to be applied to non-utf-8 paths properly.'
				' Decoding error handling behavior (one of "strict",'
					' "replace" or "ignore") can be specified after a slash.')
		cmd.add_argument('-c', '--check', action='store_true',
			help='Check if same-cased paths will collide, report collisions.'
				' Exits with non-zero status if there are any conflicts.'
				' Mutually exclusive with other case-altering actions.')
		cmd.add_argument('-l', '--lower', action='store_true',
			help='Make all paths lowercase, reporting'
					' conflicts (and leaving both paths intact), if any.'
				' Mutually exclusive with other case-altering actions.')
		cmd.add_argument('-u', '--upper', action='store_true',
			help='Make all paths uppercase, reporting'
					' conflicts (and leaving both paths intact), if any.'
				' Mutually exclusive with other case-altering actions.')

	with subcommand('ln', help='Symlink path(s).') as cmd:
		cmd.add_argument('src/dst', nargs='*',
			help='Files/dirs to symlink. If neither of --src / --dst option is'
				' specified, last argument will be treated as destination.')

		cmd.add_argument('-f', '--recursive-files', action='store_true',
			help='Re-create directories in dst, symlinking all files'
				' (and any other non-recursable nodes) into those.')
		cmd.add_argument('-l', '--recursive-files-follow-links', action='store_true',
			help='Follow symlinks to other directories within src hierarchy.'
				' Can potentially lead to infinite loops and fill fs, use with care.')

		cmd.add_argument('--relative', action='store_true',
			help='Create symlinks to relative paths.'
				' Absolute ones (but not realpaths!) are used by default.')

	## More generic opts

	for cmd in op.itemgetter('cp', 'case')(cmds.choices):
		cmd.add_argument('-1', '--non-recursive',
			dest='recursive', action='store_false', default=True,
			help='Non-recursive operation.')

	for cmd in op.itemgetter('mv', 'cp', 'ln')(cmds.choices):
		cmd.add_argument('-s', '--src', dest='src_opt', metavar='SRC',
			help='Source, positional argz will be treated as destination(s).')
		cmd.add_argument('-d', '--dst', dest='dst_opt', metavar='DST',
			help='Destination, positional argz will be treated as source(s).')
		cmd.add_argument('--reverse',
			action='store_true', help='Reverse source / destination targets.')

	for cmd in op.itemgetter('mv', 'cp')(cmds.choices):
		cmd.add_argument('-P', '--attrs', action='store_true',
			help='Force preserving fs metadata:'
				' uid/gid and timestamps, implied for some ops (which doesnt'
				' make sense w/o it, like cps), and conditionally implied for'
				' others (like mv), if user is root.')
		cmd.add_argument('-N', '--no-priv-attrs', action='store_true',
			help='Inhibit fs metadata copying (direct uid/gid/whatever setting will'
				' still work as requested) ops which may require elevated privileges.')

	for cmd in op.itemgetter('mv', 'cp', 'ch')(cmds.choices):
		cmd.add_argument('-u', '--uid',
			metavar='{uname|uid}[:[{gname|gid}][:][mode]]',
			help='Set owner user and group/mode (if specified,'
					' both can be specified as ids) for destination path(s).'
				' If group/gid part is omitted, specified users (if any) gid will be used.'
				' Specs like "::644" (same as "--mode 644"), "user:" (gid from user entry)'
					' or "1000:0:64o" (numeric uid/gid, any valid mode spec) are all fine.'
				' Overrides --attrs.')
		cmd.add_argument('-g', '--gid',
			metavar='{gname|gid}',
			help='Set gid/group for destination path(s).'
				' Overrides --uid (if also specified there), --attrs.')
		cmd.add_argument('-m', '--mode', metavar='mode',
			help='Octal or symbolic mode to set for destination path(s).'
				' Run "--mode help" to get info on syntax for symbolic mode specs.'
				' Overrides --attrs.')
		cmd.add_argument('-o', '--new-owner', action='store_true',
			help='Chown destination path according uid/gid of the dir it is being moved to.'
				' Overrides --attrs, --no-priv-attrs. Precedes --uid, --gid, --mode.')
		cmd.set_defaults(ch=True)

	opts = parser.parse_args(sys.argv[1:] if args is None else args)


	def opts_get(k, def_v=None, not_v=object()):
		val = getattr(opts, k, not_v)
		if val is not not_v: return val
		return def_v

	global log
	import logging
	logging.basicConfig(level=logging.DEBUG if opts.debug else logging.WARNING)
	log = logging.getLogger()


	## Post-process options
	opts.error = parser.error
	opts.pos = opts_get('src/dst')
	if any(map(opts_get, ['new_owner', 'no_priv_attrs'])): opts.attrs = False
	if opts_get('ch'):
		try:
			uid, gid, mode = opts_parse_uid(opts_get('uid'))
			if opts_get('gid'): gid = opts_parse_gid(opts_get('gid'))
			if opts_get('mode'): mode = opts_parse_mode(opts_get('mode'))
		except ValueError as err:
			opts.error(err.message)
		opts.uid, opts.gid, opts.mode = uid, gid, mode
		if all(map(lambda n: n is None, [uid, gid, mode])): opts.ch = False


	# Run
	ops = FSOps(opts)
	return ops[opts.call]()


if __name__ == '__main__': sys.exit(main())
