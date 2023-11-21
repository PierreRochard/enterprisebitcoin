
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

Upgrade from 15 to 16

brew update
brew install postgresql@16


/opt/homebrew/opt/postgresql@16/bin/pg_upgrade -d /opt/homebrew/var/postgresql@15/ -D /opt/homebrew/var/postgresql@16/ -b /opt/homebrew/Cellar/postgresql@15/15.5/bin/ -B /opt/homebrew/Cellar/postgresql@16/16.1/bin/

brew services start postgresql@16

/opt/homebrew/Cellar/postgresql@16/16.1/bin/vacuumdb --all --analyze-in-stages

brew uninstall postgresql@15

/opt/homebrew/Cellar/postgresql@16/16.1/bin/delete_old_cluster.sh

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
2. `./configure --enable-debug --without-bdb --with-gui=no`
3. `make -j$(nproc) `