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

parser = argparse.ArgumentParser(description=textwrap.dedent('''
Reads --dockerfile-in, and outputs --dockerfile-out after making
substitutions based on the command-line arguments to this program.

Substitutions are done using Python's `str.format()` facility, so you
can use {ubuntu_version} for --ubuntu-version. Use {{ and }} to produce
a single literal curly brace, see the Python docs for more information.

Sample usage:

    (u=14.04 ; g=4.9 ; rm Dockerfile ;
        ./emit-dockerfile.py --ubuntu-version "$u" --gcc-version "$g" \\
          --substitute proxygen_git_hash master &&
        docker build -t "fb-projects-$u-$g" . 2>&1 | tee "log-$u-$g")
'''), formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument(
    '--dockerfile-in', default='Dockerfile.in',
    metavar='PATH', help='Default: %(default)s')
parser.add_argument(
    '--dockerfile-out', default='Dockerfile',
    metavar='PATH', help='Default: %(default)s')
# Our Dockerfile uses the numeric YY.MM version format.
parser.add_argument(
    '--ubuntu-version', choices=['14.04', '16.04'], required=True,
    metavar='YY.MM', help='Choices: %(choices)s')
parser.add_argument('--gcc-version', required=True, metavar='VER')
parser.add_argument(
    '--make-parallelism', type=int, default=1, metavar='NUM',
    help='Use `make -j` on multi-CPU systems with lots of RAM'
)
parser.add_argument(
    '--substitute', nargs=2, metavar=('KEY', 'VALUE'), action='append',
    default=[],
    help='Can be repeated. Besides the --* arguments, also substitute these '
         '{key}s for these values in --dockerfile-in. WARNING: You are '
         'responsible for escaping these with e.g. $(printf %%q value) if '
         'they are to be used as shell args.'
)
args = parser.parse_args()

valid_vers = (('14.04', '4.9'), ('16.04', '5'))
if (args.ubuntu_version, args.gcc_version) not in valid_vers:
    raise Exception(
        'Due to 4/5 ABI changes (std::string), we can only use {0}'.format(
            ' / '.join('GCC {0} on Ubuntu {1}'.format(*p) for p in valid_vers)
        )
    )

with open(args.dockerfile_in, 'r') as f:
    replacements = {}
    replacements.update(args.__dict__)  # includes --substitute, lol
    replacements.update(dict(args.substitute))
    try:
        dockerfile = f.read().format(**replacements)
    except KeyError as ex:
        raise Exception(
            '`--dockerfile-in {f}` needs `--substitute {k} A_VALUE`'.format(
                f=args.dockerfile_in, k=ex.args[0]
            )
        )

with os.fdopen(os.open(
    args.dockerfile_out,
    os.O_RDWR | os.O_CREAT | os.O_EXCL,  # Do not erase an existing Dockerfile
    0o644,
), 'w') as f:
    f.write(dockerfile)
