#!/usr/bin/env python
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals
"Docker doesn't allow templating in FROM, so we do it externally. Try --help."

import argparse
import os
import re
import textwrap


def gcc_version(s):
    if re.match('^(4\.9|5\.[0-9]+)$', s) is None:
        raise Exception('GCC versions 4.9 and 5.x supported, got {}'.format(s))
    return s


parser = argparse.ArgumentParser(description=textwrap.dedent('''
Reads --dockerfile-in, and outputs --dockerfile-out after making
substitutions based on the command-line arguments to this program.

Substitutions are done using Python's `str.format()` facility, so you
can use {ubuntu_version} for --ubuntu-version. Use {{ and }} to produce
a single literal curly brace, see the Python docs for more information.

Sample usage:

  (u=14.04 ; g=4.9 ; rm Dockerfile ;
   ./emit-dockerfile.py --ubuntu-version "$u" --gcc-version "$g" &&
   docker build -t "fb-projects-$u-$g" 2>&1 | tee "log-$u-$g")
'''))
parser.add_argument('--dockerfile-in', default='Dockerfile.in')
parser.add_argument('--dockerfile-out', default='Dockerfile')
# Our Dockerfile uses the numeric YY.MM version format.
parser.add_argument(
    '--ubuntu-version', choices=['14.04', '16.04'], required=True)
parser.add_argument('--gcc-version', type=gcc_version, required=True)
parser.add_argument(
    '--make-parallelism', type=int, default=1,
    help='Use `make -j` on multi-processor system with lots of RAM'
)
args = parser.parse_args()

if not (
    (args.ubuntu_version == '14.04' and args.gcc_version == '4.9') or
    (args.ubuntu_version == '16.04' and
        re.match('^5\.[0-9]+$', args.gcc_version) is not None)
):
    raise Exception(
        'We can only use GCC 4.9 on Ubuntu 14.04, and 5.x on Ubuntu 16.04, '
        'since their C++ ABIs are incompatible (e.g. std::string).'
    )

with open(args.dockerfile_in, 'r') as f:
    dockerfile = f.read().format(**args.__dict__)

with os.fdopen(  # Do not clobber a pre-existing Dockerfile
    os.open(args.dockerfile_out, os.O_RDWR | os.O_CREAT | os.O_EXCL), 'w'
) as f:
    f.write(dockerfile)
