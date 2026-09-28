param([string]$GeneratedDir = (Join-Path $PSScriptRoot '..\recomp\generated'))
$ErrorActionPreference = 'Stop'
$path = Join-Path $GeneratedDir 'chunks\chunk_2182_rpx1_04214020.c'
$source = Get-Content -LiteralPath $path -Raw
foreach ($address in @('04215480', '042155D4', '04215778')) {
    $label = "label_${address}:"
    $hook = "    if (ctx->host_call && ppc_host_call(ctx, 0x${address}u)) return;"
    if (-not $source.Contains($label)) { throw "Missing BOTW v208 decoder label $label" }
    if (-not $source.Contains($hook)) { $source = $source.Replace($label, "$label`n$hook") }
}
# Mechanical post-generation rewrite: same-chunk gotos otherwise bypass the
# dispatcher and its native decoder entry hooks.
if($source -ne (Get-Content -LiteralPath $path -Raw)) {
    Set-Content -LiteralPath $path -Value $source -NoNewline -Encoding utf8
}

# The v208 SafeString terminator callback at 030B0C38 is exactly one BLR.
# In the observed resource-loading hot chunks, devirtualize this one target
# into the caller's continuation. Other callbacks keep the original dispatch.
# Preserve LR, CTR, and the instruction budget, including a bounded yield.
foreach ($chunk in @('chunk_1085_rpx1_030F0020.c','chunk_1086_rpx1_030F4020.c',
    'chunk_1101_rpx1_03130020.c','chunk_1268_rpx1_033CC020.c',
    'chunk_1269_rpx1_033D0020.c','chunk_1396_rpx1_035CC020.c')) {
    $path = Join-Path $GeneratedDir ('chunks\' + $chunk)
    $source = Get-Content -LiteralPath $path -Raw
    $pattern = '(ctx->lr = 0x([0-9A-F]{8})u;)(\r?\n)(            ctx->pc = target;\r?\n            return;)'
    $rewritten = [regex]::Replace($source,$pattern,{
        param($match)
        $next=$match.Groups[2].Value
        if (-not $source.Contains("label_${next}:")) { return $match.Value }
        $nl=$match.Groups[3].Value
        return $match.Groups[1].Value+$nl+
            "            if (target == 0x030B0C38u) { // v208 SafeString no-op"+$nl+
            "                ctx->pc = ctx->lr;"+$nl+
            "                if (--ctx->downcount <= 0) return;"+$nl+
            "                goto label_${next};"+$nl+
            "            }"+$nl+$match.Groups[4].Value
    })
    if($rewritten -ne $source) {
        Set-Content -LiteralPath $path -Value $rewritten -NoNewline -Encoding utf8
    }
}
foreach ($entry in @(@('chunk_1269_rpx1_033D0020.c','033D2B74'), @('chunk_1396_rpx1_035CC020.c','035CF358'))) {
    $path = Join-Path $GeneratedDir ('chunks\' + $entry[0])
    $source = Get-Content -LiteralPath $path -Raw
    $address = $entry[1]
    $label = "label_${address}:"
    $hook = "    if (ctx->host_call && ppc_host_call(ctx, 0x${address}u)) return;"
    if (-not $source.Contains($label)) { throw "Missing BOTW v208 string label $label" }
    if (-not $source.Contains($hook)) {
        $source = $source.Replace($label, "$label`n$hook")
        Set-Content -LiteralPath $path -Value $source -NoNewline -Encoding utf8
    }
}
