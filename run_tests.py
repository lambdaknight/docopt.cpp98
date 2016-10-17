#!/usr/bin/env python2

import re
import sys
import json
import subprocess

executable = "${TESTPROG}"

def check_output(*popenargs, **kwargs):
  r"""Run command with arguments and return its output as a byte string.

	If the exit code was non-zero it raises a CalledProcessError.  The
	CalledProcessError object will have the return code in the returncode
	attribute and output in the output attribute.

	The arguments are the same as for the Popen constructor.  Example:

	>>> check_output(["ls", "-l", "/dev/null"])
	'crw-rw-rw- 1 root root 1, 3 Oct 18  2007 /dev/null\n'

	The stdout argument is not allowed as it is used internally.
	To capture standard error in the result, use stderr=STDOUT.

	>>> check_output(["/bin/sh", "-c",
	...               "ls -l non_existent_file ; exit 0"],
	...              stderr=STDOUT)
	'ls: non_existent_file: No such file or directory\n'
	"""
  if 'stdout' in kwargs:
	  raise ValueError('stdout argument not allowed, it will be overridden.')
  process = subprocess.Popen(stdout=subprocess.PIPE, *popenargs, **kwargs)
  output, unused_err = process.communicate()
  retcode = process.poll()
  if retcode:
	  cmd = kwargs.get("args")
	  if cmd is None:
		  cmd = popenargs[0]
	  raise subprocess.CalledProcessError(retcode, cmd)
  return output

def parse_test(raw):
	raw = re.compile('#.*$', re.M).sub('', raw).strip()
	if raw.startswith('"""'):
		raw = raw[3:]

	for fixture in raw.split('r"""'):
		name = ''
		doc, _, body = fixture.partition('"""')
		cases = []
		for case in body.split('$')[1:]:
			argv, _, expect = case.strip().partition('\n')
			expect = json.loads(expect)
			prog, _, argv = argv.strip().partition(' ')
			cases.append((prog, argv, expect))

		yield name, doc, cases

failures = 0
passes = 0

tests = open('${TESTCASES}','r').read()
for _, doc, cases in parse_test(tests):
	if not cases: continue

	for prog, argv, expect in cases:
		args = [ x for x in argv.split() if x ]

		expect_error = not isinstance(expect, dict)

		error = None
		out = None

		try:
			out = check_output([executable, doc]+args, stderr=subprocess.STDOUT)
			if expect_error:
				error = " ** an error was expected but it appeared to succeed!"
			else:
				json_out = json.loads(out)
				if expect != json_out:
					error = " ** JSON does not match expected: %r" % expect
		except subprocess.CalledProcessError as e:
			if not expect_error:
				error = "\n ** this should have succeeded! exit code = %s" % e.returncode

		if not error:
			passes += 1
			continue

		failures += 1

		print "="*40
		print doc
		print ':'*20
		print prog, argv
		print '-'*20
		if out:
			print out
		print error

if failures:
	print "%d failures" % failures
	sys.exit(1)
else:
	print "PASS (%d)" % passes
