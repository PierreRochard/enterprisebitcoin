[CmdletBinding()]
param(
    [ValidateSet('Preflight', 'Snapshot', 'Apply', 'Verify')]
    [string] $Action = 'Preflight',
    [string] $EnvFile = '.env',
    [string] $ExpectedDatabase = 'bitcoin_enterprise',
    [string] $PsqlPath,
    [string] $PgDumpPath,
    [string] $OutputDirectory = '.runtime/postgres/migrations',
    [string] $SnapshotDirectory,
    [string] $BackupReceipt,
    [string] $ConfirmDatabase,
    [switch] $DryRun
)

. (Join-Path $PSScriptRoot 'enterprise_pg17_common.ps1')

$repoRoot = Get-EnterpriseRepoRoot
if ($ExpectedDatabase -cne 'bitcoin_enterprise') {
    throw 'This rollout is intentionally limited to bitcoin_enterprise.'
}
$migration = Join-Path $repoRoot 'src\enterprise\migrations\20260711_pg17_reuse_v1.sql'
$verification = Join-Path $repoRoot 'src\enterprise\migrations\verify_20260711_pg17_reuse_v1.sql'
$envPath = Resolve-EnterpriseRepoPath -Path $EnvFile -MustExist
$pgEnv = Import-EnterprisePgEnv -Path $envPath -ExpectedDatabase $ExpectedDatabase
$psql = Find-EnterprisePgTool -Name 'psql' -ExplicitPath $PsqlPath
Assert-EnterprisePg17Tool -Path $psql -Name 'psql'

$readOnlyOptions = '-c default_transaction_read_only=on -c statement_timeout=300000 -c lock_timeout=5000 -c search_path=pg_catalog,public'
$writeOptions = '-c default_transaction_read_only=off -c statement_timeout=1800000 -c lock_timeout=5000 -c search_path=pg_catalog,public'
$previousEnvironment = Set-EnterprisePgProcessEnvironment -Values $pgEnv -ApplicationName 'enterprise-pg17-migrate' -Options $readOnlyOptions

$preflightSql = @'
DO $preflight$
DECLARE
    block_writer_count integer;
BEGIN
    IF current_database() <> 'bitcoin_enterprise' THEN
        RAISE EXCEPTION 'connected database is not the expected database';
    END IF;
    IF current_setting('server_version_num')::integer < 170000 OR
       current_setting('server_version_num')::integer >= 180000 THEN
        RAISE EXCEPTION 'PostgreSQL major version 17 is required';
    END IF;
    IF session_user <> 'postgres' OR NOT EXISTS (
        SELECT 1 FROM pg_catalog.pg_roles WHERE rolname = session_user AND rolsuper
    ) THEN
        RAISE EXCEPTION 'the postgres superuser is required';
    END IF;
    IF (SELECT c.relkind FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n
          ON n.oid = c.relnamespace WHERE n.nspname = 'public' AND c.relname = 'blocks') <> 'r' THEN
        RAISE EXCEPTION 'public.blocks is not an ordinary table';
    END IF;
    IF (SELECT c.relkind FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n
          ON n.oid = c.relnamespace WHERE n.nspname = 'public' AND c.relname = 'prices') <> 'm' THEN
        RAISE EXCEPTION 'public.prices is not the expected materialized view';
    END IF;
    IF EXISTS (SELECT 1 FROM public.prices WHERE price IS NULL LIMIT 1) THEN
        RAISE EXCEPTION 'public.prices contains NULL price rows';
    END IF;
    SELECT count(*) INTO block_writer_count
      FROM pg_catalog.pg_stat_activity
     WHERE datname = current_database() AND pid <> pg_backend_pid()
       AND (application_name = 'enterprise-bitcoind'
            OR (state <> 'idle' AND query ~* '(insert|update|delete)[[:space:][:print:]]*blocks'));
    IF block_writer_count <> 0 THEN
        RAISE EXCEPTION 'a block writer is active; stop it before migration';
    END IF;
    IF EXISTS (SELECT 1 FROM public.blocks WHERE network IS NULL OR height IS NULL LIMIT 1) THEN
        RAISE EXCEPTION 'public.blocks contains NULL network/height keys';
    END IF;
    IF EXISTS (SELECT 1 FROM public.blocks GROUP BY network, height HAVING count(*) > 1 LIMIT 1) THEN
        RAISE EXCEPTION 'public.blocks contains duplicate network/height keys';
    END IF;
END
$preflight$;

SELECT current_setting('server_version') AS server_version,
       current_database() AS database,
       session_user AS session_user,
       inet_server_addr() AS server_address,
       inet_server_port() AS server_port,
       current_setting('data_checksums') AS data_checksums,
       (SELECT system_identifier FROM pg_catalog.pg_control_system()) AS system_identifier;
SELECT count(*) AS blocks_rows,
       count(DISTINCT (network, height)) AS distinct_network_heights,
       min(height) FILTER (WHERE network = 'mainnet') AS mainnet_min_height,
       max(height) FILTER (WHERE network = 'mainnet') AS mainnet_max_height
  FROM public.blocks;
SELECT count(*) AS prices_rows, min(day) AS first_price_day, max(day) AS last_price_day,
       count(*) FILTER (WHERE price IS NULL) AS null_prices
  FROM public.prices;
'@

function Invoke-Preflight {
    $result = Invoke-EnterprisePsql -Psql $psql -Sql $preflightSql -ExpectedDatabase $ExpectedDatabase
    $result | Write-Output
}

function ConvertTo-EnterpriseUtcDateTimeOffset {
    param(
        [Parameter(Mandatory)]
        [object] $Value,
        [Parameter(Mandatory)]
        [string] $Description
    )

    try {
        if ($Value -is [DateTimeOffset]) {
            return ([DateTimeOffset] $Value).ToUniversalTime()
        }
        if ($Value -is [DateTime]) {
            $dateTime = [DateTime] $Value
            if ($dateTime.Kind -eq [DateTimeKind]::Unspecified) {
                $dateTime = [DateTime]::SpecifyKind($dateTime, [DateTimeKind]::Utc)
            }
            return [DateTimeOffset]::new($dateTime.ToUniversalTime())
        }
        return [DateTimeOffset]::Parse(
            [string] $Value,
            [System.Globalization.CultureInfo]::InvariantCulture,
            [System.Globalization.DateTimeStyles]::AssumeUniversal -bor
                [System.Globalization.DateTimeStyles]::AdjustToUniversal
        )
    } catch {
        throw "$Description has an invalid timestamp."
    }
}

function Get-ValidatedBackupBinding {
    if (-not $BackupReceipt) { throw '-BackupReceipt is required for Snapshot and Apply.' }
    $receiptPath = Resolve-EnterpriseRepoPath -Path $BackupReceipt -MustExist
    $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    if ($receipt.kind -ne 'enterprise-pg17-physical-backup' -or
        $receipt.expected_database -ne $ExpectedDatabase -or
        -not $receipt.completed_at_utc -or -not $receipt.verified_at_utc -or
        -not $receipt.restore_tested_at_utc -or -not $receipt.restored_inventory) {
        throw 'Backup receipt is not a completed, verified, restore-tested PG17 backup for this database.'
    }

    $completedAt = ConvertTo-EnterpriseUtcDateTimeOffset `
        -Value $receipt.completed_at_utc -Description 'Backup completion receipt'
    $verifiedAt = ConvertTo-EnterpriseUtcDateTimeOffset `
        -Value $receipt.verified_at_utc -Description 'Backup verification receipt'
    $restoredAt = ConvertTo-EnterpriseUtcDateTimeOffset `
        -Value $receipt.restore_tested_at_utc -Description 'Backup restore-test receipt'
    $now = [DateTimeOffset]::UtcNow
    if ($completedAt -gt $verifiedAt -or $verifiedAt -gt $restoredAt -or $restoredAt -gt $now.AddMinutes(5) -or
        ($now - $completedAt).TotalDays -gt 7 -or ($now - $restoredAt).TotalHours -gt 48) {
        throw 'Backup/verify/restore timestamps are out of order or too old for this rollout.'
    }

    $backupRoot = [System.IO.Path]::GetFullPath('E:\enterprisebitcoin-pg17-backups').TrimEnd('\') + '\'
    $backupPath = [System.IO.Path]::GetFullPath([string] $receipt.backup_path)
    $manifestPath = Join-Path $backupPath 'backup_manifest'
    if (-not $backupPath.StartsWith($backupRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
        -not ([System.IO.Path]::GetFileName($backupPath)).StartsWith('pg17-base-', [System.StringComparison]::Ordinal) -or
        -not (Test-Path -LiteralPath $manifestPath -PathType Leaf) -or
        (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash -cne [string] $receipt.backup_manifest_sha256) {
        throw 'Retained physical backup is missing, outside the approved E: root, or changed.'
    }
    if (((Get-Item -LiteralPath $backupRoot.TrimEnd('\') -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Approved E: backup root must not be a reparse point.'
    }
    if (((Get-Item -LiteralPath $backupPath -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Retained physical backup path must not be a reparse point.'
    }

    $currentSql = @'
SELECT (SELECT system_identifier FROM pg_catalog.pg_control_system())::text || '|' ||
       (SELECT count(*) FROM public.blocks)::text || '|' ||
       (SELECT min(height) FROM public.blocks WHERE network = 'mainnet')::text || '|' ||
       (SELECT max(height) FROM public.blocks WHERE network = 'mainnet')::text || '|' ||
       (SELECT count(*) FROM public.prices)::text || '|' ||
       (SELECT min(day) FROM public.prices)::text || '|' ||
       (SELECT max(day) FROM public.prices)::text;
'@
    $currentLine = ((Invoke-EnterprisePsql -Psql $psql -Sql $currentSql -TuplesOnly) -join '').Trim()
    $current = $currentLine.Split('|')
    if ($current.Count -ne 7) { throw 'Could not bind the current database inventory to the restored backup.' }
    $restored = $receipt.restored_inventory
    if ($receipt.system_identifier -cne $current[0] -or
        [int64] $restored.blocks_rows -ne [int64] $current[1] -or
        [int64] $restored.mainnet_min_height -ne [int64] $current[2] -or
        [int64] $restored.mainnet_max_height -ne [int64] $current[3] -or
        [int64] $current[4] -lt [int64] $restored.prices_rows -or
        [string] $current[5] -cne [string] $restored.first_price_day -or
        [DateTime]::Parse([string] $current[6]) -lt [DateTime]::Parse([string] $restored.last_price_day)) {
        throw 'Current block/price inventory is incompatible with the authoritative restored-backup inventory.'
    }
    return [pscustomobject]@{
        path = $receiptPath
        receipt = $receipt
        sha256 = (Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash
        current_system_identifier = $current[0]
        completed_at = $completedAt
        restored_at = $restoredAt
    }
}

function New-InventorySnapshot {
    $backupBinding = Get-ValidatedBackupBinding
    $base = if ($SnapshotDirectory) {
        Resolve-EnterpriseRepoPath -Path $SnapshotDirectory
    } else {
        $root = Resolve-EnterpriseRepoPath -Path $OutputDirectory
        Join-Path $root ("pre-migration-{0:yyyyMMddTHHmmssZ}" -f [DateTime]::UtcNow)
    }
    if (Test-Path -LiteralPath $base) { throw "Snapshot directory already exists: $base" }
    [void] (New-Item -ItemType Directory -Path $base)
    Protect-EnterpriseDirectory -Path $base

    $pgDump = Find-EnterprisePgTool -Name 'pg_dump' -ExplicitPath $PgDumpPath
    Assert-EnterprisePg17Tool -Path $pgDump -Name 'pg_dump'
    $schemaPath = Join-Path $base 'schema-only.sql'
    & $pgDump -w --schema-only --file=$schemaPath
    if ($LASTEXITCODE -ne 0) { throw 'pg_dump schema snapshot failed.' }

    $inventorySql = @'
SELECT 'identity', current_setting('server_version'), current_database(), session_user,
       inet_server_addr()::text, inet_server_port()::text,
       (SELECT system_identifier::text FROM pg_catalog.pg_control_system());
SELECT 'role', rolname, rolsuper::text, rolinherit::text, rolcreaterole::text,
       rolcreatedb::text, rolcanlogin::text, rolreplication::text, rolconnlimit::text
  FROM pg_catalog.pg_roles ORDER BY rolname;
SELECT 'setting', name, setting, unit, source
  FROM pg_catalog.pg_settings WHERE source <> 'default' ORDER BY name;
SELECT 'block_inventory', count(*)::text, count(DISTINCT (network,height))::text,
       min(height)::text, max(height)::text FROM public.blocks;
SELECT 'price_inventory', count(*)::text, min(day)::text, max(day)::text,
       count(*) FILTER (WHERE price IS NULL)::text FROM public.prices;
SELECT 'dependency', pg_catalog.pg_describe_object(classid, objid, objsubid),
       pg_catalog.pg_describe_object(refclassid, refobjid, refobjsubid), deptype::text
  FROM pg_catalog.pg_depend
 WHERE refobjid IN ('public.blocks'::pg_catalog.regclass, 'public.prices'::pg_catalog.regclass)
 ORDER BY 1, 2;
SELECT 'session', usename, application_name, client_addr::text, state,
       backend_type, wait_event_type, wait_event
  FROM pg_catalog.pg_stat_activity WHERE datname = current_database() ORDER BY pid;
'@
    $inventoryPath = Join-Path $base 'inventory.txt'
    (Invoke-EnterprisePsql -Psql $psql -Sql $inventorySql) |
        Set-Content -LiteralPath $inventoryPath -Encoding utf8

    $blockInventoryPath = Join-Path $base 'block-height-hash.csv'
    $copySql = @'
COPY (
    SELECT network, height, hash
      FROM public.blocks
     ORDER BY network, height
) TO STDOUT WITH (FORMAT csv, HEADER true)
'@
    & $psql -X -w -v ON_ERROR_STOP=1 -c $copySql |
        Set-Content -LiteralPath $blockInventoryPath -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw 'Full block height/hash inventory export failed.' }

    $diskPath = Join-Path $base 'disk.json'
    Get-PSDrive -PSProvider FileSystem |
        Select-Object Name, Root, Used, Free |
        ConvertTo-Json | Set-Content -LiteralPath $diskPath -Encoding utf8

    $systemId = ((Invoke-EnterprisePsql -Psql $psql `
        -Sql 'SELECT system_identifier FROM pg_catalog.pg_control_system()' `
        -TuplesOnly) -join '').Trim()
    $files = Get-ChildItem -LiteralPath $base -File | ForEach-Object {
        [ordered]@{ name = $_.Name; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    }
    $marker = [ordered]@{
        kind = 'enterprise-pg17-pre-migration-snapshot'
        expected_database = $ExpectedDatabase
        system_identifier = $systemId
        created_at_utc = [DateTime]::UtcNow.ToString('o')
        backup_id = $backupBinding.receipt.backup_id
        backup_receipt_sha256 = $backupBinding.sha256
        backup_manifest_sha256 = $backupBinding.receipt.backup_manifest_sha256
        backup_completed_at_utc = $backupBinding.receipt.completed_at_utc
        backup_restore_tested_at_utc = $backupBinding.receipt.restore_tested_at_utc
        files = @($files)
    }
    Write-EnterpriseJsonAtomic -Value $marker -Path (Join-Path $base 'snapshot.json')
    foreach ($snapshotFile in Get-ChildItem -LiteralPath $base -File) {
        Protect-EnterpriseSecretFile -Path $snapshotFile.FullName
    }
    Write-Output "Created pre-migration snapshot: $base"
}

function Assert-RolloutReceipts {
    if (-not $SnapshotDirectory) { throw '-SnapshotDirectory is required for Apply.' }
    $backupBinding = Get-ValidatedBackupBinding
    $snapshotRoot = Resolve-EnterpriseRepoPath -Path $SnapshotDirectory -MustExist
    $snapshotMarkerPath = Join-Path $snapshotRoot 'snapshot.json'
    if (-not (Test-Path -LiteralPath $snapshotMarkerPath -PathType Leaf)) {
        throw 'Snapshot directory does not contain snapshot.json.'
    }
    $snapshot = Get-Content -LiteralPath $snapshotMarkerPath -Raw | ConvertFrom-Json
    if ($snapshot.kind -ne 'enterprise-pg17-pre-migration-snapshot' -or
        $snapshot.expected_database -ne $ExpectedDatabase) {
        throw 'Snapshot marker does not match this rollout.'
    }
    $snapshotCreatedAt = ConvertTo-EnterpriseUtcDateTimeOffset `
        -Value $snapshot.created_at_utc -Description 'Snapshot marker'
    $now = [DateTimeOffset]::UtcNow
    if ($snapshotCreatedAt -lt $backupBinding.restored_at -or
        $snapshotCreatedAt -lt $backupBinding.completed_at -or
        $snapshotCreatedAt -gt $now.AddMinutes(5) -or
        ($now - $snapshotCreatedAt).TotalHours -gt 2 -or
        $snapshot.backup_id -cne $backupBinding.receipt.backup_id -or
        $snapshot.backup_receipt_sha256 -cne $backupBinding.sha256 -or
        $snapshot.backup_manifest_sha256 -cne $backupBinding.receipt.backup_manifest_sha256 -or
        $snapshot.backup_completed_at_utc -cne $backupBinding.receipt.completed_at_utc -or
        $snapshot.backup_restore_tested_at_utc -cne $backupBinding.receipt.restore_tested_at_utc) {
        throw 'Snapshot is stale, predates its restored backup, or is not bound to the exact current backup receipt.'
    }
    foreach ($file in $snapshot.files) {
        if ([System.IO.Path]::GetFileName([string] $file.name) -cne [string] $file.name) {
            throw 'Snapshot marker contains an unsafe file name.'
        }
        $snapshotFile = Join-Path $snapshotRoot ([string] $file.name)
        if (-not (Test-Path -LiteralPath $snapshotFile -PathType Leaf) -or
            (Get-FileHash -LiteralPath $snapshotFile -Algorithm SHA256).Hash -cne [string] $file.sha256) {
            throw "Snapshot file is missing or changed: $($file.name)"
        }
    }
    $recordedSnapshotNames = @($snapshot.files | ForEach-Object { [string] $_.name })
    foreach ($requiredName in @('schema-only.sql', 'inventory.txt', 'block-height-hash.csv', 'disk.json')) {
        if ($requiredName -cnotin $recordedSnapshotNames) {
            throw "Snapshot marker is missing required artifact: $requiredName"
        }
    }
    if ($snapshot.system_identifier -cne $backupBinding.current_system_identifier) {
        throw 'Backup/snapshot system identifier does not match the connected cluster.'
    }
}

try {
    switch ($Action) {
        'Preflight' {
            Invoke-Preflight
            if ($DryRun) { Write-Output 'Dry run complete; no migration was applied.' }
        }
        'Snapshot' {
            Invoke-Preflight
            if ($DryRun) {
                [void] (Get-ValidatedBackupBinding)
                Write-Output 'Dry run complete; backup binding passed and no snapshot files were written.'
            }
            else { New-InventorySnapshot }
        }
        'Verify' {
            $result = Invoke-EnterprisePsql -Psql $psql -File $verification -ExpectedDatabase $ExpectedDatabase
            $result | Write-Output
        }
        'Apply' {
            if ($ConfirmDatabase -cne $ExpectedDatabase) {
                throw "Apply requires -ConfirmDatabase $ExpectedDatabase."
            }
            Invoke-Preflight
            Assert-RolloutReceipts
            if ($DryRun) {
                Write-Output 'Dry run complete; receipts and preflight passed, but migration was not applied.'
                break
            }
            Restore-EnterpriseProcessEnvironment -Previous $previousEnvironment
            $previousEnvironment = Set-EnterprisePgProcessEnvironment -Values $pgEnv `
                -ApplicationName 'enterprise-pg17-migrate' -Options $writeOptions
            $result = Invoke-EnterprisePsql -Psql $psql -File $migration -ExpectedDatabase $ExpectedDatabase
            $result | Write-Output
            Restore-EnterpriseProcessEnvironment -Previous $previousEnvironment
            $previousEnvironment = Set-EnterprisePgProcessEnvironment -Values $pgEnv `
                -ApplicationName 'enterprise-pg17-verify' -Options $readOnlyOptions
            $verifyResult = Invoke-EnterprisePsql -Psql $psql -File $verification -ExpectedDatabase $ExpectedDatabase
            $verifyResult | Write-Output
        }
    }
} finally {
    Restore-EnterpriseProcessEnvironment -Previous $previousEnvironment
}
