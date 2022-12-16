
Setup PostgreSQL
==

```postgresql
create user USER with encrypted password 'PASSWORD';
GRANT pg_read_all_data TO USER;
GRANT pg_write_all_data TO USER;
alter user USER createdb;
```

macOS
==

```commandline
brew install libpqxx libpq postgresql 
```

add to .zshrc

```shell

export PATH=/usr/bin:$PATH

export LIBRARY_PATH="$(brew --prefix)/lib"

export LIBRARY_PATH="$LIBRARY_PATH:$(brew --prefix libpq)/lib"

export PQ_LIB_DIR="$(brew --prefix libpq)/lib"
```

Linux
==

0.

`sudo apt-get update && sudo apt-get upgrade && sudo apt-get install nano tmux git build-essential libtool autotools-dev automake pkg-config bsdmainutils python3 libssl-dev libevent-dev libboost-system-dev libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev
`

Build
==
1. `./autogen.sh`
2. `./configure --enable-debug`
3. `make -j$(nproc) `