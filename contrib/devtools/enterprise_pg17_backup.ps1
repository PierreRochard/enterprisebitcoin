[CmdletBinding()]
param(
    [ValidateSet('Preflight', 'Backup', 'AdoptCompleted', 'Verify', 'RestoreTest', 'ResumeRestoreTest', 'CleanupRestoreTest')]
    [string] $Action = 'Preflight',
    [string] $EnvFile = '.env',
    [string] $ExpectedDatabase = 'bitcoin_enterprise',
    [string] $BackupRoot = 'E:\enterprisebitcoin-pg17-backups',
    [string] $RestoreRoot = 'D:\enterprisebitcoin-pg17-restore-tests',
    [string] $ReceiptDirectory = '.runtime/postgres/backups',
    [string] $ReceiptPath,
    [string] $CompletedBackupPath,
    [int] $RestorePort = 55418,
    [string] $PsqlPath,
    [string] $PgBaseBackupPath,
    [string] $PgVerifyBackupPath,
    [string] $PgCtlPath,
    [int] $SafetyMarginGiB = 25,
    [switch] $ConfirmBackup,
    [string] $ConfirmAdoptPath,
    [string] $ConfirmRestorePath,
    [string] $ConfirmCleanupPath,
    [switch] $DryRun
)

. (Join-Path $PSScriptRoot 'enterprise_pg17_common.ps1')

if ($ExpectedDatabase -cne 'bitcoin_enterprise') {
    throw 'This rollout is intentionally limited to bitcoin_enterprise.'
}
if ([System.IO.Path]::GetPathRoot([System.IO.Path]::GetFullPath($BackupRoot)) -cne 'E:\') {
    throw 'The retained physical backup root must be on E:.'
}
if ([System.IO.Path]::GetPathRoot([System.IO.Path]::GetFullPath($RestoreRoot)) -cne 'D:\') {
    throw 'The disposable restore-test root must be on D:.'
}
if ($RestorePort -lt 1024 -or $RestorePort -gt 65535) { throw 'RestorePort is invalid.' }
if ($SafetyMarginGiB -lt 25) { throw 'SafetyMarginGiB cannot be lower than 25.' }

$envPath = Resolve-EnterpriseRepoPath -Path $EnvFile -MustExist
$pgEnv = Import-EnterprisePgEnv -Path $envPath -ExpectedDatabase $ExpectedDatabase
$receiptRoot = Resolve-EnterpriseRepoPath -Path $ReceiptDirectory
$psql = Find-EnterprisePgTool -Name 'psql' -ExplicitPath $PsqlPath
Assert-EnterprisePg17Tool -Path $psql -Name 'psql'
$previousEnvironment = Set-EnterprisePgProcessEnvironment -Values $pgEnv `
    -ApplicationName 'enterprise-pg17-backup' `
    -Options '-c default_transaction_read_only=on -c statement_timeout=300000 -c search_path=pg_catalog,public'

function Assert-ExternalChildPath {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $Root,
        [Parameter(Mandatory = $true)][string] $LeafPrefix
    )
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $prefix = $fullRoot + [System.IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase) -or
        -not ([System.IO.Path]::GetDirectoryName($fullPath)).Equals(
            $fullRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
        -not ([System.IO.Path]::GetFileName($fullPath)).StartsWith($LeafPrefix, [System.StringComparison]::Ordinal)) {
        throw "Unsafe operational path: $Path"
    }
    return $fullPath
}

function Assert-NotReparsePoint {
    param([Parameter(Mandatory = $true)][string] $Path)
    if (Test-Path -LiteralPath $Path) {
        $item = Get-Item -LiteralPath $Path -Force
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Operational root/path must not be a reparse point: $Path"
        }
    }
}

function Protect-BackupTree {
    param([Parameter(Mandatory = $true)][string] $Path)
    Protect-EnterpriseDirectory -Path $Path
}

function Get-DriveFreeBytes {
    param([Parameter(Mandatory = $true)][string] $Path)
    $root = [System.IO.Path]::GetPathRoot([System.IO.Path]::GetFullPath($Path))
    $drive = [System.IO.DriveInfo]::new($root)
    if (-not $drive.IsReady) { throw "Drive is unavailable: $root" }
    return [int64] $drive.AvailableFreeSpace
}

function Get-SourceInventory {
    $sql = @'
SELECT (SELECT system_identifier FROM pg_catalog.pg_control_system())::text
       || '|' || current_setting('server_version_num')
       || '|' || (SELECT sum(pg_catalog.pg_database_size(oid)) FROM pg_catalog.pg_database)::text
       || '|' || (SELECT count(*) FROM public.blocks)::text
       || '|' || (SELECT min(height) FROM public.blocks WHERE network = 'mainnet')::text
       || '|' || (SELECT max(height) FROM public.blocks WHERE network = 'mainnet')::text
       || '|' || (SELECT count(*) FROM public.prices)::text
       || '|' || (SELECT min(day) FROM public.prices)::text
       || '|' || (SELECT max(day) FROM public.prices)::text
       || '|' || current_setting('data_directory');
'@
    $line = ((Invoke-EnterprisePsql -Psql $psql -Sql $sql -TuplesOnly) -join '').Trim()
    $parts = $line.Split('|')
    if ($parts.Count -ne 10) { throw 'Could not parse the source inventory.' }
    return [ordered]@{
        system_identifier = $parts[0]
        server_version_num = $parts[1]
        cluster_database_bytes = [int64] $parts[2]
        blocks_rows = [int64] $parts[3]
        mainnet_min_height = [int64] $parts[4]
        mainnet_max_height = [int64] $parts[5]
        prices_rows = [int64] $parts[6]
        first_price_day = $parts[7]
        last_price_day = $parts[8]
        source_data_directory = $parts[9]
    }
}

function Invoke-BackupPreflight {
    param([switch] $SkipNewBackupChecks)

    Assert-NotReparsePoint -Path $BackupRoot
    Assert-NotReparsePoint -Path $RestoreRoot
    $inventory = Get-SourceInventory
    if (-not $inventory.server_version_num.StartsWith('17')) {
        throw 'The connected cluster is not PostgreSQL 17.'
    }
    $backupFull = [System.IO.Path]::GetFullPath($BackupRoot)
    $sourceData = [System.IO.Path]::GetFullPath($inventory.source_data_directory)
    if ($backupFull.StartsWith($sourceData.TrimEnd('\', '/') + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'BackupRoot must not be inside the live PostgreSQL data directory.'
    }
    if (-not $SkipNewBackupChecks) {
        $required = $inventory.cluster_database_bytes + ($SafetyMarginGiB * 1GB)
        $free = Get-DriveFreeBytes -Path $BackupRoot
        if ($free -lt $required) {
            throw "E: lacks the required source-size plus $SafetyMarginGiB GiB safety margin."
        }
        $replication = (Invoke-EnterprisePsql -Psql $psql -Sql @'
SELECT (r.rolsuper OR r.rolreplication)::text || '|' ||
       current_setting('max_wal_senders')::integer::text
  FROM pg_catalog.pg_roles AS r WHERE r.rolname = session_user;
'@ -TuplesOnly) -join ''
        $replicationParts = $replication.Trim().Split('|')
        if ($replicationParts.Count -ne 2 -or $replicationParts[0] -ne 'true' -or [int] $replicationParts[1] -lt 1) {
            throw 'The role or server is not configured for pg_basebackup streaming.'
        }
    }
    $userTablespaces = ((Invoke-EnterprisePsql -Psql $psql -Sql @'
SELECT count(*)
  FROM pg_catalog.pg_tablespace
 WHERE spcname NOT IN ('pg_default', 'pg_global');
'@ -TuplesOnly) -join '').Trim()
    if ([int] $userTablespaces -ne 0) {
        throw 'User tablespaces are present. This rollout provides no safe tablespace mappings and refuses the physical backup.'
    }
    $blockWriters = ((Invoke-EnterprisePsql -Psql $psql -Sql @'
SELECT count(*)
  FROM pg_catalog.pg_stat_activity
 WHERE datname = current_database() AND pid <> pg_backend_pid()
   AND (application_name = 'enterprise-bitcoind'
        OR (state <> 'idle' AND query ~* '(insert|update|delete)[[:space:][:print:]]*blocks'));
'@ -TuplesOnly) -join '').Trim()
    if ([int] $blockWriters -ne 0) {
        throw 'A block writer is active; stop it before taking the rollout backup.'
    }
    return $inventory
}

function Read-BackupReceipt {
    if (-not $ReceiptPath) { throw '-ReceiptPath is required for this action.' }
    Assert-NotReparsePoint -Path $BackupRoot
    $path = Resolve-EnterpriseRepoPath -Path $ReceiptPath -MustExist
    $receipt = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if ($receipt.kind -ne 'enterprise-pg17-physical-backup' -or
        $receipt.expected_database -ne $ExpectedDatabase) {
        throw 'Receipt is not for this PostgreSQL 17 rollout.'
    }
    $safeBackupPath = Assert-ExternalChildPath -Path $receipt.backup_path -Root $BackupRoot -LeafPrefix 'pg17-base-'
    if (-not (Test-Path -LiteralPath $safeBackupPath -PathType Container)) {
        throw 'Receipt backup path does not exist.'
    }
    Assert-NotReparsePoint -Path $safeBackupPath
    return [ordered]@{ path = $path; value = $receipt; backup_path = $safeBackupPath }
}

function Save-BackupReceipt {
    param(
        [Parameter(Mandatory = $true)] $Receipt,
        [Parameter(Mandatory = $true)][string] $Path
    )
    Write-EnterpriseJsonAtomic -Value $Receipt -Path $Path
    Protect-EnterpriseSecretFile -Path $Path
}

function Invoke-VerifyBackup {
    param(
        [Parameter(Mandatory = $true)][string] $BackupPath,
        [Parameter(Mandatory = $true)] $Receipt,
        [Parameter(Mandatory = $true)][string] $ResolvedReceiptPath
    )
    $verifyBackup = Find-EnterprisePgTool -Name 'pg_verifybackup' -ExplicitPath $PgVerifyBackupPath
    Assert-EnterprisePg17Tool -Path $verifyBackup -Name 'pg_verifybackup'
    & $verifyBackup $BackupPath
    if ($LASTEXITCODE -ne 0) { throw 'pg_verifybackup rejected the physical backup.' }
    $manifest = Join-Path $BackupPath 'backup_manifest'
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { throw 'backup_manifest is missing.' }
    $manifestHash = (Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash
    if ($Receipt.backup_manifest_sha256 -and $Receipt.backup_manifest_sha256 -cne $manifestHash) {
        throw 'backup_manifest changed after its original verification.'
    }
    $verificationTime = [DateTime]::UtcNow.ToString('o')
    if (-not $Receipt.verified_at_utc) {
        $Receipt | Add-Member -NotePropertyName verified_at_utc -NotePropertyValue $verificationTime -Force
    }
    $Receipt | Add-Member -NotePropertyName last_verified_at_utc -NotePropertyValue $verificationTime -Force
    $Receipt | Add-Member -NotePropertyName backup_manifest_sha256 -NotePropertyValue $manifestHash -Force
    Save-BackupReceipt -Receipt $Receipt -Path $ResolvedReceiptPath
}

function New-PhysicalBackup {
    param([Parameter(Mandatory = $true)] $Inventory)
    $baseBackup = Find-EnterprisePgTool -Name 'pg_basebackup' -ExplicitPath $PgBaseBackupPath
    Assert-EnterprisePg17Tool -Path $baseBackup -Name 'pg_basebackup'
    $backupId = "pg17-base-{0:yyyyMMddTHHmmssZ}" -f [DateTime]::UtcNow
    $backupPath = Assert-ExternalChildPath -Path (Join-Path $BackupRoot $backupId) `
        -Root $BackupRoot -LeafPrefix 'pg17-base-'
    if (Test-Path -LiteralPath $backupPath) { throw "Backup destination already exists: $backupPath" }
    [void] (New-Item -ItemType Directory -Path $BackupRoot -Force)
    [void] (New-Item -ItemType Directory -Path $backupPath)
    Protect-BackupTree -Path $backupPath
    [void] (New-Item -ItemType Directory -Path $receiptRoot -Force)
    Protect-EnterpriseDirectory -Path $receiptRoot
    $logPath = Join-Path $receiptRoot "$backupId.log"
    [void] (New-Item -ItemType File -Path $logPath -Force)
    Protect-EnterpriseSecretFile -Path $logPath

    # BASE_BACKUP legitimately runs for hours. Override only this child
    # connection's timeout; preflight and verification queries remain bounded.
    $priorPgOptions = [Environment]::GetEnvironmentVariable('PGOPTIONS', 'Process')
    $baseBackupExitCode = -1
    try {
        [Environment]::SetEnvironmentVariable(
            'PGOPTIONS', '-c statement_timeout=0 -c lock_timeout=0', 'Process')
        & $baseBackup -D $backupPath --format=plain --wal-method=stream --checkpoint=spread `
            --manifest-checksums=SHA256 --progress --verbose --no-password --no-clean 2>&1 |
            Tee-Object -FilePath $logPath
        $baseBackupExitCode = $LASTEXITCODE
    } finally {
        [Environment]::SetEnvironmentVariable('PGOPTIONS', $priorPgOptions, 'Process')
    }
    if ($baseBackupExitCode -ne 0) {
        throw "pg_basebackup failed; the incomplete directory was retained for inspection: $backupPath"
    }
    Protect-EnterpriseSecretFile -Path $logPath
    Protect-BackupTree -Path $backupPath

    $receiptFile = Join-Path $receiptRoot "$backupId.json"
    $receipt = [pscustomobject] [ordered]@{
        kind = 'enterprise-pg17-physical-backup'
        expected_database = $ExpectedDatabase
        backup_id = $backupId
        backup_path = $backupPath
        system_identifier = $Inventory.system_identifier
        source_server_version_num = $Inventory.server_version_num
        source_inventory = $Inventory
        source_inventory_timing = 'before-backup'
        completed_at_utc = [DateTime]::UtcNow.ToString('o')
        verified_at_utc = $null
        last_verified_at_utc = $null
        backup_manifest_sha256 = $null
        restore_tested_at_utc = $null
        restore_path = $null
        restored_inventory = $null
    }
    Save-BackupReceipt -Receipt $receipt -Path $receiptFile
    Invoke-VerifyBackup -BackupPath $backupPath -Receipt $receipt -ResolvedReceiptPath $receiptFile
    Write-Output "Physical backup completed and verified: $backupPath"
    Write-Output "Backup receipt: $receiptFile"
}

function Adopt-CompletedPhysicalBackup {
    param([Parameter(Mandatory = $true)] $Inventory)

    if (-not $CompletedBackupPath) { throw 'AdoptCompleted requires -CompletedBackupPath.' }
    $backupPath = Assert-ExternalChildPath -Path $CompletedBackupPath `
        -Root $BackupRoot -LeafPrefix 'pg17-base-'
    if ($ConfirmAdoptPath -cne $backupPath) {
        throw 'AdoptCompleted requires -ConfirmAdoptPath with the exact completed backup path.'
    }
    if (-not (Test-Path -LiteralPath $backupPath -PathType Container)) {
        throw "Completed backup directory does not exist: $backupPath"
    }
    Assert-NotReparsePoint -Path $backupPath
    $backupId = Split-Path -Leaf $backupPath
    if ($backupId -cnotmatch '^pg17-base-[0-9]{8}T[0-9]{6}Z$') {
        throw 'Completed backup directory has an invalid identifier.'
    }
    foreach ($marker in @('PG_VERSION', 'backup_label', 'backup_manifest')) {
        if (-not (Test-Path -LiteralPath (Join-Path $backupPath $marker) -PathType Leaf)) {
            throw "Completed backup is missing required marker: $marker"
        }
    }
    if ((Get-Content -LiteralPath (Join-Path $backupPath 'PG_VERSION') -Raw).Trim() -ne '17') {
        throw 'Completed backup is not PostgreSQL 17.'
    }
    if (Test-Path -LiteralPath (Join-Path $backupPath 'backup_manifest.tmp')) {
        throw 'Completed backup still contains a temporary manifest.'
    }

    [void] (New-Item -ItemType Directory -Path $receiptRoot -Force)
    Protect-EnterpriseDirectory -Path $receiptRoot
    $logPath = Join-Path $receiptRoot "$backupId.log"
    if (Test-Path -LiteralPath $logPath -PathType Leaf) {
        Protect-EnterpriseSecretFile -Path $logPath
    }
    Protect-BackupTree -Path $backupPath

    $receiptFile = Join-Path $receiptRoot "$backupId.json"
    if (Test-Path -LiteralPath $receiptFile) {
        throw "A receipt already exists for the completed backup: $receiptFile"
    }
    $manifest = Get-Item -LiteralPath (Join-Path $backupPath 'backup_manifest')
    $receipt = [pscustomobject] [ordered]@{
        kind = 'enterprise-pg17-physical-backup'
        expected_database = $ExpectedDatabase
        backup_id = $backupId
        backup_path = $backupPath
        system_identifier = $Inventory.system_identifier
        source_server_version_num = $Inventory.server_version_num
        source_inventory = $Inventory
        source_inventory_timing = 'after-backup-adoption'
        completed_at_utc = $manifest.LastWriteTimeUtc.ToString('o')
        verified_at_utc = $null
        last_verified_at_utc = $null
        backup_manifest_sha256 = $null
        restore_tested_at_utc = $null
        restore_path = $null
        restored_inventory = $null
        recovery_note = 'Adopted after post-copy ACL hardening failed; backup contents were not modified.'
    }
    Save-BackupReceipt -Receipt $receipt -Path $receiptFile
    Invoke-VerifyBackup -BackupPath $backupPath -Receipt $receipt -ResolvedReceiptPath $receiptFile
    Write-Output "Completed physical backup adopted and verified: $backupPath"
    Write-Output "Backup receipt: $receiptFile"
}

function Invoke-RestoreTest {
    param(
        [Parameter(Mandatory = $true)] $ReceiptInfo,
        [switch] $UseExistingCopy
    )
    Assert-NotReparsePoint -Path $RestoreRoot
    $receipt = $ReceiptInfo.value
    if (-not $receipt.verified_at_utc) { throw 'Backup must pass pg_verifybackup before restore testing.' }
    $manifestHash = (Get-FileHash -LiteralPath (Join-Path $ReceiptInfo.backup_path 'backup_manifest') -Algorithm SHA256).Hash
    if ($manifestHash -cne $receipt.backup_manifest_sha256) { throw 'backup_manifest hash no longer matches the receipt.' }
    if (-not (Test-EnterpriseTcpPortFree -Port $RestorePort)) { throw "Restore port $RestorePort is already in use." }

    $restoreLeaf = $receipt.backup_id -replace '^pg17-base-', 'pg17-restore-'
    $restorePath = Assert-ExternalChildPath -Path (Join-Path $RestoreRoot $restoreLeaf) `
        -Root $RestoreRoot -LeafPrefix 'pg17-restore-'
    if ($UseExistingCopy) {
        if ($ConfirmRestorePath -cne $restorePath) {
            throw 'ResumeRestoreTest requires -ConfirmRestorePath with the exact retained D: path.'
        }
        if (-not (Test-Path -LiteralPath $restorePath -PathType Container)) {
            throw "Retained restore-test copy does not exist: $restorePath"
        }
        Assert-NotReparsePoint -Path $restorePath
        if ($DryRun) {
            Write-Output "Dry run passed; would verify and resume $restorePath on 127.0.0.1:$RestorePort."
            return
        }
    } else {
        $backupBytes = Get-EnterpriseDirectorySize -Path $ReceiptInfo.backup_path
        $restoreFree = Get-DriveFreeBytes -Path $RestoreRoot
        if ($restoreFree -lt ($backupBytes + 20GB)) {
            throw 'D: lacks backup size plus the 20 GiB restore-test safety margin.'
        }
        if (Test-Path -LiteralPath $restorePath) { throw "Restore-test destination already exists: $restorePath" }
        if ($DryRun) {
            Write-Output "Dry run passed; would copy to $restorePath and test on 127.0.0.1:$RestorePort."
            return
        }

        [void] (New-Item -ItemType Directory -Path $RestoreRoot -Force)
        [void] (New-Item -ItemType Directory -Path $restorePath)
        Protect-BackupTree -Path $restorePath
        & robocopy.exe $ReceiptInfo.backup_path $restorePath /E /COPY:DAT /DCOPY:DAT /R:2 /W:2 /NFL /NDL /NP
        if ($LASTEXITCODE -gt 7) { throw "robocopy failed with exit code $LASTEXITCODE; restore copy was retained." }
    }
    Protect-BackupTree -Path $restorePath
    $tablespaceLinks = @(Get-ChildItem -LiteralPath (Join-Path $restorePath 'pg_tblspc') -Force -ErrorAction Stop)
    if ($tablespaceLinks.Count -ne 0) {
        throw 'Disposable restore contains pg_tblspc entries; refusing to start it because they could target source paths.'
    }

    $verifyBackup = Find-EnterprisePgTool -Name 'pg_verifybackup' -ExplicitPath $PgVerifyBackupPath
    Assert-EnterprisePg17Tool -Path $verifyBackup -Name 'pg_verifybackup'
    & $verifyBackup $restorePath
    if ($LASTEXITCODE -ne 0) { throw 'pg_verifybackup rejected the disposable D: restore copy.' }
    $restoreManifestHash = (Get-FileHash -LiteralPath (Join-Path $restorePath 'backup_manifest') -Algorithm SHA256).Hash
    if ($restoreManifestHash -cne $receipt.backup_manifest_sha256) {
        throw 'Disposable D: restore manifest does not match the verified E: receipt.'
    }

    $pgCtl = Find-EnterprisePgTool -Name 'pg_ctl' -ExplicitPath $PgCtlPath
    Assert-EnterprisePg17Tool -Path $pgCtl -Name 'pg_ctl'
    $logDirectory = Resolve-EnterpriseRepoPath -Path '.runtime/postgres/logs'
    [void] (New-Item -ItemType Directory -Path $logDirectory -Force)
    Protect-EnterpriseDirectory -Path $logDirectory
    $logPath = Join-Path $logDirectory "$restoreLeaf.log"
    [void] (New-Item -ItemType File -Path $logPath -Force)
    Protect-EnterpriseSecretFile -Path $logPath
    $postgresDataPath = $restorePath -replace '\\', '/'
    $serverOptions = "-p $RestorePort -c listen_addresses=127.0.0.1 -c data_directory=$postgresDataPath " +
        "-c hba_file=$postgresDataPath/pg_hba.conf -c ident_file=$postgresDataPath/pg_ident.conf " +
        "-c unix_socket_directories= -c ssl=off -c external_pid_file= " +
        "-c archive_mode=off -c archive_command= -c archive_library= " +
        "-c restore_command= -c primary_conninfo= -c primary_slot_name= " +
        "-c synchronous_standby_names= -c shared_preload_libraries= " +
        "-c logging_collector=off -c log_destination=stderr -c autovacuum=off " +
        "-c max_connections=10 -c shared_buffers=256MB -c huge_pages=off " +
        "-c work_mem=4MB -c maintenance_work_mem=64MB -c effective_cache_size=512MB " +
        "-c max_worker_processes=4 -c max_parallel_workers=2 " +
        "-c max_parallel_workers_per_gather=0 -c cluster_name=enterprise-restore-test " +
        "-c default_transaction_read_only=on"
    $restoreError = $null
    $stopError = $null
    try {
        # The restored postgres process does not need the source password. Do
        # not leave it in the long-lived child process environment.
        $savedPassword = [Environment]::GetEnvironmentVariable('PGPASSWORD', 'Process')
        try {
            [Environment]::SetEnvironmentVariable('PGPASSWORD', $null, 'Process')
            & $pgCtl -D $restorePath -l $logPath -o $serverOptions -w -t 180 start
            $startExitCode = $LASTEXITCODE
        } finally {
            [Environment]::SetEnvironmentVariable('PGPASSWORD', $savedPassword, 'Process')
        }
        if ($startExitCode -ne 0) { throw 'Disposable PostgreSQL restore clone did not start.' }
        $cloneEnv = @{}
        foreach ($entry in $pgEnv.GetEnumerator()) { $cloneEnv[$entry.Key] = $entry.Value }
        $cloneEnv['PGHOST'] = '127.0.0.1'
        $cloneEnv['PGPORT'] = [string] $RestorePort
        Restore-EnterpriseProcessEnvironment -Previous $previousEnvironment
        $script:previousEnvironment = Set-EnterprisePgProcessEnvironment -Values $cloneEnv `
            -ApplicationName 'enterprise-pg17-restore-test' `
            -Options '-c default_transaction_read_only=on -c statement_timeout=300000 -c search_path=pg_catalog,public'
        $cloneInventory = Get-SourceInventory
        if ($cloneInventory.prices_rows -lt 1 -or -not $cloneInventory.first_price_day -or
            -not $cloneInventory.last_price_day -or -not $receipt.source_inventory.first_price_day -or
            -not $receipt.source_inventory.last_price_day) {
            throw 'Restored cluster does not contain a usable prices inventory.'
        }
        $cloneLastPriceDay = [DateTime]::Parse([string] $cloneInventory.last_price_day)
        $sourceLastPriceDay = [DateTime]::Parse([string] $receipt.source_inventory.last_price_day)
        $timingProperty = $receipt.PSObject.Properties['source_inventory_timing']
        $recoveryProperty = $receipt.PSObject.Properties['recovery_note']
        $adoptedReceipt = ($timingProperty -and
            [string] $timingProperty.Value -ceq 'after-backup-adoption') -or
            ($recoveryProperty -and [bool] $recoveryProperty.Value)
        if ($cloneInventory.system_identifier -cne $receipt.system_identifier -or
            $cloneInventory.blocks_rows -ne [int64] $receipt.source_inventory.blocks_rows -or
            $cloneInventory.mainnet_max_height -ne [int64] $receipt.source_inventory.mainnet_max_height -or
            $cloneInventory.mainnet_min_height -ne [int64] $receipt.source_inventory.mainnet_min_height -or
            $cloneInventory.first_price_day -cne [string] $receipt.source_inventory.first_price_day -or
            ($adoptedReceipt -and (
                $cloneInventory.prices_rows -gt [int64] $receipt.source_inventory.prices_rows -or
                $cloneLastPriceDay -gt $sourceLastPriceDay)) -or
            (-not $adoptedReceipt -and (
                $cloneInventory.prices_rows -lt [int64] $receipt.source_inventory.prices_rows -or
                $cloneLastPriceDay -lt $sourceLastPriceDay))) {
            throw 'Restored cluster inventory does not match the backup receipt.'
        }
    } catch {
        $restoreError = $_
    } finally {
        & $pgCtl -D $restorePath status *> $null
        $cloneIsRunning = $LASTEXITCODE -eq 0
        if ($cloneIsRunning) {
            & $pgCtl -D $restorePath -m fast -w -t 180 stop
            if ($LASTEXITCODE -ne 0) {
                $stopError = 'Disposable restore clone did not stop cleanly; manual intervention is required.'
            }
        }
    }
    Protect-EnterpriseSecretFile -Path $logPath
    if ($restoreError) {
        if ($stopError) { Write-Warning $stopError }
        throw $restoreError
    }
    if ($stopError) { throw $stopError }
    $receipt | Add-Member -NotePropertyName restore_tested_at_utc `
        -NotePropertyValue ([DateTime]::UtcNow.ToString('o')) -Force
    $receipt | Add-Member -NotePropertyName restore_path -NotePropertyValue $restorePath -Force
    $receipt | Add-Member -NotePropertyName restored_inventory -NotePropertyValue $cloneInventory -Force
    Save-BackupReceipt -Receipt $receipt -Path $ReceiptInfo.path
    Write-Output "Restore test passed; stopped clone retained at: $restorePath"
    Write-Output 'Use CleanupRestoreTest with the exact path after reviewing the result.'
}

try {
    switch ($Action) {
        'Preflight' {
            $inventory = Invoke-BackupPreflight
            Write-Output 'Preflight passed for PostgreSQL 17 physical backup.'
            Write-Output "Estimated cluster database bytes: $($inventory.cluster_database_bytes)"
            Write-Output "Available E: bytes: $(Get-DriveFreeBytes -Path $BackupRoot)"
            if ($DryRun) { Write-Output 'Dry-run preflight complete; no files were written.' }
        }
        'Backup' {
            if (-not $ConfirmBackup) { throw 'Backup requires -ConfirmBackup.' }
            $inventory = Invoke-BackupPreflight
            Write-Output 'Preflight passed for PostgreSQL 17 physical backup.'
            if ($DryRun) { Write-Output 'Dry run complete; no physical backup was created.' }
            else { New-PhysicalBackup -Inventory $inventory }
        }
        'AdoptCompleted' {
            $inventory = Invoke-BackupPreflight -SkipNewBackupChecks
            Write-Output 'Preflight passed for completed PostgreSQL 17 backup adoption.'
            if ($DryRun) {
                if (-not $CompletedBackupPath) { throw 'AdoptCompleted requires -CompletedBackupPath.' }
                $safePath = Assert-ExternalChildPath -Path $CompletedBackupPath `
                    -Root $BackupRoot -LeafPrefix 'pg17-base-'
                if ($ConfirmAdoptPath -cne $safePath) {
                    throw 'AdoptCompleted requires -ConfirmAdoptPath with the exact completed backup path.'
                }
                Write-Output "Dry run complete; would adopt and verify $safePath"
            } else {
                Adopt-CompletedPhysicalBackup -Inventory $inventory
            }
        }
        'Verify' {
            $info = Read-BackupReceipt
            if ($DryRun) { Write-Output 'Dry run complete; receipt and backup path are valid.' }
            else {
                Invoke-VerifyBackup -BackupPath $info.backup_path -Receipt $info.value -ResolvedReceiptPath $info.path
                Write-Output 'Physical backup verified and receipt updated.'
            }
        }
        'RestoreTest' {
            $info = Read-BackupReceipt
            Invoke-RestoreTest -ReceiptInfo $info
        }
        'ResumeRestoreTest' {
            $info = Read-BackupReceipt
            Invoke-RestoreTest -ReceiptInfo $info -UseExistingCopy
        }
        'CleanupRestoreTest' {
            $info = Read-BackupReceipt
            Assert-NotReparsePoint -Path $RestoreRoot
            if (-not $info.value.restore_path) { throw 'Receipt does not name a restore-test path.' }
            $restorePath = Assert-ExternalChildPath -Path $info.value.restore_path -Root $RestoreRoot -LeafPrefix 'pg17-restore-'
            Assert-NotReparsePoint -Path $restorePath
            if ($ConfirmCleanupPath -cne $restorePath) {
                throw 'Cleanup requires -ConfirmCleanupPath with the exact receipt restore path.'
            }
            if (-not (Test-Path -LiteralPath (Join-Path $restorePath 'PG_VERSION')) -or
                -not (Test-Path -LiteralPath (Join-Path $restorePath 'backup_manifest'))) {
                throw 'Restore-test path lacks the expected PostgreSQL backup markers.'
            }
            $pgCtl = Find-EnterprisePgTool -Name 'pg_ctl' -ExplicitPath $PgCtlPath
            & $pgCtl -D $restorePath status *> $null
            if ($LASTEXITCODE -eq 0) { throw 'Refusing cleanup because the restore-test clone is running.' }
            if ($DryRun) { Write-Output "Dry run complete; would remove only $restorePath" }
            else {
                Remove-Item -LiteralPath $restorePath -Recurse -Force
                Write-Output "Removed disposable restore-test copy: $restorePath"
            }
        }
    }
} finally {
    Restore-EnterpriseProcessEnvironment -Previous $previousEnvironment
}
