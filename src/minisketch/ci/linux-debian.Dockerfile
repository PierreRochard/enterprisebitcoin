FROM debian:stable

RUN dpkg --add-architecture i386
RUN dpkg --add-architecture s390x

# dkpg-dev: to make pkg-config work in cross-builds
RUN apt-get update && \
    apt-get install --no-install-recommends --no-upgrade -y \
        git ca-certificates \
        make automake libtool pkg-config dpkg-dev valgrind qemu-user \
        gcc g++ clang libclang-rt-dev libc6-dbg \
        gcc-i686-linux-gnu g++-i686-linux-gnu libc6-dev-i386-cross libc6-dbg:i386 \
        g++-s390x-linux-gnu libstdc++6:s390x gcc-s390x-linux-gnu libc6-dev-s390x-cross libc6-dbg:s390x \
        wine wine64 g++-mingw-w64-x86-64 && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

RUN useradd --create-home --shell /bin/bash ci && \
    install -d -o ci -g ci \
        /ci_container_base \
        /ci_container_base/ci \
        /ci_container_base/ci/scratch \
        /ci_container_base/ci/scratch/ccache \
        /ci_container_base/depends \
        /ci_container_base/depends/built \
        /ci_container_base/depends/sources \
        /ci_container_base/prev_releases

WORKDIR /home/ci
USER ci

# Run a dummy command in wine to make it set up configuration
RUN wine true || true
