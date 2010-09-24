#!/usr/bin/env python
# -*- coding: utf-8 -*-

from optparse import OptionParser
parser = OptionParser(usage='%prog CMD [options]')

parser.add_option('-s', '--src', metavar='PATH', action='store', type='str',
	help='source, positional argz will be treated as destination')
parser.add_option('-d', '--dst', metavar='PATH', action='store', type='str',
	help='destination, positional argz will be treated as source')
parser.add_option('-r', '--recursive', action='store_true', dest='recursive', help='recursive operation')

parser.add_option('--reverse', action='store_true', dest='reverse', help='reverse source / destination targets')
parser.add_option('--recode', metavar='ENC', action='store',
	type='str', help='recode passed content to specified encoding')

parser.add_option('-f', '--force', action='store_true',
	help='overwrite, crush and destroy everything that stands in the way')
parser.add_option('-c', '--cautious', action='store_true',
	help='stop on first error encountered (default is to keep going on non-fatal)')
parser.add_option('-p', '--pretend', action='store_true',
	help='just show what will be done, implies -v')

parser.add_option('-e', '--exclude', metavar='PATH[:PATH...]', action='store', type='str',
	help='colon-separated regexes, to match paths (relative'
		' to action root), excluded from given recursive action')

parser.add_option('-m', '--mode', metavar='OCT/STR', action='store', type='str',
	help='path mode, can be specified as nine-char string like "rwxr-xr-x"')
parser.add_option('-u', '--uid', metavar='NAME/ID[:NAME/ID]',
	action='store', type='str', dest='uid', help='user name or id')
parser.add_option('-g', '--gid', metavar='NAME/ID',
	action='store', type='str', dest='gid', help='group name or id')
parser.add_option('-o', '--ownership', metavar='PATH', action='store', type='str',
	dest='ownership', help='use the same uid/gid as on the given path; precedes uid/gid')
parser.add_option('-a', '--sample', metavar='PATH', action='store', type='str', dest='sample',
	help='use the same uid/gid and mode as on the given path; precedes all similar options')

parser.add_option('-P', '--attrz', action='store_true',
	help='force preserving fs metadata:'
		' uid/gid and timestamps, implied for some ops, like mv or cps')
parser.add_option('-N', '--no-priv-attrz', action='store_true', dest='no_priv_attrz',
	help='inhibit fs metadata copying (direct uid/gid setting will work as requested)'
		' ops which may require elevated privileges')
parser.add_option('-H', '--no-dereference', default=True,
	action='store_false', dest='dereference', help='never follow symlinks')

parser.add_option('-t', '--time', metavar='SEC', action='store', type='int',
	default=20, help='delay for timed operations (like expose), default - 20s')

parser.add_option('-v', '--verbose', action='store_true', help='output more operational data')
parser.add_option('--debug', action='store_true', help='print lots of debug info')
parser.add_option('--dump', action='store_true', help='dump available commands')
parser.add_option('--dump-args', action='store_true', dest='dump_args', help='dump available arguments')

optz, argz = parser.parse_args()


from fgc import log, sh
from string import whitespace as spaces
from time import sleep
import os, sys
if optz.debug: log_level = log.DEBUG
elif optz.verbose or optz.pretend: log_level = log.INFO
else: log_level = log.WARNING
log.cfg(level=log_level)


if optz.sample:
	sample = os.stat(optz.sample)
	optz.uid, optz.gid, optz.mode = sample.st_uid, sample.st_gid, oct(sample.st_mode)[-4:]
if optz.ownership:
	sample = os.stat(optz.ownership)
	optz.uid, optz.gid = sample.st_uid, sample.st_gid
if not optz.gid and optz.uid and ':' in optz.uid:
	optz.uid, optz.gid = optz.uid.split(':', 1)
	if not optz.gid: optz.gid = optz.uid # "user:"
if optz.exclude: optz.exclude = optz.exclude.split(':')
if optz.uid and not isinstance(optz.uid, int): optz.uid = sh.uid(optz.uid)
if optz.gid and not isinstance(optz.gid, int): optz.gid = sh.gid(optz.gid)


def throw_err(err=RuntimeError):
	if optz.cautious: raise err
	else: log.error(err)

def do(*argz, **kwz):
	if not optz.pretend: argz[0](*argz[1:], **kwz)
	else:
		if not isinstance(argz[1:], str) and len(argz[1:]) == 1: arg = argz[1]
		else: arg = argz[1:]
		if not kwz: kwz = ''
		try: fn = argz[0].func_name
		except AttributeError: fn = str(argz[0]).split(' ').pop().strip('<>')
		log.info('%s: %s %s'%(fn, arg, kwz))


def _mkdir(kwz):
	'''
	Create a directory.
	If dst is specified, then all given paths will be created as relative to it.
	'''
	for arg in argz:
		if optz.dst: arg = os.path.join(optz.dst, arg)
		if os.path.exists(arg):
			try:
				if optz.force: sh.rr(arg)
				else: raise RuntimeError, 'Destination path exists: %s'%arg
			except (sh.Error, RuntimeError), err:
				throw_err(err)
				continue
		try: do(sh.mkdir, arg, **kwz)
		except sh.Error, err: throw_err(err)

def _lc(kwz):
	'''
	Convert all node names to lowercase in a given path(s).
	Always recursive.
	'''
	abs = lambda x: os.path.join(dir, x)
	done = []
	while len(argz) > len(done):
		for dir in argz:
			if dir in done: continue
			mv = False
			for path in sh.crawl(dir, topdown=True, filter=optz.exclude):
				if not path.islower():
					mv = abs(path), abs(path.lower())
					do(sh.mv, *mv)
			if optz.pretend or not mv: done.append(dir)

def _ln(kwz):
	'''
	Create symlink(s).
	'''
	raise NotImplementedError, 'Should be able to create sym/hard links, understand -p and -r flags'
	#~ if optz.recursive:
		#~ ln_r
	#~ else:

def parse_flow():
	if optz.src:
		if not optz.dst: cps = [(optz.src, dst) for dst in argz]
		else: cps = [(optz.src, optz.dst)]
	elif optz.dst: cps = [(src, optz.dst) for src in argz]
	elif len(argz) != 2: parser.error('Need exactly two arguments or src/dst specification')
	else: cps = [(argz[0], argz[1])]
	if optz.reverse: cps = [(dst,src) for src,dst in cps]
	return cps

def parse_ids():
	if optz.uid != None or optz.gid != None:
		uid = optz.uid if optz.uid != None else -1
		gid = optz.gid if optz.gid != None else -1
		return uid, gid
	else: return None

def _cps(kwz):
	'''
	Copy node attributes to other node(s).
	'''
	kwz['skip_ts'], kwz['attrz'] = False, not optz.no_priv_attrz
	cps = parse_flow()
	for src, dst in cps:
		if not optz.recursive: do(sh.cp_stat, src, dst, **kwz)
		else:
			for node in sh.crawl(dst, topdown=False, filter=optz.exclude):
				do(sh.cp_stat, src, os.path.join(dst, node), **kwz)
			do(sh.cp_stat, src, dst, **kwz)

def _cc(kwz):
	'''
	Copy content of one (or more) file into another(s).
	If dst is specified, contents of all sources will be concatenated into it.
	'''
	cps = parse_flow()
	if not optz.src and optz.dst:
		do(open, dst, 'w') # Truncate
		kwz['append'] = True
	for src, dst in cps:
		do(sh.cp_cat, src, dst, **kwz)
		try: uid,gid = parse_ids()
		except TypeError: pass
		else: do(os.chown, dst, uid, gid)
		if optz.mode: do(os.chmod, dst, sh.mode(optz.mode))

def _cp(kwz):
	'''
	Copy file/path(s).
	'''
	cps = parse_flow()
	for src, dst in cps:
		into = os.path.isdir(dst)
		if not optz.recursive:
			if kwz.get('attrz', False) and optz.no_priv_attrz:
				kwz['skip_ts'] = kwz['attrz'] = False
			do(sh.cp, src, dst, **kwz)
		else:
			if kwz.get('attrz', False) and optz.no_priv_attrz:
				kwz['attrz'], kwz['atom'] = False, ft.partial(sh.cp_d, skip_ts=False)
			do(sh.cp_r, src, dst, **kwz)
		if into: dst = os.path.join(dst, os.path.basename(src))
		try: uid,gid = parse_ids()
		except TypeError: pass
		else:
			do(os.chown, dst, uid, gid)
			if optz.recursive:
				for path in sh.crawl(dst): do(os.chown, os.path.join(dst, path), uid, gid)
		if optz.mode:
			mode = sh.mode(optz.mode)
			do(os.chmod, dst, mode)
			if optz.recursive:
				for path in sh.crawl(dst): do(os.chmod, os.path.join(dst, path), mode)

def _mv(kwz):
	'''
	Move file/path(s).
	'''
	cps = parse_flow()
	if optz.no_priv_attrz: kwz['attrz'] = False
	for src, dst in cps:
		if os.path.isdir(dst): dst = os.path.join(dst, os.path.basename(src))
		do(sh.mv, src, dst, **kwz)
		try: uid,gid = parse_ids()
		except TypeError: pass
		else:
			do(os.chown, dst, uid, gid)
			for path in sh.crawl(dst): do(os.chown, os.path.join(dst, path), uid, gid)
		if optz.mode:
			mode = sh.mode(optz.mode)
			do(os.chmod, dst, mode)
			for path in sh.crawl(dst): do(os.chmod, os.path.join(dst, path), mode)

def _ch(kwz):
	'''
	Change path mode / ownership.
	'''
	if optz.exclude: raise NotImplementedError('Exclude is not implemented for ch yet')
	for path in argz:
		if optz.mode: do(os.chmod, path, sh.mode(optz.mode))
		try: uid,gid = parse_ids()
		except TypeError: pass
		else: do(os.chown, path, uid, gid)

def _expose(kwz):
	'''
	Temporarily alter path mode / ownership.
	'''
	global argz
	delay = kwz.pop('time')
	ownage = {}
	for path in argz:
		sample = os.stat(path)
		ownage[path] = sample.st_uid, sample.st_gid, oct(sample.st_mode)[-4:]
	for path in argz: _ch(kwz)
	if not os.fork():
		sleep(delay)
		for path, (optz.uid, optz.gid, optz.mode) in ownage.iteritems():
			argz = [path]
			_ch(kwz)
		try:
			sys.stderr.write('\n')
			log.warn('fs metadata reverted to original state')
		except IOError: pass # pipes might be broken already


meta = {
	'mkdir': {
		'mode': lambda: sh.mode(optz.mode),
		'direct': ['recursive', 'uid', 'gid'],
		'use': ['pretend', 'force', 'dst', 'ownership', 'sample']
	},
	'ch': {
		'use': ['mode', 'uid', 'gid', 'ownership', 'sample', 'pretend', 'exclude']
	},
	'cc': {
		'direct': ['recode'],
		'use': ['src', 'dst', 'reverse', 'uid', 'gid', 'ownership', 'sample', 'pretend', 'mode']
	},
	'cp': {
		'skip': optz.exclude,
		'symlinks': optz.dereference,
		'direct': ['attrz'],
		'use': [
			'no_priv_attrz',
			'recursive',
			'pretend',
			'exclude',
			'dereference',
			'src', 'dst', 'reverse',
			'uid', 'gid',
			'ownership', 'sample',
			'mode'
		]
	},
	'mv': {
		'direct': ['attrz'],
		'use': [
			'no_priv_attrz',
			'recursive',
			'pretend',
			'src', 'dst', 'reverse',
			'uid', 'gid',
			'ownership', 'sample',
			'mode'
		]
	},
	'lc': {'use': ['pretend', 'exclude']},
	'ln': {'use': ['pretend']},
	'cps': {
		'direct': ['dereference'],
		'use': ['no_priv_attrz', 'recursive', 'pretend', 'exclude', 'src', 'dst', 'reverse']
	},
	'expose': {
		'direct': ['time'],
		'use': [
			'uid', 'gid',
			'ownership', 'sample',
			'mode',
			'pretend', 'exclude'
		]
	}
}


try: cmd = argz.pop(0)
except IndexError:
	if not optz.dump and not optz.dump_args: parser.error('No cmd specified')
	else:
		if optz.dump: sys.stdout.write(' '.join([k[1:] for k in locals().keys() if k.startswith('_') and k[1:].isalnum()]))
			#~ cmdz = [
				#~ "'%s[%s]:%s:'"%(k[1:], eval(k+'.__doc__').strip(spaces).splitlines()[0].strip('.'), k[1:])
				#~ for k in locals().keys() if k.startswith('_') and k[1:].isalnum()
			#~ ]
			#~ sys.stdout.write(' '.join(cmdz))
		elif optz.dump_args:
			for opt in parser.option_list:
				opt_str = ''
				if opt._short_opts: opt_str += '(%s)'%opt._short_opts[0]
				if opt._long_opts:
					if opt._long_opts[0].startswith('--dump'): continue
					opt_str += opt._long_opts[0]
				if opt.help: opt_str += '[%s]'%opt.help
				if opt._long_opts: opt_str += ':%s:'%opt._long_opts[0].strip('-')
				sys.stdout.write(" '%s' "%opt_str)
		sys.exit()

try: opt = meta[cmd]
except KeyError: parser.error('Unknown cmd: %s'%cmd)
else:
	kwz = {}

	kmerge = lambda x: opt.update((k, k) for k in x)
	try: use = opt.pop('use') # not used, actually ;P
	except (KeyError,TypeError): use = []
	try: direct = opt.pop('direct')
	except KeyError: pass
	except TypeError:
		direct = opt
		opt = {}
		kmerge(direct)
	else: kmerge(direct)

	cmd = locals()['_'+cmd]

	if argz:
		# --- Execute cmd
		# Change process name
		from ctypes.util import find_library
		from ctypes import CDLL
		k, v = CDLL(find_library('c')), os.path.basename(sys.argv[0])
		try: k.prctl(15, v, 0, 0, 0) # linux
		except AttributeError:
			try: k.setproctitle(v) # freebsd
			except AttributeError: pass # beat it

		for k,v in opt.iteritems():
			try:
				opt = getattr(optz, k)
				if opt != None: opt = v()
				else: raise AttributeError
			except TypeError: kwz[v] = opt # '-r' -> recursive=True
			except AttributeError: pass
			else: kwz[k] = opt # 'mode': lambda: sh.mode(optz.mode)

		log.debug('Passing kwz: %s'%kwz)

		try: cmd(kwz)
		except (sh.Error, RuntimeError), err: log.fatal(err)

	else:
		# --- Just print command description
		sys.stdout.write(cmd.__doc__ or '\n\tNo information\n')
		try:
			for k in direct: kwz[k] = k
		except NameError: pass
		sys.stdout.write( '\n\tUsed parameters: %s\n\n'%(', '.join(kwz.keys() + use)) )
