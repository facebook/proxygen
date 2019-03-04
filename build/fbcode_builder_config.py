#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved
from __future__ import absolute_import, division, print_function, unicode_literals

import specs.fizz as fizz
import specs.folly as folly
import specs.proxygen as proxygen
import specs.sodium as sodium
import specs.wangle as wangle
from shell_quoting import ShellQuoted


"fbcode_builder steps to build & test Proxygen"


def fbcode_builder_spec(builder):
    return {
        "depends_on": [folly, wangle, proxygen, fizz, sodium],
        "steps": [
            # Proxygen is the last step, so we are still in its working dir.
            builder.step("Run proxygen tests", [builder.run(ShellQuoted("make check"))])
        ],
    }


config = {
    "github_project": "facebook/proxygen",
    "fbcode_builder_spec": fbcode_builder_spec,
}
