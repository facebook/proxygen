#!/usr/bin/env python
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals
'Facebook-specific additions to the fbcode_builder spec for Proxygen'

config = read_fbcode_builder_config('fbcode_builder_config.py')
config['legocastle_opts'] = {
    'alias': 'proxygen-oss',
    'oncall': 'proxygen_open_source',
    'build_name': 'Open-source build for Proxygen',
    'projects_dir': None,  # No deps to build from outside fbsource
    # Some lego-linux machines are still 14.04, but we need GCC 4.9+.
    'legocastle_os': 'ubuntu_16.04',
}
