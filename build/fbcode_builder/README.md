# Easy builds for Facebook projects

This is a Python 2.6+ library designed to simplify continuous-integration
(and other builds) of Facebook projects.

For external Travis builds, the entry point is `travis_docker_build.sh`.


## Using Docker to reproduce a CI build

If you are debugging or enhancing a CI build, you will want to do so from
host or virtual machine that can run a reasonably modern version of Docker:

```
./make_docker_context.py --help  # See available options
./travis_docker_build.sh  # Tiny wrapper that starts a Travis-like build
```

**IMPORTANT**: Read `fbcode_builder/README.docker` befire diving in!


# What to read next

The *.py files are fairly well-documented. You might want to peruse them
in this order:
 - shell_quoting.py
 - fbcode_builder.py
 - docker_builder.py
 - make_docker_context.py

This library also has an (unpublished) component targeting Facebook's
internal continuous-integration platform using the same build-step DSL.


# Contributing

Please follow the ambient style (or PEP-8), and keep the code Python 2.6
compatible -- since `fbcode_builder`'s only dependency is Docker, we want to
allow building projects on even fairly ancient base systems.
