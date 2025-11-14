# Installing the TRDP stack from the official SourceForge release (Linux)

The backend expects a pre-built TRDP stack that follows the prefix/include/lib layout described in
[README.md](./README.md). The upstream TRDP sources use GNU Make, but you can still produce the
required layout manually. The steps below were validated on a Debian/Ubuntu-like environment.

## 1. Download and extract the release archive

```bash
cd /tmp
wget -O TRDP-3.0.0.0.zip "https://sourceforge.net/projects/tcnopen/files/latest/download"
unzip TRDP-3.0.0.0.zip
cd 3.0.0.0
```

The latest archive currently expands into a `3.0.0.0/` directory that already contains the POSIX
Makefiles, configuration examples, and sources.

## 2. Select the Linux build configuration

Pick the configuration file that matches your target architecture from `config/`. For a 64-bit
x86 Linux host:

```bash
make LINUX_X86_64_config
```

This copies the toolchain settings into `config/config.mk`, which is automatically included by the
project-level `Makefile`.

## 3. Build the libraries and samples

```bash
make -j"$(nproc)"
```

The default target produces `libtrdp.a` plus a collection of diagnostic binaries under
`bld/output/linux-x86_64-rel/`. (As of release 3.0.0.0 the archive does not provide a separate
`libtrdpap.a`; only `libtrdp.a` is emitted.)

## 4. Stage the install tree that CMake will discover

Choose a prefix (e.g. `/opt/trdp-3.0.0.0` or `$HOME/.local/trdp-3.0.0.0`) and populate it with the
headers and archive built above:

```bash
export TRDP_ROOT=/opt/trdp-3.0.0.0
sudo mkdir -p "$TRDP_ROOT/include/trdp" "$TRDP_ROOT/lib"
sudo rsync -a src/api/ "$TRDP_ROOT/include/trdp/"
sudo cp bld/output/linux-x86_64-rel/libtrdp.a "$TRDP_ROOT/lib/"
```

The backend assumes that headers live directly under `include/trdp/` (for example the public
`trdp_if_light.h`, `tau_dnr.h`, and related API headers that ship with the archive) and that the
static archives reside in `lib/`.

## 5. Point CMake at the installation

Use the `TRDP_ROOT` cache entry (or environment variable) when configuring the backend. When the
layout is correct you will see the `FindTRDP` module report success:

```bash
cmake -S backend -B backend/build -DTRDP_ROOT="$TRDP_ROOT"
# ...
# -- Found TRDP: /opt/trdp-3.0.0.0/include/trdp
```

You can also set `TRDP_ROOT` in the environment (`TRDP_ROOT=/opt/trdp-3.0.0.0 cmake ...`) if you do
not want to pass the cache entry each time. The resulting build tree links against
`TRDP::trdp`, which wraps `/opt/trdp-3.0.0.0/lib/libtrdp.a` and exports the include path found above.
