# Use Ubuntu as the base image
FROM ubuntu:latest

# Avoid prompts from apt
ENV DEBIAN_FRONTEND=noninteractive

# Update the system and install necessary packages
RUN apt-get update && apt-get upgrade -y \
    && apt-get install -y \
       build-essential libtool autotools-dev automake pkg-config bsdmainutils python3 \
       libevent-dev libboost-dev libsqlite3-dev libminiupnpc-dev libnatpmp-dev \
       libzmq3-dev systemtap-sdt-dev git \
       libpqxx-dev libpq-dev \
    && rm -rf /var/lib/apt/lists/*

# Perform a shallow clone of the enterprisebitcoin repository from the libpqxx-master branch
RUN git clone --branch libpqxx-master --depth 1 https://github.com/PierreRochard/enterprisebitcoin.git

# Change working directory to the cloned repository
WORKDIR /enterprisebitcoin

# Copy the .env file from the host to the container's working directory
COPY .env ./

# Run build commands with the '--without-gui' flag
RUN ./autogen.sh \
    && ./configure --without-gui \
    && make -j$(nproc)

# Create the Bitcoin data directory
RUN mkdir -p /root/.bitcoin

# Set environment variable to limit malloc arenas
ENV MALLOC_ARENA_MAX=1

# Define a volume for persisting data
VOLUME ["/root/.bitcoin"]

# Expose the port used by the application
EXPOSE 8332

# Command to run the built application with the optimized parameters
CMD ["./src/bitcoind", "-dbcache=14000", "-maxmempool=300", "-txindex", "-coinstatsindex"]
