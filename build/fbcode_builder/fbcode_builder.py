#!/usr/bin/env python
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals
'''

This is a small DSL to describe builds of Facebook's open-source projects
that are published to Github from a single internal repo, including projects
that depend on folly, wangle, proxygen, fbthrift, etc.

This file defines the interface of the DSL, and common utilieis, but you
will have to instantiate a specific builder, with specific options, in
order to get work done -- see e.g. make_docker_context.py.

== Design notes ==

Goals:

 - A simple declarative language for what needs to be checked out & built,
   how, in what order.

 - The same specification should work for external continuous integration
   builds (e.g. Travis + Docker) and for internal VM-based continuous
   integration builds.

 - One should be able to build without root, and to install to a prefix.

Non-goals:

 - General usefulness. The only point of this is to make it easier to build
   and test Facebook's open-source services.

Ideas for the future -- these may not be very good :)

 - A "shell script" builder. Like DockerFBCodeBuilder, but outputs a
   shell script that runs outside of a container. Or maybe even
   synchronously executes the shell commands, `make`-style.

 - A "Makefile" generator. That might make iterating on builds even quicker
   than what you can currently get with Docker build caching.

'''

import os

from shell_quoting import path_join, shell_join, ShellQuoted


class FBCodeBuilder(object):

    def __init__(self, **kwargs):
        self._options_do_not_access = kwargs  # Use .option() instead.
        # This raises upon detecting options that are specified but unused,
        # because otherwise it is very easy to make a typo in option names.
        self.options_used = set()

    def __repr__(self):
        return '{0}({1})'.format(
            self.__class__.__name__,
            ', '.join(
                '{0}={1}'.format(k, repr(v))
                    for k, v in self._options_do_not_access.items()
            )
        )

    def option(self, name, default=None):
        value = self._options_do_not_access.get(name, default)
        if value is None:
            raise RuntimeError('Option {0} is required'.format(name))
        self.options_used.add(name)
        return value

    def add_option(self, name, value):
        if name in self._options_do_not_access:
            raise RuntimeError('Option {0} already set'.format(name))
        self._options_do_not_access[name] = value

    #
    # Abstract parts common to every installation flow
    #

    def render(self, steps):
        '''

        Converts nested actions to your builder's expected output format.
        Typically takes the output of build().

        '''
        res = self._render_impl(steps)  # Implementation-dependent
        # Now that the output is rendered, we expect all options to have
        # been used.
        unused_options = set(self._options_do_not_access)
        unused_options -= self.options_used
        if unused_options:
            raise RuntimeError(
                'Unused options: {0} -- please check if you made a typo '
                'in any of them. Those that are truly not useful should '
                'be not be set so that this typo detection can be useful.'
                .format(unused_options)
            )
        return res

    def build(self, steps):
        return [self.setup(), self.diagnostics()] + steps

    def setup(self):
        'Your builder may want to install packages here.'
        raise NotImplementedError

    def diagnostics(self):
        'Log some system diagnostics before/after setup for ease of debugging'
        # The builder's repr is not used in a command to avoid pointlessly
        # invalidating Docker's build cache.
        return self.step('Diagnostics', [
            self.comment('Builder {0}'.format(repr(self))),
            self.run(ShellQuoted('hostname')),
            self.run(ShellQuoted('cat /etc/issue')),
            self.run(ShellQuoted('g++ --version || echo g++ not installed')),
        ])

    def step(self, name, actions):
        'A labeled collection of actions or other steps'
        raise NotImplementedError

    def run(self, shell_cmd):
        'Run this bash command'
        raise NotImplementedError

    def workdir(self, dir):
        'Create this directory if it does not exist, and change into it'
        raise NotImplementedError

    def copy_local_repo(self, dir, dest_name):
        '''
        Copy the local repo at `dir` into this step's `workdir()`, analog of:
          cp -r /path/to/folly folly
        '''
        raise NotImplementedError

    #
    # Specific build helpers
    #

    def install_debian_deps(self):
        actions = [
            self.run(ShellQuoted(
                'apt-get update && apt-get install -yq '
                'autoconf-archive '
                'bison '
                'build-essential '
                'cmake '
                'curl '
                'flex '
                'git '
                'gperf '
                'joe '
                'libboost-all-dev '
                'libcap-dev '
                'libdouble-conversion-dev '
                'libevent-dev '
                'libgflags-dev '
                'libgoogle-glog-dev '
                'libkrb5-dev '
                'libnuma-dev '
                'libsasl2-dev '
                'libsnappy-dev '
                'libsqlite3-dev '
                'libssl-dev '
                'libtool '
                'netcat-openbsd '
                'pkg-config '
                'unzip '
                'wget'
            )),
        ]
        gcc_version = self.option('gcc_version')

        # We need some extra packages to be able to install GCC 4.9 on 14.04.
        if self.option('os_image') == 'ubuntu:14.04' and gcc_version == '4.9':
            actions.append(self.run(ShellQuoted(
                'apt-get install -yq software-properties-common && '
                'add-apt-repository ppa:ubuntu-toolchain-r/test && '
                'apt-get update'
            )))

        # Make the selected GCC the default before building anything
        actions.extend([
            self.run(ShellQuoted('apt-get install -yq {c} {cpp}').format(
                c=ShellQuoted('gcc-{v}').format(v=gcc_version),
                cpp=ShellQuoted('g++-{v}').format(v=gcc_version),
            )),
            self.run(ShellQuoted(
                'update-alternatives --install /usr/bin/gcc gcc {c} 40 '
                '--slave /usr/bin/g++ g++ {cpp}'
            ).format(
                c=ShellQuoted('/usr/bin/gcc-{v}').format(v=gcc_version),
                cpp=ShellQuoted('/usr/bin/g++-{v}').format(v=gcc_version),
            )),
            self.run(ShellQuoted('update-alternatives --config gcc')),
        ])

        # Ubuntu 14.04 comes with a CMake version that is too old for mstch.
        if self.option('os_image') == 'ubuntu:14.04':
            actions.append(self.run(ShellQuoted(
                'apt-get install -yq software-properties-common && '
                'add-apt-repository ppa:george-edison55/cmake-3.x && '
                'apt-get update && '
                'apt-get upgrade -yq cmake'
            )))

        return self.step('Install packages for Debian-based OS', actions)

    def github_project_workdir(self, project, path):
        # Only check out a non-default branch if requested. This especially
        # makes sense when building from a local repo.
        git_hash = self.option('{0}:git_hash'.format(project), '')
        maybe_change_branch = [
            self.run(ShellQuoted('git checkout {hash}').format(hash=git_hash)),
        ] if git_hash else []

        base_dir = self.option('projects_dir')
        local_repo_dir = self.option('{0}:local_repo_dir'.format(project), '')
        return self.step('Check out {0}, workdir {1}'.format(project, path), [
            self.workdir(base_dir),
            self.run(
                ShellQuoted('git clone https://github.com/{p}')
                    .format(p=project)
            ) if not local_repo_dir else self.copy_local_repo(
                local_repo_dir, os.path.basename(project)
            ),
            self.workdir(path_join(base_dir, os.path.basename(project), path)),
        ] + maybe_change_branch)

    def fb_github_project_workdir(self, project_and_path):
        'This helper lets Facebook-internal CI special-cases FB projects'
        project, path = project_and_path.split('/', 1)
        return self.github_project_workdir('facebook/' + project, path)

    def _make_vars(self, make_vars):
        return shell_join(' ', (
            ShellQuoted('{k}={v}').format(k=k, v=v)
                for k, v in ({} if make_vars is None else make_vars).items()
        ))

    def parallel_make(self, make_vars=None):
        return self.run(ShellQuoted('make -j {n} {vars}').format(
            n=self.option('make_parallelism'),
            vars=self._make_vars(make_vars),
        ))

    def make_and_install(self, make_vars=None):
        return [
            self.parallel_make(make_vars),
            self.run(ShellQuoted('make install {vars}').format(
                vars=self._make_vars(make_vars),
            )),
        ]

    def autoconf_install(self, name):
        return self.step('Build and install {0}'.format(name), [
            self.run(ShellQuoted('autoreconf -ivf')),
            self.run(ShellQuoted(
                'LDFLAGS="$LDFLAGS -L"{p}"/lib -Wl,-rpath="{p}"/lib" '
                'CFLAGS="$CFLAGS -I"{p}"/include" '
                'CPPFLAGS="$CPPFLAGS -I"{p}"/include" '
                'PY_PREFIX={p} '
                './configure --prefix={p}'
            ).format(p=self.option('prefix'))),
        ] + self.make_and_install())

    def cmake_install(self, name):
        cmake_defines = {
            'BUILD_SHARED_LIBS': 'ON',
            'CMAKE_INSTALL_PREFIX': self.option('prefix'),
        }
        cmake_defines.update(
            self.option('{0}:cmake_defines'.format(name), {})
        )
        return self.step('Build and install {0}'.format(name), [
            self.workdir('build'),
            self.run(ShellQuoted('cmake {args} ..').format(
                args=shell_join(' ', (
                    ShellQuoted('-D{k}={v}').format(k=k, v=v)
                        for k, v in cmake_defines.items()
                ))
            )),
        ] + self.make_and_install())

    def fb_github_autoconf_install(self, project_and_path):
        return [
            self.fb_github_project_workdir(project_and_path),
            self.autoconf_install(project_and_path),
        ]

    def fb_github_cmake_install(self, project_and_path):
        return [
            self.fb_github_project_workdir(project_and_path),
            self.cmake_install(project_and_path),
        ]
