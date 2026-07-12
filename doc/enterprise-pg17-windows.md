# PostgreSQL 17 Windows mainnet rollout

This rollout reuses only the native PostgreSQL 17 `bitcoin_enterprise`
database. It preserves the existing `blocks` table and `prices` materialized
view. Never run `src/enterprise/schema.sql` against this database: that file is
the destructive fresh-database bootstrap and intentionally uses an ordinary
`prices` table. The reused PostgreSQL 17 database instead requires the guarded
additive migration and retains its materialized view. Never point these scripts
at another database or at a PostgreSQL 18 container.

All commands below are run from the repository root in PowerShell 7. Start with
`-DryRun` where it is offered. None of the tracked scripts starts the production
node implicitly.

## 1. Create and verify the runtime layout

The ignored `.env` must contain exactly the connection inputs expected by the
node: `PGHOST`, `PGPORT`, `PGDB`, `PGUSER`, and `PGPASSWORD`. Use one unquoted
`KEY=VALUE` per line: `export`, surrounding whitespace or quotes, duplicate or
unknown keys, inline comments, and values containing whitespace, `#`, or quotes
are rejected. Run `Preflight` before any backup or rollout action; it fails
without printing a rejected value.

```powershell
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_runtime.ps1 -Action Preflight
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_runtime.ps1 -Action Initialize -ConfirmInitialize
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_runtime.ps1 -Action Verify
```

Initialization creates `.runtime/mainnet/{node,blocks-root}`, copies `.env` to
`.runtime/mainnet/enterprise.env`, and limits both secret files to the current
Windows identity and SYSTEM. It refuses to initialize if chain data is already
present. The generated configuration uses a fresh mainnet datadir, outbound-only
networking, no wallet, no optional Bitcoin indexes, 20 GiB pruning, a 4 GiB
database cache, the enterprise index, a 1 GiB spool ceiling, and denomination
backfill through height 933996. Enterprise mempool export remains explicitly
disabled for this production rollout. Do not load an assumeutxo snapshot.

`.runtime/` is ignored by Git but is still removable by `git clean -fdx`. Treat
the directory as operational state, not as source-controlled recovery media.

## 2. Preflight, snapshot, and verify the physical backup

The physical backup contains every database in the PostgreSQL 17 cluster and
is therefore sensitive. It is retained under `E:\enterprisebitcoin-pg17-backups`
with an ACL limited to the current Windows identity and SYSTEM. Receipts and
small logs live under ignored `.runtime/postgres/`. Preflight rejects user
tablespaces because this rollout intentionally provides no mapping that could
make a disposable restore follow links back to source paths.

```powershell
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_backup.ps1 -Action Preflight -DryRun
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_backup.ps1 -Action Backup -ConfirmBackup -DryRun
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_backup.ps1 -Action Backup -ConfirmBackup
```

`Backup` runs `pg_basebackup` with streamed WAL and a SHA-256 manifest, then
runs `pg_verifybackup`. Read-only preflight queries retain finite timeouts; only
the long-running `BASE_BACKUP` connection has its statement timeout cleared.
Record the receipt path printed by the command. Restore test it on loopback
port 55418 using a disposable copy on `D:`:

```powershell
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_backup.ps1 -Action RestoreTest -ReceiptPath <receipt.json> -DryRun
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_backup.ps1 -Action RestoreTest -ReceiptPath <receipt.json>
```

The clone is started read-only, its cluster identifier and block/price
inventory are compared with the receipt, and it is stopped before the command
returns. Cleanup requires the exact path recorded in the receipt and refuses to
remove a running server or a directory without PostgreSQL backup markers:

```powershell
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_backup.ps1 -Action CleanupRestoreTest -ReceiptPath <receipt.json> -ConfirmCleanupPath <exact-D-path> -DryRun
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_backup.ps1 -Action CleanupRestoreTest -ReceiptPath <receipt.json> -ConfirmCleanupPath <exact-D-path>
```

## 3. Snapshot and apply the additive migration

Stop every existing block writer first. Price-maintenance readers may remain
connected. Preflight checks PostgreSQL 17, the exact database, superuser role,
object kinds, duplicate/null block keys, NULL price rows, and active block
writers.

```powershell
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_migrate.ps1 -Action Preflight
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_migrate.ps1 -Action Snapshot -BackupReceipt <receipt.json>
```

Record the printed snapshot directory. Apply requires both the verified,
restore-tested backup receipt and this snapshot; their PostgreSQL system
identifiers must match the live cluster. The snapshot records the exact backup
receipt and manifest hashes. Apply rejects a changed receipt, a backup older
than seven days, a restore test older than 48 hours, or a snapshot older than
two hours.

```powershell
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_migrate.ps1 -Action Apply -ConfirmDatabase bitcoin_enterprise -BackupReceipt <receipt.json> -SnapshotDirectory <snapshot-directory> -DryRun
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_migrate.ps1 -Action Apply -ConfirmDatabase bitcoin_enterprise -BackupReceipt <receipt.json> -SnapshotDirectory <snapshot-directory>
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_migrate.ps1 -Action Verify
```

The migration is additive, idempotent, and deliberately two-phase. It first
builds and commits the unique block-height index without holding an ALTER lock.
It then runs the short metadata changes and marker in one transaction bounded
by a five-second lock timeout. A metadata failure rolls back that transaction,
but the already valid index may remain for the next idempotent attempt. The
second phase adds the 19 denomination columns, required ingest/gap/mempool
tables and monitoring index, and fingerprints `prices` before and after. The
scripts contain no production cleanup path.

## 4. Launch and monitor

After committed binaries have a clean version string and verified SHA-256
manifest, ask the runtime tool for the manual command. It prints but does not
run it:

```powershell
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_runtime.ps1 -Action LaunchCommand -BitcoindPath <artifact-bitcoind.exe>
```

Monitor once, then start the watch loop. Supplying `-EnableStop` requires the
exact resolved datadir confirmation and allows only a graceful `bitcoin-cli
stop` below 100 GiB free. The monitor warns below 150 GiB, records node, SQL,
price, WAL, ingest, gap, and spool metrics, and never deletes database or spool
files. Each sample also rejects NULL rows in `prices` and checks a bounded tail
of processed blocks for missing `btc_usd_price` on or after 2009-01-07; blocks
before the first available price day may remain NULL.

```powershell
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_monitor.ps1 -Action Once -BitcoinCliPath <artifact-bitcoin-cli.exe>
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_monitor.ps1 -Action Watch -BitcoinCliPath <artifact-bitcoin-cli.exe> -EnableStop -ConfirmDatadir <absolute-repo-path>\.runtime\mainnet\node
```

After backfill succeeds through 933996, stop the node, regenerate the config,
verify it with the same option, and restart manually:

```powershell
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_runtime.ps1 -Action Configure -BackfillHeight -1 -ConfirmConfigUpdate -ForceConfig
pwsh -NoProfile -File contrib/devtools/enterprise_pg17_runtime.ps1 -Action Verify -BackfillHeight -1
```

Retain the verified pre-migration backup. After stopping the node, create a
post-IBD backup only with `-SafetyMarginGiB 100`; its preflight enforces that
free space exceeds the measured cluster database size plus 100 GiB.

## Rollback after database writes begin

Rollback is an explicit administrator operation, never an automatic script
action. Stop the native node first. Start the verified pre-migration backup
clone and export only `bitcoin_enterprise`. Stop dependent clients, rename the
affected production database without deleting it, create a fresh
`bitcoin_enterprise`, and restore the export into that new database. Re-run the
schema, role, block-height/hash, price, and dependent-view checks before moving
clients to the restored database. Resume the node only after those checks pass;
retain both the renamed affected database and the physical backup until the
restored service has completed its soak.
