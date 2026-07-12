FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential \
       ca-certificates \
       cmake \
       git \
       libboost-dev \
       libevent-dev \
       libminiupnpc-dev \
       libnatpmp-dev \
       libpq-dev \
       libpqxx-dev \
       libsqlite3-dev \
       ninja-build \
       pkg-config \
       python3 \
       systemtap-sdt-dev \
       zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /enterprisebitcoin
COPY . .

RUN cmake -S . -B build -GNinja \
      -DBUILD_GUI=OFF \
      -DENABLE_WALLET=OFF \
      -DENABLE_IPC=OFF \
      -DWITH_ENTERPRISE_SQL=ON \
      -DWITH_ZMQ=OFF \
    && cmake --build build --target bitcoind

RUN mkdir -p /root/.bitcoin

ENV MALLOC_ARENA_MAX=1
VOLUME ["/root/.bitcoin"]
EXPOSE 8332

CMD ["./build/bin/bitcoind", "-dbcache=14000", "-maxmempool=300", "-txindex", "-coinstatsindex"]
