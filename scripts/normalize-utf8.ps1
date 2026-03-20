param(
    [string]$Root = "."
)

$ErrorActionPreference = "Stop"

[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding

$utf8Strict = [System.Text.UTF8Encoding]::new($false, $true)
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$rootPath = (Resolve-Path $Root).Path

function Get-RepoRelativePath {
    param(
        [string]$BasePath,
        [string]$FullPath
    )

    $baseUri = New-Object System.Uri(($BasePath.TrimEnd('\') + '\'))
    $fileUri = New-Object System.Uri($FullPath)
    return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($fileUri).ToString()).Replace('/', '\')
}

function Test-IsManagedTextFile {
    param(
        [System.IO.FileInfo]$FileInfo
    )

    $pathSegments = $FileInfo.FullName -split '[\\/]'
    if ($pathSegments -contains '.git' -or $pathSegments -contains 'build') {
        return $false
    }

    return (
        $FileInfo.Extension -in $textExtensions -or
        $FileInfo.Name -eq 'CMakeLists.txt' -or
        $FileInfo.Name -eq '.editorconfig' -or
        $FileInfo.Name -eq '.gitattributes' -or
        $FileInfo.Name -eq '.gitignore'
    )
}

$gb18030 = [System.Text.Encoding]::GetEncoding(
    "GB18030",
    [System.Text.EncoderExceptionFallback]::new(),
    [System.Text.DecoderExceptionFallback]::new()
)

$textExtensions = @(
    ".cpp", ".h", ".hpp", ".c", ".cc",
    ".md", ".txt", ".yml", ".yaml",
    ".ini", ".sql", ".ps1", ".qss",
    ".qrc", ".cmake"
)

$updated = 0

Get-ChildItem -Path $Root -Recurse -File | Where-Object { Test-IsManagedTextFile $_ } | ForEach-Object {
    $bytes = [System.IO.File]::ReadAllBytes($_.FullName)

    $text = $null
    try {
        $text = $utf8Strict.GetString($bytes)
    } catch {
        try {
            $text = $gb18030.GetString($bytes)
        } catch {
            Write-Warning "Skip unsupported encoding: $($_.FullName)"
            return
        }
    }

    $normalizedText = $text -replace "`r`n", "`n" -replace "`r", "`n"
    $normalizedBytes = $utf8NoBom.GetBytes($normalizedText)

    $sameLength = $bytes.Length -eq $normalizedBytes.Length
    $sameBytes = $sameLength
    if ($sameBytes) {
        for ($index = 0; $index -lt $bytes.Length; $index++) {
            if ($bytes[$index] -ne $normalizedBytes[$index]) {
                $sameBytes = $false
                break
            }
        }
    }

    if (-not $sameBytes) {
        [System.IO.File]::WriteAllBytes($_.FullName, $normalizedBytes)
        $updated++
        Write-Host "Normalized $(Get-RepoRelativePath -BasePath $rootPath -FullPath $_.FullName)"
    }
}

Write-Host "Normalization complete. Updated files: $updated"
