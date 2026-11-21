# libv90modem documentation

Plain Markdown; `include/v90modem.h` is the authority on the API, and
these pages follow it.

- [Getting started](getting-started.md): build, install, link, and a
  minimal answering and calling program.
- [The line interface](line-interface.md): samples, the G.711 law, block
  lengths, clocks, echo delay, levels, and what the program does itself.
- [Answering](answering.md): the answering role, modulation by modulation
  (x2 included), with its events, retrains, cleardown and limits.
- [Calling](calling.md): the calling role, the same way (simulation and own
  answerer only).
- [Error control](error-control.md): the V.14 start-stop framer and the
  V.42 endpoint (LAPM, V.42bis, V.44) on the modem's bit pump.
- [API reference](api.md): every type, constant and function of the
  header, by topic.
- [Testing](testing.md): `make check`, `check-arm`, `check-ubsan`, the port
  matrix, the replay tool and the recorded calls.
- [Known limits](limits.md): what is not done, not tried, or measured
  only in simulation.

Elsewhere in the tree: `README.md` (overview), `DESIGN.md` (how the modem
is built and why), `CHANGELOG.md` (what each version changes for a
program), `CONTRIBUTING.md`, `tools/README.md` (the tools and the call
recording format), and [history/](history/README.md), the development
logs.
