param(
    [string]$Repo = "Driadix/ShuttleControllerV3"
)

$ErrorActionPreference = "Stop"

# Windows PowerShell 5.1 otherwise decodes native UTF-8 output through a legacy code page.
$utf8 = New-Object System.Text.UTF8Encoding($false)
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$OutputEncoding = $utf8

$json = gh issue list --repo $Repo --state all --limit 1000 --json number,title,body,comments
if ($LASTEXITCODE -ne 0) {
    throw "Failed to query GitHub issues."
}

$issues = $json | ConvertFrom-Json
$failures = @()

foreach ($issue in $issues) {
    $texts = @($issue.title, $issue.body)
    $texts += @($issue.comments | ForEach-Object { $_.body })
    $text = $texts -join "`n"

    $markers = @()
    if ($text.Contains([char]0xfffd)) { $markers += "U+FFFD" }
    if ($text.Contains([char]0x2568)) { $markers += "CP866-leading-D0" }
    if ($text.Contains([char]0x2564)) { $markers += "CP866-leading-D1" }
    $doubleEncodedPrefix = ([string][char]0x0442) + ([string][char]0x0425)
    if ($text.Contains($doubleEncodedPrefix)) { $markers += "double-encoded-prefix" }

    if ($markers.Count -gt 0) {
        $failures += "#$($issue.number) $($issue.title): $($markers -join ', ')"
    }
}

if ($failures.Count -gt 0) {
    Write-Error ("Corrupt GitHub text detected:`n" + ($failures -join "`n"))
    exit 1
}

"GitHub issue text check passed for $($issues.Count) issues."
