$ErrorActionPreference = 'Stop'
& python (Join-Path $PSScriptRoot 'parity_ledger.py') --verify
exit $LASTEXITCODE
