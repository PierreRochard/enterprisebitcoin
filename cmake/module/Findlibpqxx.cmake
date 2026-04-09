# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

#[=======================================================================[
Findlibpqxx
-----------

Finds the libpqxx headers and library.

This is a wrapper around find_package()/pkg_check_modules() commands that:
 - facilitates searching in various build environments
 - prints a standard log message

#]=======================================================================]

include(FindPackageHandleStandardArgs)

find_package(libpqxx ${libpqxx_FIND_VERSION} NO_MODULE QUIET)
if(libpqxx_FOUND)
  find_package_handle_standard_args(libpqxx
    REQUIRED_VARS libpqxx_DIR
    VERSION_VAR libpqxx_VERSION
  )
  mark_as_advanced(libpqxx_DIR)
else()
  set(_libpqxx_pkg libpqxx)
  if(libpqxx_FIND_VERSION)
    string(APPEND _libpqxx_pkg ">=${libpqxx_FIND_VERSION}")
  endif()

  find_package(PkgConfig REQUIRED)
  pkg_check_modules(libpqxx REQUIRED QUIET
    IMPORTED_TARGET GLOBAL
    ${_libpqxx_pkg}
  )
  find_package_handle_standard_args(libpqxx
    REQUIRED_VARS libpqxx_LIBRARY_DIRS
    VERSION_VAR libpqxx_VERSION
  )
  if(TARGET PkgConfig::libpqxx AND NOT TARGET libpqxx::pqxx)
    add_library(libpqxx::pqxx ALIAS PkgConfig::libpqxx)
  endif()

  unset(_libpqxx_pkg)
endif()
