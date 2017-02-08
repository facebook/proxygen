#!/usr/bin/env python
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals
'fbcode_builder steps to build & test Proxygen'

import specs.folly as folly
import specs.wangle as wangle
import specs.proxygen as proxygen

from shell_quoting import ShellQuoted


def fbcode_builder_spec(builder):
    return {
        'depends_on': [folly, wangle, proxygen],
        'steps': [
            # Proxygen is the last step, so we are still in its working dir.
            builder.step('Run proxygen tests', [
                builder.run(ShellQuoted('make check')),
            ])
        ],
    }


config = {
    'github_project': 'facebook/proxygen',
    'fbcode_builder_spec': fbcode_builder_spec,
}
