/* This file was generated by ODB, object-relational mapping (ORM)
 * compiler for C++.
 */

DROP TABLE IF EXISTS "bitcoin"."eInputs" CASCADE;

DROP TABLE IF EXISTS "schema_version";

CREATE TABLE "bitcoin"."eInputs" (
  "id" SERIAL NOT NULL PRIMARY KEY,
  "output_transaction_hash" TEXT NOT NULL,
  "output_vector" INTEGER NOT NULL,
  "block_hash" TEXT NOT NULL,
  "transaction_block_index" INTEGER NOT NULL,
  "transaction_hash" TEXT NOT NULL,
  "vector" INTEGER NOT NULL,
  "unlocking_script_id" TEXT NOT NULL,
  "sequence" INTEGER NOT NULL);

CREATE TABLE "schema_version" (
  "name" TEXT NOT NULL PRIMARY KEY,
  "version" BIGINT NOT NULL,
  "migration" BOOLEAN NOT NULL);

INSERT INTO "schema_version" (
  "name", "version", "migration")
  VALUES ('', 1, FALSE);

