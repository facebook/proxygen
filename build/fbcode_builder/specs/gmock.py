#!/usr/bin/env python
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals


def fbcode_builder_spec(builder):
    return {
        'steps': [
            # google mock also provides the gtest libraries
            builder.github_project_workdir('google/googletest', 'googlemock/build'),
            builder.cmake_install('google/googletest'),
        ],
    }
