[CmdletBinding()]
param(
    [ValidateSet('Once', 'Watch')]
    [string] $Action = 'Once',
    [string] $EnvFile = '.runtime/mainnet/enterprise.env',
    [string] $ExpectedDatabase = 'bitcoin_enterprise',
    [string] $Datadir = '.runtime/mainnet/node',
    [string] $BitcoinCliPath,
    [string] $PsqlPath,
    [int] $IntervalSeconds = 60,
    [int] $WarningFreeGiB = 150,
    [int] $StopFreeGiB = 100,
    [switch] $EnableStop,
    [string] $ConfirmDatadir,
    [string] $LogPath = '.runtime/postgres/logs/mainnet-monitor.jsonl',
    [switch] $NoLog
)

. (Join-Path $PSScriptRoot 'enterprise_pg17_common.ps1')

if ($ExpectedDatabase -cne 'bitcoin_enterprise') {
    throw 'This monitor is intentionally limited to bitcoin_enterprise.'
}
if ($WarningFreeGiB -le $StopFreeGiB -or $StopFreeGiB -le 0) {
    throw 'WarningFreeGiB must be greater than StopFreeGiB, and both must be positive.'
}
if ($Action -eq 'Watch' -and $IntervalSeconds -lt 10) {
    throw 'Watch interval must be at least 10 seconds.'
}

$nodeDatadir = Resolve-EnterpriseRepoPath -Path $Datadir -MustExist
$expectedNodeDatadir = Resolve-EnterpriseRepoPath -Path '.runtime/mainnet/node'
if (-not $nodeDatadir.Equals($expectedNodeDatadir, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Datadir must be the repo-local .runtime/mainnet/node directory.'
}
$spoolPath = Join-Path $nodeDatadir 'enterprise\block_spool'
$envPath = Resolve-EnterpriseRepoPath -Path $EnvFile -MustExist
$pgEnv = Import-EnterprisePgEnv -Path $envPath -ExpectedDatabase $ExpectedDatabase
$psql = Find-EnterprisePgTool -Name 'psql' -ExplicitPath $PsqlPath
Assert-EnterprisePg17Tool -Path $psql -Name 'psql'
$resolvedLogPath = if ($NoLog) { $null } else { Resolve-EnterpriseRepoPath -Path $LogPath }
$previousEnvironment = Set-EnterprisePgProcessEnvironment -Values $pgEnv `
    -ApplicationName 'enterprise-pg17-monitor' `
    -Options '-c default_transaction_read_only=on -c statement_timeout=30000 -c lock_timeout=1000 -c search_path=pg_catalog,public'

function Get-SqlMetrics {
    $sql = @'
WITH ingest_progress AS (
    SELECT max(ingest.height) FILTER (
               WHERE ingest.source = 'denomination-backfill' AND ingest.status = 'succeeded') AS backfill_height,
           max(ingest.height) FILTER (WHERE ingest.status = 'succeeded') AS processed_height
      FROM public.enterprise_block_ingest AS ingest
      JOIN public.blocks AS blocks
        ON blocks.network = ingest.network AND blocks.height = ingest.height
     WHERE ingest.network = 'mainnet'
       AND blocks.denomination_classifier_version = 'denomination-v2'
), available_price_days AS (
    SELECT CASE
               WHEN pg_catalog.pg_typeof(prices.day)::text = 'timestamp with time zone'
                   THEN (prices.day::timestamp with time zone AT TIME ZONE 'UTC')::date
               ELSE prices.day::timestamp without time zone::date
           END AS utc_day
      FROM public.prices AS prices
     WHERE prices.price IS NOT NULL
), price_frontier AS (
    SELECT min(utc_day) AS first_available_day,
           max(utc_day) AS available_through_day
      FROM available_price_days
), priced_null_tail AS (
    SELECT count(*) FILTER (
               WHERE price_frontier.first_available_day IS NOT NULL
                 AND (blocks.time AT TIME ZONE 'UTC')::date
                     BETWEEN price_frontier.first_available_day
                         AND price_frontier.available_through_day
           ) AS overdue_row_count,
           count(*) FILTER (
               WHERE price_frontier.available_through_day IS NULL
                  OR (blocks.time AT TIME ZONE 'UTC')::date > price_frontier.available_through_day
           ) AS pending_row_count
      FROM public.blocks AS blocks
      CROSS JOIN ingest_progress
      CROSS JOIN price_frontier
     WHERE ingest_progress.processed_height IS NOT NULL
       AND blocks.network = 'mainnet'
       AND blocks.height BETWEEN greatest(0, ingest_progress.processed_height - 2047)
                             AND ingest_progress.processed_height
       AND blocks.denomination_classifier_version = 'denomination-v2'
       AND blocks.btc_usd_price IS NULL
)
SELECT pg_catalog.pg_database_size(current_database())::text || '|' ||
       COALESCE((SELECT height FROM public.blocks WHERE network = 'mainnet' ORDER BY height DESC LIMIT 1)::text, '') || '|' ||
       COALESCE(price_frontier.first_available_day::text, '') || '|' ||
       COALESCE(price_frontier.available_through_day::text, '') || '|' ||
       COALESCE(ingest_progress.backfill_height::text, '') || '|' ||
       COALESCE(ingest_progress.processed_height::text, '') || '|' ||
       (SELECT count(*) FROM public.prices WHERE price IS NULL)::text || '|' ||
       priced_null_tail.overdue_row_count::text || '|' ||
       priced_null_tail.pending_row_count::text || '|' ||
       (SELECT count(*) FROM public.enterprise_block_ingest
         WHERE network = 'mainnet' AND status = 'failed')::text || '|' ||
       (SELECT count(*) FROM public.enterprise_block_ingest
         WHERE network = 'mainnet' AND status = 'started')::text || '|' ||
       (SELECT count(*) FROM public.enterprise_block_gaps
         WHERE network = 'mainnet' AND status NOT IN ('resolved', 'succeeded'))::text || '|' ||
       pg_catalog.pg_current_wal_lsn()::text || '|' ||
       (SELECT count(*) FROM pg_catalog.pg_stat_activity
         WHERE datname = current_database() AND application_name = 'enterprise-bitcoind')::text
  FROM ingest_progress CROSS JOIN price_frontier CROSS JOIN priced_null_tail;
'@
    $line = ((Invoke-EnterprisePsql -Psql $psql -Sql $sql -TuplesOnly) -join '').Trim()
    $parts = $line.Split('|')
    if ($parts.Count -ne 14) { throw 'Could not parse PostgreSQL monitor metrics.' }
    return [ordered]@{
        database_bytes = [int64] $parts[0]
        sql_max_height = if ($parts[1]) { [int64] $parts[1] } else { $null }
        first_available_price_day = $parts[2]
        available_price_through_day = $parts[3]
        backfill_succeeded_height = if ($parts[4]) { [int64] $parts[4] } else { $null }
        processed_height = if ($parts[5]) { [int64] $parts[5] } else { $null }
        prices_null_rows = [int64] $parts[6]
        processed_price_null_overdue_tail = [int64] $parts[7]
        processed_price_null_pending_tail = [int64] $parts[8]
        failed_ingests = [int64] $parts[9]
        started_ingests = [int64] $parts[10]
        unresolved_gaps = [int64] $parts[11]
        wal_lsn = $parts[12]
        node_database_sessions = [int] $parts[13]
    }
}

function Get-NodeMetrics {
    if (-not $BitcoinCliPath) { return $null }
    $cli = (Resolve-Path -LiteralPath $BitcoinCliPath -ErrorAction Stop).Path
    $chainRaw = & $cli "-datadir=$nodeDatadir" getblockchaininfo 2>$null
    if ($LASTEXITCODE -ne 0) { throw 'bitcoin-cli could not query getblockchaininfo.' }
    $indexRaw = & $cli "-datadir=$nodeDatadir" getindexinfo 2>$null
    if ($LASTEXITCODE -ne 0) { throw 'bitcoin-cli could not query node indexes.' }
    $chain = ($chainRaw -join [Environment]::NewLine) | ConvertFrom-Json
    $indexes = ($indexRaw -join [Environment]::NewLine) | ConvertFrom-Json
    $enterpriseIndex = $indexes.enterpriseindex
    $coinStatsIndex = $indexes.coinstatsindex
    return [ordered]@{
        headers = [int64] $chain.headers
        blocks = [int64] $chain.blocks
        initial_block_download = [bool] $chain.initialblockdownload
        verification_progress = [double] $chain.verificationprogress
        enterprise_height = if ($enterpriseIndex) { [int64] $enterpriseIndex.best_block_height } else { $null }
        enterprise_synced = if ($enterpriseIndex) { [bool] $enterpriseIndex.synced } else { $false }
        coinstats_height = if ($coinStatsIndex) { [int64] $coinStatsIndex.best_block_height } else { $null }
        coinstats_synced = if ($coinStatsIndex) { [bool] $coinStatsIndex.synced } else { $false }
    }
}

function Stop-ManagedNode {
    if (-not $EnableStop) { return $false }
    if (-not $BitcoinCliPath) { throw 'EnableStop requires BitcoinCliPath.' }
    $confirmed = if ($ConfirmDatadir) { [System.IO.Path]::GetFullPath($ConfirmDatadir) } else { '' }
    if (-not $confirmed.Equals($nodeDatadir, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'EnableStop requires -ConfirmDatadir with the exact resolved runtime datadir path.'
    }
    $cli = (Resolve-Path -LiteralPath $BitcoinCliPath -ErrorAction Stop).Path
    & $cli "-datadir=$nodeDatadir" stop
    if ($LASTEXITCODE -ne 0) { throw 'bitcoin-cli failed to stop the managed node gracefully.' }
    Write-Warning 'Free space crossed the stop threshold; the managed node received a graceful stop request.'
    return $true
}

function Get-MonitorSample {
    $drive = [System.IO.DriveInfo]::new([System.IO.Path]::GetPathRoot($nodeDatadir))
    $sample = [ordered]@{
        timestamp_utc = [DateTime]::UtcNow.ToString('o')
        drive = $drive.Name
        free_bytes = [int64] $drive.AvailableFreeSpace
        spool_bytes = Get-EnterpriseDirectorySize -Path $spoolPath
        spool_files = if (Test-Path -LiteralPath $spoolPath) {
            @(Get-ChildItem -LiteralPath $spoolPath -File -Force).Count
        } else { 0 }
        sql = $null
        sql_error = $null
        node = $null
        node_error = $null
    }
    try { $sample.sql = Get-SqlMetrics }
    catch { $sample.sql_error = $_.Exception.Message }
    try { $sample.node = Get-NodeMetrics }
    catch { $sample.node_error = $_.Exception.Message }
    return $sample
}

try {
    do {
        $sample = Get-MonitorSample
        $freeGiB = [math]::Round($sample.free_bytes / 1GB, 2)
        $spoolMiB = [math]::Round($sample.spool_bytes / 1MB, 2)
        Write-Output "[$($sample.timestamp_utc)] C: free=${freeGiB}GiB spool=${spoolMiB}MiB files=$($sample.spool_files)"
        if ($sample.sql) {
            Write-Output "SQL max=$($sample.sql.sql_max_height) backfill=$($sample.sql.backfill_succeeded_height) processed=$($sample.sql.processed_height) failed=$($sample.sql.failed_ingests) gaps=$($sample.sql.unresolved_gaps) price_frontier=$($sample.sql.first_available_price_day)..$($sample.sql.available_price_through_day) overdue_price_nulls=$($sample.sql.processed_price_null_overdue_tail) pending_price_nulls=$($sample.sql.processed_price_null_pending_tail)"
            if ($sample.sql.prices_null_rows -ne 0) {
                Write-Warning "prices contains $($sample.sql.prices_null_rows) NULL price rows."
            }
            if (-not $sample.sql.available_price_through_day) {
                Write-Warning 'public.prices has no non-NULL price frontier.'
            }
            if ($sample.sql.processed_price_null_overdue_tail -ne 0) {
                Write-Warning "OVERDUE PRICE COVERAGE: the latest processed height window contains $($sample.sql.processed_price_null_overdue_tail) blocks with NULL btc_usd_price on or before the available price frontier $($sample.sql.available_price_through_day)."
            }
            if ($sample.sql.processed_price_null_pending_tail -ne 0) {
                Write-Output "PENDING PRICE COVERAGE: the latest processed height window contains $($sample.sql.processed_price_null_pending_tail) blocks newer than the available price frontier $($sample.sql.available_price_through_day)."
            }
        } elseif ($sample.sql_error) { Write-Warning $sample.sql_error }
        if ($sample.node) {
            Write-Output "Node blocks=$($sample.node.blocks) headers=$($sample.node.headers) enterprise=$($sample.node.enterprise_height) enterprise_synced=$($sample.node.enterprise_synced) coinstats=$($sample.node.coinstats_height) coinstats_synced=$($sample.node.coinstats_synced)"
        } elseif ($sample.node_error) { Write-Warning $sample.node_error }

        if ($resolvedLogPath) {
            [void] (New-Item -ItemType Directory -Path (Split-Path -Parent $resolvedLogPath) -Force)
            ($sample | ConvertTo-Json -Depth 8 -Compress) | Add-Content -LiteralPath $resolvedLogPath -Encoding utf8
        }

        $stopped = $false
        if ($sample.free_bytes -lt ($StopFreeGiB * 1GB)) {
            Write-Warning "CRITICAL: C: free space is below ${StopFreeGiB} GiB. Nothing was deleted."
            $stopped = Stop-ManagedNode
        } elseif ($sample.free_bytes -lt ($WarningFreeGiB * 1GB)) {
            Write-Warning "C: free space is below the ${WarningFreeGiB} GiB warning threshold."
        }
        if ($Action -eq 'Once' -or $stopped) { break }
        Start-Sleep -Seconds $IntervalSeconds
    } while ($true)
} finally {
    Restore-EnterpriseProcessEnvironment -Previous $previousEnvironment
}
