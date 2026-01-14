# AGENTS.md

This repository is the Bitcoin Core integration/staging tree. Use the docs
listed below as the source of truth for workflow, build, test, and style.

Key docs
- README.md (project overview, testing entry points)
- CONTRIBUTING.md (workflow, commit hygiene, PR conventions)
- doc/developer-notes.md (C++/Python style)
- doc/build-*.md (platform build guides)
- test/README.md (how to run tests)
- doc/bitcoin-conf.md, doc/files.md (config and datadir layout)
- src/enterprise/README.md (enterprise SQL exporter setup)

Repository map
- src/            Core node, wallet, RPC, validation, net, and utilities.
- src/qt/         GUI sources (GUI-only changes generally belong in the
                  bitcoin-core/gui repo unless build or interface changes).
- src/enterprise/ PostgreSQL exporters and schema helpers (ENABLE_ENTERPRISE).
- doc/            Documentation and design notes.
- test/           Functional, fuzz, and lint test suites.
- contrib/        Dev tools and helper scripts.
- depends/        Depends build system for deterministic builds.

Build (Windows, MSVC)
- CMake presets are in CMakePresets.json (see doc/build-windows-msvc.md).
- Example (Release):
  - cmake -B build --preset vs2022
  - cmake --build build --config Release
  - ctest --test-dir build --build-config Release

Build (Unix-like)
- See doc/build-unix.md or platform-specific docs in doc/build-*.md.
- Typical autotools flow (when applicable):
  - ./autogen.sh
  - ./configure [options]
  - make -j<N>

Testing
- Unit tests: ctest (from the build directory).
- Functional tests: build/test/functional/test_runner.py
- Fuzzing: see doc/fuzzing.md
- Lint: see test/lint/README.md
- On Windows, functional tests require PYTHONUTF8=1 (see test/README.md).

Style and conventions
- Follow doc/developer-notes.md and src/.clang-format for C++ style.
- Prefer minimal, focused diffs; avoid style-only or refactor-only changes.
- Use the naming conventions in developer notes (m_ for members, g_ globals).
- Translation changes should go through Transifex, not direct edits.

Config and data
- bitcoin.conf format and precedence: doc/bitcoin-conf.md.
- Datadir layout and defaults: doc/files.md (use -datadir to override).
- Logs are written to debug.log inside the datadir.

Enterprise PostgreSQL exporters
- Build option: ENABLE_ENTERPRISE=ON (CMake option in CMakeLists.txt).
- Uses libpqxx and a .env-driven connection string (PGDB, PGUSER, PGPASSWORD,
  PGHOST, PGPORT) via src/enterprise/db.h.
- Schema helpers live in src/enterprise/schema.sql and utxo_stats_schemas.sql.
