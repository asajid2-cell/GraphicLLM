param(
    [string]$SeedRoot = "",
    [string]$SceneId = "",
    [string]$RuntimeLayoutContractsPath = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($SeedRoot)) {
    $SeedRoot = Join-Path $root "assets/scenes/hand_authored"
}
if ([string]::IsNullOrWhiteSpace($RuntimeLayoutContractsPath)) {
    $RuntimeLayoutContractsPath = Join-Path $root "assets/scenes/hand_authored/runtime_layout_contracts.json"
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) { $script:failures.Add($Message) | Out-Null }

function Has-Property([object]$Object, [string]$Name) {
    return $null -ne $Object -and ($Object.PSObject.Properties.Name -contains $Name)
}

function Get-BuilderBody([string]$Source, [string]$Builder) {
    $needle = "void $Builder"
    $start = $Source.IndexOf($needle)
    if ($start -lt 0) { return $null }
    $next = $Source.IndexOf("`nvoid Engine::", $start + $needle.Length)
    if ($next -lt 0) { $next = $Source.Length }
    return $Source.Substring($start, $next - $start)
}

function Parse-Vec3Values([string]$Text) {
    $clean = $Text -replace "glm::radians\(([^)]*)\)", '$1'
    $clean = $clean -replace "[fF]", ""
    $clean = $clean -replace "glm::half_pi<[^>]+>\(\)", "1.570796"
    $parts = @($clean -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_.Length -gt 0 })
    if ($parts.Count -eq 1) {
        try {
            $v = [double]$parts[0]
            return @($v, $v, $v)
        } catch {
            return $null
        }
    }
    if ($parts.Count -lt 3) { return $null }
    $values = New-Object System.Collections.Generic.List[double]
    foreach ($part in $parts[0..2]) {
        try {
            $values.Add([double]$part) | Out-Null
        } catch {
            return $null
        }
    }
    return @($values)
}

function Get-FirstScaleForTag([string]$Body, [string]$Tag) {
    $pattern = [regex]::Escape($Tag) + '(?s).*?glm::vec3\((?<position>[^)]*)\).*?glm::vec3\((?<scale>[^)]*)\)'
    $match = [regex]::Match($Body, $pattern)
    if (-not $match.Success) { return $null }
    return Parse-Vec3Values $match.Groups["scale"].Value
}

$seedFiles = @(Get-ChildItem -Path $SeedRoot -Filter scene_seed.json -Recurse | Where-Object { $_.FullName -notmatch "\\schema\\" })
if (-not [string]::IsNullOrWhiteSpace($SceneId)) {
    $seedFiles = @($seedFiles | Where-Object { $_.Directory.Name -eq $SceneId })
}
if ($seedFiles.Count -lt 1) { Add-Failure "no scene seeds found for composition validation" }

$engineScenesPath = Join-Path $root "src/Core/Engine_Scenes.cpp"
$engineScenesSource = ""
if (Test-Path $engineScenesPath) {
    $engineScenesSource = Get-Content $engineScenesPath -Raw
} else {
    Add-Failure "Engine_Scenes.cpp missing for runtime layout validation"
}

$layoutContracts = $null
if (Test-Path $RuntimeLayoutContractsPath) {
    $layoutContracts = Get-Content $RuntimeLayoutContractsPath -Raw | ConvertFrom-Json
    if ([int]$layoutContracts.schema -ne 1) {
        Add-Failure "runtime layout contract schema must be 1"
    }
} else {
    Add-Failure "runtime layout contracts missing: $RuntimeLayoutContractsPath"
}

foreach ($file in $seedFiles) {
    $seed = Get-Content $file.FullName -Raw | ConvertFrom-Json
    $ctx = [string]$seed.id
    $contactTolerance = [double]$seed.validation.contact_tolerance_m
    $sceneMaxGap = [double]$seed.validation.max_gap_m
    $maxRoundRatio = [double]$seed.validation.max_round_prop_axis_ratio

    foreach ($asset in @($seed.assets)) {
        $assetCtx = "$ctx asset '$($asset.id)'"
        $posY = [double]$asset.transform.position[1]
        $scale = @($asset.transform.scale)
        foreach ($component in $scale) {
            if ([double]$component -le 0.0) { Add-Failure "$assetCtx scale components must be positive" }
        }
        if ($asset.contact -eq "grounded" -and -not (Has-Property $asset "expected_surface_y")) {
            Add-Failure "$assetCtx grounded contact must declare expected_surface_y"
        }
        if ($asset.contact -eq "contained_liquid") {
            if (-not ([string]$asset.role).Contains("liquid")) {
                Add-Failure "$assetCtx contained_liquid contact should have a liquid role"
            }
        }
        if ($asset.contact -eq "mounted" -and -not (Has-Property $asset "anchor")) {
            Add-Failure "$assetCtx mounted asset must declare anchor"
        }
        if ([string]$asset.role -match "^round_prop$|^cylinder_prop$" -or [string]$asset.source -match "primitive:cylinder") {
            $maxScale = ($scale | ForEach-Object { [double]$_ } | Measure-Object -Maximum).Maximum
            $minScale = ($scale | ForEach-Object { [double]$_ } | Measure-Object -Minimum).Minimum
            $xScale = [double]$scale[0]
            $zScale = [double]$scale[2]
            $horizontalRatio = [Math]::Max($xScale, $zScale) / [Math]::Max(0.0001, [Math]::Min($xScale, $zScale))
            if ($minScale -le 0.0 -or $horizontalRatio -gt $maxRoundRatio) {
                Add-Failure "$assetCtx round/cylindrical horizontal scale ratio exceeds $maxRoundRatio"
            }
        }
    }

    foreach ($group in @($seed.authored_groups)) {
        $groupCtx = "$ctx authored group '$($group.id)'"
        $groupMaxGap = [double]$group.contact_rules.max_gap_m
        if ($groupMaxGap -gt $sceneMaxGap) {
            Add-Failure "$groupCtx max_gap_m $groupMaxGap exceeds scene max_gap_m $sceneMaxGap"
        }
        if ([bool]$group.primary -and @($group.parts).Count -lt 4) {
            Add-Failure "$groupCtx primary group must have at least four named parts"
        }
        if ([bool]$group.contact_rules.requires_supports -and -not ((@($group.parts) -join " ") -match "support|post|bracket|frame|mullion|arch")) {
            Add-Failure "$groupCtx requires supports but parts do not name support/post/frame/bracket elements"
        }
        if ([bool]$group.contact_rules.must_contain_liquid -and -not ((@($group.parts) -join " ") -match "liquid|water|lava|creek|foam")) {
            Add-Failure "$groupCtx must_contain_liquid but parts do not name liquid/water/lava elements"
        }
    }

    foreach ($layer in @("foreground", "midground", "background")) {
        $text = [string]$seed.intent.foreground_midground_background.$layer
        if ($text.Length -lt 12) {
            Add-Failure "$ctx intent layer '$layer' is too vague for composition validation"
        }
    }
}

if ($null -ne $layoutContracts) {
    $sceneIdsWithSeeds = @{}
    foreach ($file in $seedFiles) {
        $seed = Get-Content $file.FullName -Raw | ConvertFrom-Json
        $sceneIdsWithSeeds[[string]$seed.id] = $true
    }

    foreach ($contract in @($layoutContracts.scenes)) {
        $sceneIdValue = [string]$contract.id
        if (-not [string]::IsNullOrWhiteSpace($SceneId) -and $sceneIdValue -ne $SceneId) { continue }
        if (-not $sceneIdsWithSeeds.ContainsKey($sceneIdValue)) {
            Add-Failure "runtime layout contract '$sceneIdValue' has no matching scene seed"
            continue
        }

        $builder = [string]$contract.builder
        $body = Get-BuilderBody $engineScenesSource $builder
        if ([string]::IsNullOrWhiteSpace($body)) {
            Add-Failure "$sceneIdValue builder body '$builder' was not found"
            continue
        }

        foreach ($tagToken in @($contract.required_tag_tokens)) {
            if ($body -notmatch [regex]::Escape([string]$tagToken)) {
                Add-Failure "$sceneIdValue runtime builder missing required tag token '$tagToken'"
            }
        }

        foreach ($group in @($contract.support_groups)) {
            $groupCtx = "$sceneIdValue runtime group '$($group.id)'"
            foreach ($token in @($group.support_tokens)) {
                if ($body -notmatch [regex]::Escape([string]$token)) {
                    Add-Failure "$groupCtx missing support token '$token'"
                }
            }
            foreach ($token in @($group.mounted_tokens)) {
                if ($body -notmatch [regex]::Escape([string]$token)) {
                    Add-Failure "$groupCtx missing mounted token '$token'"
                }
            }
            foreach ($token in @($group.liquid_tokens)) {
                if ($body -notmatch [regex]::Escape([string]$token)) {
                    Add-Failure "$groupCtx missing liquid/flow token '$token'"
                }
            }
        }

        foreach ($check in @($contract.round_prop_checks)) {
            $tag = [string]$check.tag
            $scale = Get-FirstScaleForTag $body $tag
            if ($null -eq $scale -or @($scale).Count -ne 3) {
                Add-Failure "$sceneIdValue round prop '$tag' scale could not be parsed from runtime builder"
                continue
            }
            $x = [Math]::Abs([double]$scale[0])
            $z = [Math]::Abs([double]$scale[2])
            $ratio = [Math]::Max($x, $z) / [Math]::Max(0.0001, [Math]::Min($x, $z))
            if ($ratio -gt [double]$check.max_horizontal_axis_ratio) {
                Add-Failure "$sceneIdValue round prop '$tag' horizontal axis ratio $ratio exceeds $($check.max_horizontal_axis_ratio)"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Scene composition stability tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) { Write-Host " - $failure" -ForegroundColor Red }
    exit 1
}

Write-Host "Scene composition stability tests passed: seeds=$($seedFiles.Count)" -ForegroundColor Green
