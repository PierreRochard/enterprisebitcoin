# Shared helpers for the PostgreSQL 17 Windows rollout scripts.
# This file is dot-sourced; it does not perform any work on its own.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-EnterpriseRepoRoot {
    return [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
}

function Resolve-EnterpriseRepoPath {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [switch] $MustExist
    )

    $repoRoot = Get-EnterpriseRepoRoot
    $candidate = if ([System.IO.Path]::IsPathRooted($Path)) {
        [System.IO.Path]::GetFullPath($Path)
    } else {
        [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
    }
    $rootPrefix = $repoRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if (-not $candidate.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase) -and
        -not $candidate.Equals($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path must remain inside the repository: $Path"
    }
    if ($MustExist -and -not (Test-Path -LiteralPath $candidate)) {
        throw "Required path does not exist: $candidate"
    }
    return $candidate
}

function Import-EnterprisePgEnv {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [string] $ExpectedDatabase = 'bitcoin_enterprise'
    )

    $fullPath = Resolve-EnterpriseRepoPath -Path $Path -MustExist
    $values = @{}
    $allowed = @('PGDB', 'PGUSER', 'PGPASSWORD', 'PGHOST', 'PGPORT')
    foreach ($line in [System.IO.File]::ReadAllLines($fullPath)) {
        if ($line -eq '' -or $line.StartsWith('#')) { continue }
        if ($line -notmatch '^([A-Z][A-Z0-9_]*)=([^#\s''"]+)$') {
            throw "Invalid dotenv syntax in $fullPath; use exact unquoted KEY=VALUE lines (values were not displayed)."
        }
        $name = $Matches[1]
        $value = $Matches[2]
        if ($name -cnotin $allowed) { throw "Unexpected dotenv key: $name" }
        if ($values.ContainsKey($name)) { throw "Duplicate dotenv key: $name" }
        $values[$name] = $value
    }

    $required = @('PGHOST', 'PGPORT', 'PGDB', 'PGUSER', 'PGPASSWORD')
    foreach ($name in $required) {
        if (-not $values.ContainsKey($name) -or [string]::IsNullOrEmpty($values[$name])) {
            throw "Missing required dotenv key: $name"
        }
    }
    if ($values['PGDB'] -cne $ExpectedDatabase) {
        throw "PGDB does not match the expected database name."
    }
    if ($values['PGHOST'] -notin @('127.0.0.1', 'localhost', '::1')) {
        throw "PGHOST must resolve explicitly to this laptop for the PG17 reuse rollout."
    }
    $port = 0
    if (-not [int]::TryParse($values['PGPORT'], [ref] $port) -or $port -lt 1 -or $port -gt 65535) {
        throw 'PGPORT is invalid.'
    }
    Write-Verbose "Validated dotenv keys $($required -join ', ') without displaying values."
    return $values
}

function Set-EnterprisePgProcessEnvironment {
    param(
        [Parameter(Mandatory = $true)][hashtable] $Values,
        [string] $ApplicationName,
        [string] $Options
    )

    $previous = @{}
    $toSet = @{
        PGHOST = $Values['PGHOST']
        PGPORT = $Values['PGPORT']
        PGDATABASE = $Values['PGDB']
        PGUSER = $Values['PGUSER']
        PGPASSWORD = $Values['PGPASSWORD']
        PGCONNECT_TIMEOUT = '10'
        PGHOSTADDR = $null
        PGSERVICE = $null
        PGSERVICEFILE = $null
        PGPASSFILE = $null
    }
    if ($ApplicationName) { $toSet['PGAPPNAME'] = $ApplicationName }
    if ($Options) { $toSet['PGOPTIONS'] = $Options }
    foreach ($entry in $toSet.GetEnumerator()) {
        $previous[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key, 'Process')
        $newValue = if ($null -eq $entry.Value) { $null } else { [string] $entry.Value }
        [Environment]::SetEnvironmentVariable($entry.Key, $newValue, 'Process')
    }
    return $previous
}

function Restore-EnterpriseProcessEnvironment {
    param([Parameter(Mandatory = $true)][hashtable] $Previous)
    foreach ($entry in $Previous.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
}

function Find-EnterprisePgTool {
    param(
        [Parameter(Mandatory = $true)][string] $Name,
        [string] $ExplicitPath
    )

    if ($ExplicitPath) {
        $resolved = (Resolve-Path -LiteralPath $ExplicitPath -ErrorAction Stop).Path
        return $resolved
    }
    $pg17 = Join-Path $env:ProgramFiles "PostgreSQL\17\bin\$Name.exe"
    if (Test-Path -LiteralPath $pg17 -PathType Leaf) { return $pg17 }
    $command = Get-Command "$Name.exe" -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw "Could not find $Name from PostgreSQL 17."
}

function Assert-EnterprisePg17Tool {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $Name
    )
    $versionOutput = (& $Path --version 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $versionOutput -notmatch ' 17(?:\.|\s)') {
        throw "$Name must be from PostgreSQL major version 17."
    }
}

function Invoke-EnterprisePsql {
    param(
        [Parameter(Mandatory = $true)][string] $Psql,
        [string] $Sql,
        [string] $File,
        [string] $ExpectedDatabase,
        [switch] $TuplesOnly
    )

    $arguments = @('-X', '-w', '-v', 'ON_ERROR_STOP=1')
    if ($TuplesOnly) { $arguments += @('-A', '-t') }
    if ($ExpectedDatabase) { $arguments += @('-v', "expected_database=$ExpectedDatabase") }
    if ($Sql) { $arguments += @('-c', $Sql) }
    elseif ($File) { $arguments += @('-f', $File) }
    else { throw 'Invoke-EnterprisePsql requires Sql or File.' }
    $output = @(& $Psql @arguments 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "psql failed without exposing credentials:`n$($output -join [Environment]::NewLine)"
    }
    return $output
}

function Test-EnterpriseProtectedAcl {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [switch] $Directory
    )

    $acl = Get-Acl -LiteralPath $Path
    if (-not $acl.AreAccessRulesProtected) { return $false }
    $allowed = @(
        [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value,
        ([System.Security.Principal.SecurityIdentifier]::new(
            [System.Security.Principal.WellKnownSidType]::LocalSystemSid, $null)).Value
    )
    $found = @{}
    $requiredInheritance = [System.Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    foreach ($rule in $acl.Access) {
        try {
            $sid = $rule.IdentityReference.Translate(
                [System.Security.Principal.SecurityIdentifier]).Value
        } catch {
            return $false
        }
        if ($rule.AccessControlType -ne 'Allow' -or $sid -notin $allowed -or
            ($rule.FileSystemRights -band [System.Security.AccessControl.FileSystemRights]::FullControl) -ne
                [System.Security.AccessControl.FileSystemRights]::FullControl) {
            return $false
        }
        if ($Directory) {
            if (($rule.InheritanceFlags -band $requiredInheritance) -ne $requiredInheritance) {
                return $false
            }
        } elseif ($rule.InheritanceFlags -ne [System.Security.AccessControl.InheritanceFlags]::None) {
            return $false
        }
        $found[$sid] = $true
    }
    return $found.Count -eq $allowed.Count -and
        @($allowed | Where-Object { -not $found.ContainsKey($_) }).Count -eq 0
}

function Protect-EnterpriseSecretFile {
    param([Parameter(Mandatory = $true)][string] $Path)

    $fullPath = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
    if (Test-EnterpriseProtectedAcl -Path $fullPath) { return }
    $currentSid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
    $systemSid = [System.Security.Principal.SecurityIdentifier]::new(
        [System.Security.Principal.WellKnownSidType]::LocalSystemSid, $null)
    $acl = [System.Security.AccessControl.FileSecurity]::new()
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($sid in @($currentSid, $systemSid)) {
        $rule = [System.Security.AccessControl.FileSystemAccessRule]::new(
            $sid,
            [System.Security.AccessControl.FileSystemRights]::FullControl,
            [System.Security.AccessControl.AccessControlType]::Allow)
        [void] $acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $fullPath -AclObject $acl
}

function Protect-EnterpriseDirectory {
    param([Parameter(Mandatory = $true)][string] $Path)

    $fullPath = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
    if (Test-EnterpriseProtectedAcl -Path $fullPath -Directory) { return }
    $currentSid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
    $systemSid = [System.Security.Principal.SecurityIdentifier]::new(
        [System.Security.Principal.WellKnownSidType]::LocalSystemSid, $null)
    $acl = [System.Security.AccessControl.DirectorySecurity]::new()
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($sid in @($currentSid, $systemSid)) {
        $rule = [System.Security.AccessControl.FileSystemAccessRule]::new(
            $sid,
            [System.Security.AccessControl.FileSystemRights]::FullControl,
            [System.Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit',
            [System.Security.AccessControl.PropagationFlags]::None,
            [System.Security.AccessControl.AccessControlType]::Allow)
        [void] $acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $fullPath -AclObject $acl
}

function Assert-EnterpriseSecretAcl {
    param([Parameter(Mandatory = $true)][string] $Path)

    if (-not (Test-EnterpriseProtectedAcl -Path $Path)) {
        throw "Secret file ACL is not restricted to the current account and SYSTEM: $Path"
    }
}

function Write-EnterpriseJsonAtomic {
    param(
        [Parameter(Mandatory = $true)] $Value,
        [Parameter(Mandatory = $true)][string] $Path
    )
    $directory = Split-Path -Parent $Path
    [void] (New-Item -ItemType Directory -Path $directory -Force)
    $temporary = "$Path.tmp-$PID"
    $Value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $temporary -Encoding utf8
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

function Get-EnterpriseDirectorySize {
    param([Parameter(Mandatory = $true)][string] $Path)
    if (-not (Test-Path -LiteralPath $Path)) { return [int64] 0 }
    $measurement = Get-ChildItem -LiteralPath $Path -File -Recurse -Force -ErrorAction Stop |
        Measure-Object -Property Length -Sum
    if ($null -eq $measurement -or $null -eq $measurement.Sum) { return [int64] 0 }
    return [int64] $measurement.Sum
}

function Test-EnterpriseTcpPortFree {
    param([Parameter(Mandatory = $true)][int] $Port)
    $listener = $null
    try {
        $listener = [System.Net.Sockets.TcpListener]::new(
            [System.Net.IPAddress]::Loopback, $Port)
        $listener.Start()
        return $true
    } catch {
        return $false
    } finally {
        if ($listener) { $listener.Stop() }
    }
}
