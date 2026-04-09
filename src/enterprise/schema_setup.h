#ifndef ENTERPRISE_SCHEMA_SETUP_H
#define ENTERPRISE_SCHEMA_SETUP_H

#include <pqxx/pqxx>

#include <string_view>

namespace enterprise {

void EnsureBlockExportSchema(pqxx::connection& conn, std::string_view network);
void EnsureUtxoSnapshotSchema(pqxx::connection& conn, std::string_view network);

} // namespace enterprise

#endif // ENTERPRISE_SCHEMA_SETUP_H
