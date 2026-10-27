# llhttp

Vendored copy of [llhttp](https://github.com/nodejs/llhttp), the HTTP/1.x
parser maintained by the Node.js project, plus the `LlhttpParser` wrapper and
the experimental `HTTP1xLlCodec` built on it. Intended to replace the fork of
`http_parser` in `proxygen/external/http_parser`, which is unmaintained upstream
and has diverged locally.

## Version

llhttp **9.4.3**, unmodified.

## Contents

llhttp generates its state machine from TypeScript. Only the generated C is
vendored, so building proxygen needs no Node toolchain:

| File       | Upstream path        | Generated |
| ---------- | -------------------- | --------- |
| `llhttp.h` | `build/llhttp.h`     | yes       |
| `llhttp.c` | `build/c/llhttp.c`   | yes       |
| `api.c`    | `src/native/api.c`   | no        |
| `http.c`   | `src/native/http.c`  | no        |

Taken from the copy already in this repo at
`third-party/node/v24.22.1/src/deps/llhttp`, which ships the generated output
alongside the sources.

## Updating

Replace the four files from a matching upstream release or an existing
in-repo copy, keeping the generated and hand-written sources at the same
version -- `llhttp.h` and `llhttp.c` describe the same state machine and a
mismatched pair will not fail loudly. Update the version above, and regenerate
`LLHTTP_SYMBOLS` in `BUCK`.

Do not edit the vendored sources in place. Anything proxygen needs on top of
llhttp belongs in `LlhttpParser` or the codec; the `http_parser` fork is what
happens otherwise.
