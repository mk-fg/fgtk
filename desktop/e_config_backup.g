#!/usr/bin/env python2
# -*- coding: utf-8 -*-
from __future__ import print_function

import itertools as it, operator as op, functools as ft
from collections import namedtuple
from subprocess import Popen, PIPE
import os, sys, re

Group = namedtuple('group', 'name type key contents')
Value = namedtuple('value', 'name type contents')
sort_func = lambda elm: (elm.__class__.__name__, elm)


%%
parser eet_cfg:
	ignore: r'[ \t\r\n]+'
	token END: r'$'
	token N: r'[+\-]?[\d.]+'
	token S: r'"([^"\\]*(\\.[^"\\]*)*)"' # not tested for \\" vs \"
	token VT: r'\w+:'
	token GT: r'struct|list|hash'
	token K: r'(key\s+"[^"]*"\s*;)?'

	rule config: block END {{ return block }}

	rule block: block_group {{ return block_group }}
		| block_value {{ return block_value }}

	rule block_group:
		K 'group' S GT r'\{' {{ contents = list() }}
		( block {{ contents.append(block) }} )*
		r'\}' {{ return Group(S, GT, K, contents) }}

	rule value: S {{ return S }} | N {{ return N }}
	rule block_value: 'value' S VT value ';' {{ return Value(S, VT, value) }}
%%


def dump(elm, indent=0):
	assert isinstance(elm, (Group, Value)), elm
	if hasattr(elm, 'contents'):
		if isinstance(elm.contents, bytes):
			contents = elm.contents
		else:
			contents = ''.join(
				dump(val, indent=indent+1)
				for val in sorted(elm.contents, key=sort_func) )
	eol = bol = ''
	node = [elm.__class__.__name__, elm.name]
	if isinstance(elm, Group):
		if elm.key: bol = ' '*indent*4 + elm.key + '\n'
		if elm.name.strip('"') in [ 'E_Remember',
			'E_Exehist_Item', 'Comp_Match', 'E_Randr_Config' ]: return ''
		contents = '{{\n{}{}}}'.format(contents, ' '*indent*4)
	elif isinstance(elm, Value):
		if elm.name == '"file"'\
				and elm.type == 'string:'\
				and contents.startswith('"/tmp/.lqr_wpset_bg.'):
			contents = '"/tmp/bg.png"'
		eol = ';'
	node.extend([elm.type, contents])
	return bol + ' '*indent*4 + ' '.join(node) + eol + '\n'

def dump_config(path, name=False):
	try:
		if name:
			if name is True: name = 'file: {}'.format(os.path.basename(path))
			data = '----- {}\n'.format(name)
		else: data = ''
		src = Popen(['eet', '-d', path, 'config'], stdout=PIPE)
		data += dump(parse('config', src.stdout.read()))
		if src.wait(): raise RuntimeError('eet exited with non-zero status')
		return data
	except Exception as err:
		print('Failed to process file: {}'.format(path), file=sys.stderr)
		raise


def main(argv=None):
	import argparse
	parser = argparse.ArgumentParser(
		description='Tool to decode E config, replace transient values (like desktop bg'
				' or E_Remember settings) and sort groups/values there to make it diff-friendly.'
			' With a second argument, also backup decoded config to a specified location.')
	parser.add_argument('config',
		help='Path to eet-encoded/compressed E config.'
			' If "-" (dash) is passed instead, list of paths to such configs will be read from stdin.')
	parser.add_argument('backup', nargs='?',
		help='Backup decoded config to the specified path.'
			' Checks file at the destination path for irrelevant changes'
				' (e.g. +/- 100 in prop.h or prop.pos_h) and skips the backup'
				' if only such diffs are detected.')
	optz = parser.parse_args(argv if argv is not None else sys.argv[1:])

	if optz.config != '-': src_data = dump_config(optz.config)
	else:
		paths = sys.stdin.read()
		sep = '\0' if '\0' in paths else '\n'
		paths = sorted(filter(None, paths.split(sep)))
		src_data = ''.join(map(ft.partial(dump_config, name=True), paths))

	if not optz.backup: sys.stdout.write(src_data)
	else:
		if os.path.exists(optz.backup):
			# Check if changes are irrelevant and can be discarded
			from tempfile import NamedTemporaryFile
			bak_data = open(optz.backup).read()

			with NamedTemporaryFile() as tmp1, NamedTemporaryFile() as tmp2:
				tmp1.write(src_data), tmp2.write(bak_data)
				tmp1.flush(), tmp2.flush()
				diff = Popen(['diff', '--unchanged-line-format=', tmp1.name, tmp2.name], stdout=PIPE)
				diff_data = diff.stdout.read()
				diff.wait()

			for line in it.imap(op.methodcaller('strip'), diff_data.splitlines()):
				# 'value "prop.pos_h" int: 291;' <-- numbers often change
				if not re.search( r'^value\s+"prop\.((pos|res)_)?'
					'[whxy]|startup_id"\s+int:\s+\d+\s*;$', line ): break
			else: src_data = None

		if src_data: open(optz.backup, 'w').write(src_data)


if __name__ == '__main__': main()
