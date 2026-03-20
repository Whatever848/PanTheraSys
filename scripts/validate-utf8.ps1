param(
    [string]$Root = "."
)

$ErrorActionPreference = "Stop"

$utf8Strict = [System.Text.UTF8Encoding]::new($false, $true)
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

$textExtensions = @(
    ".cpp", ".h", ".hpp", ".c", ".cc",
    ".md", ".txt", ".yml", ".yaml",
    ".ini", ".sql", ".ps1", ".qss",
    ".qrc", ".cmake"
)

$failures = New-Object System.Collections.Generic.List[string]

Get-ChildItem -Path $Root -Recurse -File | Where-Object { Test-IsManagedTextFile $_ } | ForEach-Object {
    $bytes = [System.IO.File]::ReadAllBytes($_.FullName)
    try {
        [void]$utf8Strict.GetString($bytes)
    } catch {
        $relativePath = Get-RepoRelativePath -BasePath $rootPath -FullPath $_.FullName
        $failures.Add("$relativePath is not valid UTF-8: $($_.Exception.Message)")
        return
    }

    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        $relativePath = Get-RepoRelativePath -BasePath $rootPath -FullPath $_.FullName
        $failures.Add("$relativePath contains a UTF-8 BOM; expected UTF-8 without BOM.")
    }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    throw "UTF-8 validation failed."
}

Write-Host "UTF-8 validation passed."
