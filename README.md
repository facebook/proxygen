## Proxygen: Facebook's C++ HTTP Libraries

[![Build Status](https://travis-ci.org/facebook/proxygen.svg?branch=master)](https://travis-ci.org/facebook/proxygen)

This project comprises the core C++ HTTP abstractions used at
Facebook. Internally, it is used as the basis for building many HTTP
servers, proxies, and clients. This release focuses on the common HTTP
abstractions and our simple HTTPServer framework. Future releases will
provide simple client APIs as well. The framework supports HTTP/1.1,
SPDY/3, SPDY/3.1, and HTTP/2. The goal is to provide a simple,
performant, and modern C++ HTTP library.

We have a Google group for general discussions at https://groups.google.com/d/forum/facebook-proxygen.

The [original blog post](https://code.facebook.com/posts/1503205539947302)
also has more background on the project.

### Installing

Note that currently this project has only been tested on Ubuntu 14.04,
although it likely works on many other platforms. Support for Mac OSX is
incomplete.

You will need at least 3 GiB of memory to compile `proxygen` and its
dependencies.

##### Easy Install

Just run `./deps.sh` from the `proxygen/` directory to get and build all
the dependencies and `proxygen`. It will also run all the tests. Then run
`./reinstall.sh` to install it. You can run `./deps.sh && ./reinstall.sh`
whenever to rebase the dependencies, and then rebuild and reinstall `proxygen`.

A note on compatibility: this project relies on system installed
[folly](https://github.com/facebook/folly). If you rebase `proxygen` and `make` starts to fail, you likely
need to update to the latest version of `folly`. Running
`./deps.sh && ./reinstall.sh` will do this for you. We are still working
on a solution to manage depencies more predictably.

##### Other Platforms

If you are running on another platform, you may need to install several
packages first. Proxygen and `folly` are all autotools based projects.

### Introduction

Directory structure and contents:

| Directory                  | Purpose                                                                       |
|----------------------------|-------------------------------------------------------------------------------|
| `proxygen/external/`       | Contains non-installed 3rd-party code proxygen depends on.                    |
| `proxygen/lib/`            | Core networking abstractions.                                                 |
| `proxygen/lib/http/`       | HTTP specific code.                                                           |
| `proxygen/lib/services/`   | Connection management and server code.                                        |
| `proxygen/lib/utils/`      | Miscellaneous helper code.                                                    |
| `proxygen/httpserver/`     | Contains code wrapping `proxygen/lib/` for building simple C++ http servers. We recommend building on top of these APIs. |

### Architecture

The central abstractions to understand in `proxygen/lib` are the session, codec,
transaction, and handler. These are the lowest level abstractions, and we
don't generally recommend building off of these directly.

When bytes are read off the wire, the `HTTPCodec` stored inside
`HTTPSession` parses these into higher level objects and associates with
it a transaction identifier. The codec then calls into `HTTPSession` which
is responsible for maintaining the mapping between transaction identifier
and `HTTPTransaction` objects. Each HTTP request/response pair has a
separate `HTTPTransaction` object. Finally, `HTTPTransaction` forwards the
call to a handler object which implements `HTTPTransaction::Handler`. The
handler is responsible for implementing business logic for the request or
response.

The handler then calls back into the transaction to generate egress
(whether the egress is a request or response). The call flows from the
transaction back to the session, which uses the codec to convert the
higher level semantics of the particular call into the appropriate bytes
to send on the wire.

The same handler and transaction interfaces are used to both create requests
and handle responses. The API is generic enough to allow
both. `HTTPSession` is specialized slightly differently depending on
whether you are using the connection to issue or respond to HTTP
requests.

![Core Proxygen Architecture](CoreProxygenArchitecture.png)

Moving into higher levels of abstraction, `proxygen/httpserver` has a
simpler set of APIs and is the recommended way to interface with `proxygen`
when acting as a server if you don't need the full control of the lower
level abstractions.

The basic components here are `HTTPServer`, `RequestHandlerFactory`, and
`RequestHandler`. An `HTTPServer` takes some configuration and is given a
`RequestHandlerFactory`. Once the server is started, the installed
`RequestHandlerFactory` spawns a `RequestHandler` for each HTTP
request. `RequestHandler` is a simple interface users of the library
implement. Subclasses of `RequestHandler` should use the inherited
protected member `ResponseHandler* downstream_` to send the response.

### Using it

Proxygen is a library. After installing it, you can build your own C++
server. Try `cd`ing to the directory containing the echo server at
`proxygen/httpserver/samples/echo/`. You can then build it with this one
liner:

<code>
g++ -std=c++14 -o my_echo EchoServer.cpp EchoHandler.cpp -lproxygenhttpserver -lfolly -lglog -lgflags -pthread
</code>

After running `./my_echo`, we can verify it works using curl in a different terminal:
```shell
$ curl -v http://localhost:11000/
*   Trying 127.0.0.1...
* Connected to localhost (127.0.0.1) port 11000 (#0)
> GET / HTTP/1.1
> User-Agent: curl/7.35.0
> Host: localhost:11000
> Accept: */*
>
< HTTP/1.1 200 OK
< Request-Number: 1
< Date: Thu, 30 Oct 2014 17:07:36 GMT
< Connection: keep-alive
< Content-Length: 0
<
* Connection #0 to host localhost left intact
```

### Documentation

We use Doxygen for Proxygen's internal documentation. You can generate a
copy of these docs by running `doxygen Doxyfile` from the project
root. You'll want to look at `html/namespaceproxygen.html` to start. This
will also generate `folly` documentation.

### Contributing
Contributions to Proxygen are more than welcome. [Read the guidelines in CONTRIBUTING.md](CONTRIBUTING.md).
Make sure you've [signed the CLA](https://code.facebook.com/cla) before sending in a pull request.

### Whitehat

Facebook has a [bounty program](https://www.facebook.com/whitehat/) for
the safe disclosure of security bugs. If you find a vulnerability, please
go through the process outlined on that page and do not file a public issue.
