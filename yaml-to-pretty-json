#!/usr/bin/env python

import os, sys, json, pathlib as pl

import yaml # pip install --user pyyaml


def main(args=None):
	import argparse
	parser = argparse.ArgumentParser(
		description='Convert YAML input file to a prettified JSON.')
	parser.add_argument('path', nargs='*', help='Path to the YAML file(s) to convert.')
	parser.add_argument('-c', '--convert', action='store_true',
		help='Convert all input file(s) and create .json files next to them.')
	parser.add_argument('-r', '--reverse',
		action='store_true', help='Convert JSON to pretty YAML instead (requires "pyaml" module).')
	opts = parser.parse_args(sys.argv[1:] if args is None else args)

	src_list = opts.path
	if not src_list:
		if opts.convert: parser.error('No source file path(s) specfied for -c/--convert')
		src_list = [sys.stdin]
	for src in src_list:
		if src is sys.stdin: src_str = src.read()
		else:
			src_path = pl.Path(src)
			src_str = src_path.read_text()

		src = yaml.safe_load(src_str)
		if not opts.reverse:
			res = json.dumps(src, sort_keys=True, indent=2, separators=(',', ': '))
		else:
			import pyaml
			res = pyaml.dumps(src)
		if res and not res.endswith('\n'): res += '\n'

		if not opts.convert: print(res)
		else:
			p = src_path.parent / (src_path.name.rsplit('.', 1)[0] + '.json')
			p.write_text(res)

if __name__ == '__main__': sys.exit(main())
