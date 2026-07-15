# BlocksDS slim image provides the ARM cross-compiler toolchain
# (arm-none-eabi-gcc), libnds, MaxMod, ndstool, and all headers needed
# to build NDS homebrew. The slim variant omits docs and examples.
FROM skylyrac/blocksds:slim-latest

# gdb-multiarch: multi-architecture GDB for debugging NDS binaries via
# the melonDS GDB stub. Connects from the container to melonDS running
# on the host over host.docker.internal:3333.
RUN apt-get update -qq \
    && apt-get install -y -qq --no-install-recommends gdb-multiarch \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
