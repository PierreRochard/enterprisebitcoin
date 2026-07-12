#ifndef BITCOIN_ENTERPRISE_PQXX_COMPAT_H
#define BITCOIN_ENTERPRISE_PQXX_COMPAT_H

#if defined(BITCOIN_LIBPQXX_DISABLE_SOURCE_LOCATION)
// libpqxx 7.8 infers this ABI from the consumer's language mode. Match a
// library that the configure-time link probe identified as a C++17 build.
#include <version>
#if defined(__cpp_lib_source_location)
#pragma push_macro("__cpp_lib_source_location")
#define BITCOIN_PQXX_RESTORE_SOURCE_LOCATION_MACRO
#undef __cpp_lib_source_location
#endif
#endif

#include <pqxx/pqxx>
#include <pqxx/version>

#if defined(BITCOIN_PQXX_RESTORE_SOURCE_LOCATION_MACRO)
#pragma pop_macro("__cpp_lib_source_location")
#undef BITCOIN_PQXX_RESTORE_SOURCE_LOCATION_MACRO
#endif

#include <utility>

namespace enterprise {

template <typename... Args>
pqxx::result ExecPrepared(pqxx::transaction_base& transaction, const char* statement, Args&&... args)
{
#if PQXX_VERSION_MAJOR > 7 || (PQXX_VERSION_MAJOR == 7 && PQXX_VERSION_MINOR >= 10)
    return transaction.exec(
        pqxx::prepped{statement},
        pqxx::params{std::forward<Args>(args)...});
#else
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    auto result{transaction.exec_prepared(statement, std::forward<Args>(args)...)};
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    return result;
#endif
}

} // namespace enterprise

#endif // BITCOIN_ENTERPRISE_PQXX_COMPAT_H
