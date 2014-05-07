#!/usr/bin/env python
# -*- coding: utf-8 -*-
from __future__ import print_function

import itertools as it, operator as op, functools as ft
from os.path import join, exists, isfile, isdir, expanduser
from contextlib import closing
from collections import defaultdict, OrderedDict, namedtuple
import os, sys, io, types, re, random, sqlite3, ConfigParser


class AdHocException(Exception): pass

def get_profile_dir(profile):
	if profile:
		if profile.startswith(os.sep): return profile
		profile = profile.lower() # for ci comparisons

	ff_home = expanduser('~/.mozilla/firefox')
	profiles = ConfigParser.RawConfigParser()
	profiles.read(join(ff_home, 'profiles.ini'))

	profile_path, paths = None, list()
	for k in profiles.sections():
		if not k.startswith('Profile'): continue
		name = profiles.get(k, 'Name').strip().lower()
		path = profiles.get(k, 'Path')
		if profile is not None and name == profile:
			profile_path = path
			break
		if profile:
			paths.append(path)
			continue
		if profile_path: continue
		try:
			if not profiles.getboolean(k, 'Default'): raise AdHocException
		except (ConfigParser.NoOptionError, AdHocException): pass
		else: profile_path = path
	else:
		for path in paths:
			if profile in path.lower():
				profile_path = path
				break
		assert profile_path, profile

	profile_path = join(ff_home, profile_path)
	assert isdir(profile_path), [name, profile_path]
	return profile_path


def sqlite_dict_row(cursor, row):
	row = sqlite3.Row(cursor, row)
	return dict((k, row[k]) for k in row.keys())

def bookmarks_get(db_path, timeout=30):
	assert isfile(db_path), db_path
	bms = defaultdict(lambda: dict(bm_tags=set()))

	with sqlite3.connect(db_path, timeout=timeout) as conn:
		conn.row_factory = sqlite_dict_row
		with closing(conn.cursor()) as c:
			c.execute('select * from moz_bookmarks')
			bm_data = c.fetchall()

			bms_meta = dict()
			for r in bm_data:
				t, fk = r['type'], r['fk']
				if t == 1 and r['title'] is not None:
					bm = dict(bm_title=r['title'], bm_added=r['dateAdded'], bm_folder=r['parent'])
					c.execute('select * from moz_places where id = ?', (fk,))
					p = c.fetchone()
					if p is None or p['hidden']:
						if p is None:
							log.warn('Missing moz_places entry for bookmark, ignoring: %s', r)
						bms[fk]['legit'] = False
						continue
					assert not set(bm).intersection(p), [bm, p]
					bm.update(p)
					bms[fk].update(bm)
					bms[fk].setdefault('legit', True)
				elif t == 1: bms[fk]['bm_tags'].add(r['parent']) # tag link
				elif t == 2: bms_meta[r['id']] = r['title']
				else: raise ValueError(r)

			for fk in bms.keys():
				if not bms[fk].pop('legit', False):
					del bms[fk]
					continue
				bm = bms[fk]
				# Tags
				bm_tags, tags = bm['bm_tags'], set()
				for t in bm_tags:
					try: tags.add(bms_meta[t])
					except KeyError:
						log.warn( 'Unknown tag id in'
							' bookmark-tag link, skipping: %s (bm: %s)', t, bm )
				bm_tags.clear()
				bm_tags.update(tags)
				# Path
				# XXX: subdir parents, if I'll ever use these
				try: bm['bm_folder'] = bms_meta[bm['bm_folder']]
				except KeyError:
					log.warn( 'Unknown parent folder id in'
						' bookmark-parent link, using "Unknown": %s (bm: %s)', bm['bm_folder'], bm )
					bm['bm_folder'] = 'Unknown'
				# Favicon
				# XXX: fetch missing ones maybe?
				favicon = bm.pop('favicon_id')
				if favicon:
					c.execute('select data, mime_type from moz_favicons where id = ?', (favicon,))
					favicon = c.fetchone()
				if favicon:
					bm['favicon'] = dict(
						mime_type=favicon['mime_type'],
						data=bytes(favicon['data']).encode('base64') )

	return bms


detect_link = lambda slug:\
	isinstance(slug, types.StringTypes)\
		and bool(re.search(ur'^\s*((https?|spdy|file)://|about:)', slug))

class LinkError(Exception): pass
class LinkTypeError(LinkError): pass
class LinkAmbiguity(LinkError): pass
class LinkMissing(LinkError): pass

class Link(namedtuple('Link', 'url title')):

	def __new__(cls, str1, str2=None):
		for val in str1, str2:
			if not isinstance(val, (types.StringTypes, types.NoneType, int)):
				raise LinkTypeError('Incorrect passed link/title type (%s): %s', type(val), val)
		str2, str1 = sorted([str1, str2], key=bool)
		url, title = str1, None
		if str2:
			str2, str1 = sorted([str1, str2], key=detect_link)
			if detect_link(str2):
				raise LinkAmbiguity('Two links passed instead of one: %s', [str1, str2])
			url, title = str1, str2
		if not detect_link(url):
			raise LinkMissing('Failed to find link in passed tuple: %s', [str1, str2])
		assert detect_link(url) and not detect_link(title), [url, title]
		if title: title = unicode(title)
		return super(Link, cls).__new__(cls, url, title)

def backlog_get(path):
	import yaml
	with open(path) as src: backlog = yaml.load(src)

	def process_layer(data, path=None):
		if path is None: path = list()
		if isinstance(data, types.StringTypes):
			path += ['string']
			# print('STR', repr(data)[:100])
			try: return Link(data)
			except LinkMissing:
				log.warn('Dangling string not bound to any links (path: %s): %r', path, data)
		elif isinstance(data, (list, tuple)):
			if len(data) == 2:
				# print('TUPLE', repr(data)[:100])
				try: return Link(*data)
				except LinkError: pass
				if not isinstance(data[0], (list, dict, tuple))\
						and isinstance(data[1], (list, dict, tuple))\
						and not detect_link(data[0]):
					return process_layer(data[1], path+[data[0]])
			# print('LIST', repr(data)[:100])
			return filter(None, list(process_layer(val, path+[n]) for n, val in enumerate(data)))
		elif isinstance(data, dict):
			path += ['dict']
			# print('DICT', repr(data)[:100])
			return process_layer(data.items(), path)
		else:
			log.warn( 'Unrecognized data structure'
				' (path: %s, type: %s): %s', path, type(data), data )

	def flatten(val):
		if not isinstance(val, Link):
			for val in val:
				for val in flatten(val): yield val
		else: yield val

	return set(flatten(process_layer(backlog)))

def backlog_process(backlog, spec):
	if not backlog: return set()
	match = re.search(r'^random-(\d+)$', spec)
	if match:
		random.seed()
		backlog_subset = set()
		for n in xrange(int(match.group(1))):
			if not backlog: break
			link = random.choice(list(backlog))
			backlog.remove(link)
			backlog_subset.add(link)
		return backlog_subset
	elif spec == 'all': return backlog
	else: raise ValueError(spec)


def generate_rst(bookmarks, backlog): # XXX: also recent/common places maybe?
	output = io.StringIO()
	p = lambda line,end=u'\n': print(unicode(line), end=end, file=output)

	def p_header(header, style=u'=', indent=u'', newlines=1):
		p(indent, end=u'')
		p(header)
		p(indent, end=u'')
		p(u'='*len(header))
		p(u'\n'*newlines, end=u'')

	links = OrderedDict()
	def p_link(title, url, tpl=None, tpl_cb=None, end=u'\n'):
		assert title not in links, title # XXX: append some token
		if not title: link = url
		else:
			title = title.replace('`', "'").replace(':', '_')
			link = u'`{}`_'.format(title)
			links[title] = url
		if tpl: p(unicode(tpl).format(link), end=end)
		elif tpl_cb: p(tpl(link), end=end)
		else: return link
	def p_link_urls(newlines_pre=1, newlines=0):
		p(u'\n'*newlines_pre, end=u'')
		for title, url in links.viewitems():
			p(u'.. _{}: {}'.format(title, url))
		links.clear()
		p(u'\n'*newlines, end=u'')

	p_header('Bookmarks')
	for bm in bookmarks.viewvalues():
		p_link(bm['bm_title'], bm['url'], ' * {}')
	p_link_urls(newlines=2)

	p_header('Backlog')
	for link in backlog:
		p_link(link.title, link.url, ' * {}')
	p_link_urls()

	return output.getvalue()



def main(args=None):
	import argparse
	parser = argparse.ArgumentParser(
		description='Tool to generate firefox homepage from bookmarks, history and other data.')

	parser.add_argument('-o', '--output', metavar='format', choices=['rst', 'html'], default='html',
		help='Output format. Possible choices: rst, html (default: %(default)s).')

	parser.add_argument('-b', '--backlog', metavar='path',
		help='Path to yaml with a backlog of random links to visit.'
			' Format should be any depth of nested dicts or lists, with dicts or two-element'
				' tuples of title + link, or just lists of links at the bottom ones.'
			' Links get detected by the web url format, point is that any'
				' obviously-tied string element will be considered to be a title for that link.')
	parser.add_argument('-x', '--backlog-pick', metavar='spec', default='random-30',
		help='How to pick/represent which backlog links to display.'
			' Supported choices: random-<num>, all (default: %(default)s).')

	parser.add_argument('-P', '--profile', metavar='name/key/path',
		help='Full firefox profile name, profile directory name'
			' or its fragment, or a full path to profile dir (default: use default profile).')
	parser.add_argument('-t', '--db-lock-timeout',
		type=float, metavar='seconds', default=30,
		help='Timeout to acquire sqlite transaction locks (default: %(default)ss).')

	parser.add_argument('-d', '--debug', action='store_true', help='Verbose operation mode.')
	opts = parser.parse_args(sys.argv[1:] if args is None else args)

	global log
	import logging
	logging.basicConfig(level=logging.DEBUG if opts.debug else logging.WARNING)
	log = logging.getLogger()

	if opts.debug:
		global pyaml, dump
		try:
			import pyaml
		except ImportError:
			# Only crash when/if this debug printer actually gets used
			err_type, err_val, err_tb = sys.exc_info()
			def pyaml_dump(*args,**kws): raise err_type, err_val, err_tb
			pyaml = type('pyaml_mock', (object,), dict(dump=pyaml_dump))()
		else:
			pyaml.UnsafePrettyYAMLDumper.add_representer(
				sqlite3.Row, lambda s,o: s.represent_dict(dict((k, o[k]) for k in o.keys())) )
			pyaml.UnsafePrettyYAMLDumper.add_representer(
				Link, lambda s,o: s.represent_dict(o._asdict()) )
		dump = ft.partial(pyaml.dump, dst=sys.stdout)

	profile_dir = get_profile_dir(opts.profile)
	log.debug('Using ff profile dir: %s', profile_dir)

	bookmarks = bookmarks_get(join(profile_dir, 'places.sqlite'), timeout=opts.db_lock_timeout)

	if opts.backlog:
		backlog = backlog_get(opts.backlog)
		backlog = backlog_process(backlog, opts.backlog_pick)
	else: backlog = set()

	# XXX: get_places()

	out = generate_rst(bookmarks, backlog)
	if opts.output == 'html':
		raise NotImplementedError
		# XXX: generate_html(out)

	sys.stdout.write(out.encode('utf-8'))


if __name__ == '__main__': sys.exit(main())
