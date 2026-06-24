# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

#[=======================================================================[
FindLibPQXX
-----------

Finds the libpqxx C++ client library for PostgreSQL.

#]=======================================================================]

find_package(PkgConfig REQUIRED)

if(APPLE)
  foreach(_libpq_prefix IN ITEMS
    /opt/homebrew/opt/libpq
    /usr/local/opt/libpq
    /opt/homebrew/opt/postgresql
    /usr/local/opt/postgresql
    /opt/homebrew/opt/postgresql@18
    /usr/local/opt/postgresql@18
    /opt/homebrew/opt/postgresql@17
    /usr/local/opt/postgresql@17
    /opt/homebrew/opt/postgresql@16
    /usr/local/opt/postgresql@16
  )
    if(EXISTS "${_libpq_prefix}/lib/pkgconfig")
      list(APPEND _libpq_pkg_config_paths "${_libpq_prefix}/lib/pkgconfig")
    endif()
    if(EXISTS "${_libpq_prefix}/lib/postgresql/pkgconfig")
      list(APPEND _libpq_pkg_config_paths "${_libpq_prefix}/lib/postgresql/pkgconfig")
    endif()
  endforeach()
  if(_libpq_pkg_config_paths)
    list(JOIN _libpq_pkg_config_paths ":" _libpq_pkg_config_path)
    if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
      set(ENV{PKG_CONFIG_PATH} "${_libpq_pkg_config_path}:$ENV{PKG_CONFIG_PATH}")
    else()
      set(ENV{PKG_CONFIG_PATH} "${_libpq_pkg_config_path}")
    endif()
  endif()
endif()

pkg_check_modules(libpqxx REQUIRED
  IMPORTED_TARGET
  libpqxx
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibPQXX
  REQUIRED_VARS libpqxx_LIBRARY_DIRS
  VERSION_VAR libpqxx_VERSION
)

if(LibPQXX_FOUND AND NOT TARGET LibPQXX::pqxx)
  add_library(LibPQXX::pqxx ALIAS PkgConfig::libpqxx)
endif()
