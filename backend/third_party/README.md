# Third-Party Dependencies

This directory documents external dependencies that the backend integrates via CMake. The actual code is downloaded at configure-time using `FetchContent` so that we do not need to vendor the full sources inside the repository.

* **cpp-httplib** – header-only HTTP server/client used for REST endpoints. Pulled from https://github.com/yhirose/cpp-httplib (tag `v0.15.3`).
* **SQLite3** – provided by the host toolchain and linked via `find_package(SQLite3 REQUIRED)`.
* **TRDP stack** – pre-built static libraries delivered by MEN/Tecnologias. The backend links against an installed copy of the stack.

## TRDP installation layout

Because the upstream TRDP sources rely on classic Makefiles, we standardize on a lightweight
"install" layout that mimics how CMake packages are typically structured. Pick a prefix that is
stable on your system (e.g. `/usr/local/trdp`, `/opt/trdp`, or a custom directory referenced by a
`TRDP_ROOT` environment variable) and copy the artifacts into the following tree:

```
$TRDP_ROOT
├── include/
│   └── trdp/          # All exported headers from the TRDP stack
│       ├── trdp_if.h
│       ├── ...
└── lib/
    ├── libtrdp.a
    ├── libtrdpap.a    # optional application profile helper
    └── lib*.so/.dll   # if shared libraries are available
```

Example helper script (Linux):

```
export TRDP_ROOT=/usr/local/trdp
sudo mkdir -p "$TRDP_ROOT/include/trdp" "$TRDP_ROOT/lib"
sudo cp path/to/TRDP/include/*.h "$TRDP_ROOT/include/trdp/"
sudo cp path/to/TRDP/lib/libtrdp*.a "$TRDP_ROOT/lib/"
```

Once the files follow this layout you can configure the backend with either:

```
cmake -S backend -B backend/build -DTRDP_ROOT=/usr/local/trdp
# or rely on the environment variable
TRDP_ROOT=/usr/local/trdp cmake -S backend -B backend/build
```

The `backend/cmake/FindTRDP.cmake` module will discover the headers and libraries and expose
imported targets (`TRDP::trdp`, `TRDP::trdpap`) so that the rest of the project can link against the
stack without hard-coding absolute paths.

### Automating the install from CMake

When working on Linux hosts you can let the backend's CMake project bootstrap the TRDP
dependencies for you. Pass the following cache entries when configuring:

```
cmake -S backend -B backend/build \
      -DTRDP_AUTO_SETUP=ON \
      -DTRDP_SETUP_PREFIX=$HOME/.local/trdp \
      -DTRDP_PLATFORM_CONFIG=LINUX_X86_64_config
```

With `TRDP_AUTO_SETUP` enabled the configure step will invoke
[`third_party/scripts/setup_trdp.sh`](./scripts/setup_trdp.sh) to download the latest
SourceForge archive, build it with GNU Make, and populate the requested prefix. Afterwards the
standard `FindTRDP.cmake` logic discovers the freshly installed headers and libraries
automatically. (You can point `TRDP_PLATFORM_CONFIG` at any of the upstream `*_config` targets if
you need architectures other than `LINUX_X86_64`.)

See [TRDP_INSTALL_LINUX.md](./TRDP_INSTALL_LINUX.md) for a concrete walkthrough that downloads the
official SourceForge release on Linux, builds it with the upstream Makefiles, and stages the files in
the layout above so CMake can detect them automatically. The same document also describes how to run
`setup_trdp.sh` manually when you prefer to prepare the prefix outside of CMake.
