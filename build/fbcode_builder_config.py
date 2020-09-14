#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import absolute_import, division, print_function, unicode_literals

import specs.fizz as fizz
import specs.fmt as fmt
import specs.folly as folly
import specs.gmock as gmock
import specs.mvfst as mvfst
import specs.proxygen_quic as proxygen_quic
import specs.sodium as sodium
import specs.wangle as wangle
import specs.zstd as zstd
from shell_quoting import ShellQuoted


"fbcode_builder steps to build & test Proxygen"


def fbcode_builder_spec(builder):
    return {
        "depends_on": [
            gmock,
            fmt,
            folly,
            wangle,
            fizz,
            sodium,
            zstd,
            mvfst,
            proxygen_quic,
        ],
        "steps": [
            # Tests for the full build with no QUIC/HTTP3
            # Proxygen is the last step, so we are still in its working dir.
            builder.step(
                "Run proxygen tests",
                [builder.run(ShellQuoted("env CTEST_OUTPUT_ON_FAILURE=1 make test"))],
            )
        ],
    }


config = {
    "github_project": "facebook/proxygen",
    "fbcode_builder_spec": fbcode_builder_spec,
}
