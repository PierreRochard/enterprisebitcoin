#ifdef ODB_COMPILER
#define ENTERPRISE_SCHEMA_VERSION      0x000001 // 00.00.01
#define ENTERPRISE_SCHEMA_BASE_VERSION 0x000001 // 00.00.01
#pragma db model version(ENTERPRISE_SCHEMA_BASE_VERSION, ENTERPRISE_SCHEMA_VERSION, open)
#endif