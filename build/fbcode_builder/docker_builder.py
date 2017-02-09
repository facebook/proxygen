#!/usr/bin/env python
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals
'''

Extends FBCodeBuilder to produce Docker context directories.

In order to get the largest iteration-time savings from Docker's build
caching, you will want to:
 - Use fine-grained steps as appropriate (e.g. separate make & make install),
 - Start your action sequence with the lowest-risk steps, and with the steps
   that change the least often, and
 - Put the steps that you are debugging towards the very end.

'''
import os
import tempfile

from fbcode_builder import FBCodeBuilder
from shell_quoting import (
    raw_shell, shell_comment, shell_join, ShellQuoted
)
from utils import recursively_flatten_list, run_command


class DockerFBCodeBuilder(FBCodeBuilder):

    def _user(self):
        return self.option('user', 'root')

    def _change_user(self):
        return ShellQuoted('USER {u}').format(u=self._user())

    def setup(self):
        # Please add RPM-based OSes here as appropriate.
        #
        # To allow exercising non-root installs -- we change users after the
        # system packages are installed.  TODO: For users not defined in the
        # image, we should probably `useradd`.
        return self.step('Setup', [
            ShellQuoted('FROM {img}').format(
                # Docker can't deal with quotes. Oh well.
                img=ShellQuoted(self.option('os_image')),
            ),
            # /bin/sh syntax is a pain
            ShellQuoted('SHELL ["/bin/bash", "-c"]'),
        ] + self.install_debian_deps() + [self._change_user()])

    def step(self, name, actions):
        assert '\n' not in name, 'Name {0} would span > 1 line'.format(name)
        b = ShellQuoted('')
        return [ShellQuoted('### {0} ###'.format(name)), b] + actions + [b]

    def run(self, shell_cmd):
        return ShellQuoted('RUN {cmd}').format(cmd=shell_cmd)

    def workdir(self, dir):
        return [
            # As late as Docker 1.12.5, this results in `build` being owned
            # by root:root -- the explicit `mkdir` works around the bug:
            #   USER nobody
            #   WORKDIR build
            ShellQuoted('USER root'),
            ShellQuoted('RUN mkdir -p {d} && chown {u} {d}').format(
                d=dir, u=self._user()
            ),
            self._change_user(),
            ShellQuoted('WORKDIR {dir}').format(dir=dir),
        ]

    def comment(self, comment):
        # This should not be a command since we don't want comment changes
        # to invalidate the Docker build cache.
        return shell_comment(comment)

    def copy_local_repo(self, repo_dir, dest_name):
        fd, archive_path = tempfile.mkstemp(
            prefix='local_repo_{0}_'.format(dest_name),
            suffix='.tgz',
            dir=os.path.abspath(self.option('docker_context_dir')),
        )
        os.close(fd)
        run_command('tar', 'czf', archive_path, '.', cwd=repo_dir)
        return [
            ShellQuoted('ADD {archive} {dest_name}').format(
                archive=os.path.basename(archive_path), dest_name=dest_name
            ),
            # Docker permissions make very little sense... see also workdir()
            ShellQuoted('USER root'),
            ShellQuoted('RUN chown -R {u} {d}').format(
                d=dest_name, u=self._user()
            ),
            self._change_user(),
        ]

    def _render_impl(self, steps):
        return raw_shell(shell_join('\n', recursively_flatten_list(steps)))
