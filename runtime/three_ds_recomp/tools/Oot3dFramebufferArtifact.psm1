Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Read-Oot3dFramebufferBmp {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    $resolved = [IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "Framebuffer artifact is missing: $resolved"
    }
    $bytes = [IO.File]::ReadAllBytes($resolved)
    if ($bytes.Length -lt 54 -or
        $bytes[0] -ne [byte][char]'B' -or
        $bytes[1] -ne [byte][char]'M') {
        throw "Framebuffer artifact is not a BMP file: $resolved"
    }

    $pixelOffset = [BitConverter]::ToUInt32($bytes, 10)
    $dibHeaderSize = [BitConverter]::ToUInt32($bytes, 14)
    $width = [BitConverter]::ToInt32($bytes, 18)
    $signedHeight = [BitConverter]::ToInt32($bytes, 22)
    $bitsPerPixel = [BitConverter]::ToUInt16($bytes, 28)
    $compression = [BitConverter]::ToUInt32($bytes, 30)
    if ($dibHeaderSize -lt 40 -or $pixelOffset -lt 54 -or
        $width -le 0 -or $signedHeight -eq 0 -or
        $bitsPerPixel -ne 24 -or $compression -ne 0) {
        throw "Framebuffer BMP must be uncompressed 24-bit RGB"
    }
    $height = [Math]::Abs([int64]$signedHeight)
    $rowStride = [int64](([int64]$width * 3 + 3) -band -4)
    $requiredBytes = [int64]$pixelOffset + $rowStride * $height
    if ($requiredBytes -gt $bytes.LongLength) {
        throw "Framebuffer BMP pixel payload is truncated"
    }

    [pscustomobject]@{
        Path = $resolved
        Bytes = $bytes
        PixelOffset = [int64]$pixelOffset
        Width = $width
        Height = $height
        RowStride = $rowStride
        Sha256 = (
            Get-FileHash -LiteralPath $resolved -Algorithm SHA256
        ).Hash
    }
}

function Get-Oot3dFramebufferMetrics {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [psobject]$Framebuffer,
        [ValidateRange(1024, 10000000)]
        [int]$MaximumSampledPixels = 120000
    )

    $pixelCount =
        [int64]$Framebuffer.Width * [int64]$Framebuffer.Height
    $sampleStep = [Math]::Max(
        1,
        [int][Math]::Ceiling(
            [Math]::Sqrt($pixelCount / $MaximumSampledPixels)))
    $colors = [Collections.Generic.HashSet[int]]::new()
    $sampleCount = 0L
    $chromaticCount = 0L
    $minimumChannel = 255
    $maximumChannel = 0
    for ($y = 0; $y -lt $Framebuffer.Height; $y += $sampleStep) {
        $row =
            $Framebuffer.PixelOffset +
            [int64]$y * $Framebuffer.RowStride
        for ($x = 0; $x -lt $Framebuffer.Width; $x += $sampleStep) {
            $offset = $row + [int64]$x * 3
            $blue = [int]$Framebuffer.Bytes[$offset]
            $green = [int]$Framebuffer.Bytes[$offset + 1]
            $red = [int]$Framebuffer.Bytes[$offset + 2]
            [void]$colors.Add(
                ($red -shl 16) -bor ($green -shl 8) -bor $blue)
            $minimumChannel =
                [Math]::Min(
                    $minimumChannel,
                    [Math]::Min($red, [Math]::Min($green, $blue)))
            $maximumChannel =
                [Math]::Max(
                    $maximumChannel,
                    [Math]::Max($red, [Math]::Max($green, $blue)))
            if ($red -ne $green -or $green -ne $blue) {
                ++$chromaticCount
            }
            ++$sampleCount
        }
    }

    [pscustomobject]@{
        Path = $Framebuffer.Path
        Width = $Framebuffer.Width
        Height = $Framebuffer.Height
        SampleStep = $sampleStep
        SampleCount = $sampleCount
        UniqueColors = $colors.Count
        ChannelRange = $maximumChannel - $minimumChannel
        ChromaticFraction =
            if ($sampleCount -eq 0) {
                0.0
            } else {
                [double]$chromaticCount / $sampleCount
            }
        Sha256 = $Framebuffer.Sha256
    }
}

function Compare-Oot3dFramebufferBmp {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [psobject]$Reference,
        [Parameter(Mandatory)]
        [psobject]$Candidate,
        [ValidateRange(0, 255)]
        [int]$ChangedPixelThreshold = 2,
        [ValidateRange(1024, 10000000)]
        [int]$MaximumSampledPixels = 240000
    )

    if ($Reference.Width -ne $Candidate.Width -or
        $Reference.Height -ne $Candidate.Height) {
        throw "Framebuffer dimensions differ: " +
            "$($Reference.Width)x$($Reference.Height) vs " +
            "$($Candidate.Width)x$($Candidate.Height)"
    }
    $pixelCount =
        [int64]$Reference.Width * [int64]$Reference.Height
    $sampleStep = [Math]::Max(
        1,
        [int][Math]::Ceiling(
            [Math]::Sqrt($pixelCount / $MaximumSampledPixels)))
    $channelDeltaHistogram = [int64[]]::new(256)
    $sampledPixels = 0L
    $sampledChannels = 0L
    $changedPixels = 0L
    $absoluteDeltaSum = 0L
    $squaredDeltaSum = 0L
    $maximumDelta = 0
    for ($y = 0; $y -lt $Reference.Height; $y += $sampleStep) {
        $referenceRow =
            $Reference.PixelOffset +
            [int64]$y * $Reference.RowStride
        $candidateRow =
            $Candidate.PixelOffset +
            [int64]$y * $Candidate.RowStride
        for ($x = 0; $x -lt $Reference.Width; $x += $sampleStep) {
            $referenceOffset = $referenceRow + [int64]$x * 3
            $candidateOffset = $candidateRow + [int64]$x * 3
            $pixelMaximumDelta = 0
            for ($channel = 0; $channel -lt 3; ++$channel) {
                $delta = [Math]::Abs(
                    [int]$Reference.Bytes[
                        $referenceOffset + $channel] -
                    [int]$Candidate.Bytes[
                        $candidateOffset + $channel])
                $absoluteDeltaSum += $delta
                $squaredDeltaSum += [int64]$delta * $delta
                ++$channelDeltaHistogram[$delta]
                ++$sampledChannels
                $pixelMaximumDelta =
                    [Math]::Max($pixelMaximumDelta, $delta)
                $maximumDelta = [Math]::Max($maximumDelta, $delta)
            }
            if ($pixelMaximumDelta -gt $ChangedPixelThreshold) {
                ++$changedPixels
            }
            ++$sampledPixels
        }
    }

    $p95Target = [int64][Math]::Ceiling($sampledChannels * 0.95)
    $p95Count = 0L
    $p95 = 0
    for (; $p95 -lt $channelDeltaHistogram.Count; ++$p95) {
        $p95Count += $channelDeltaHistogram[$p95]
        if ($p95Count -ge $p95Target) {
            break
        }
    }
    [pscustomobject]@{
        ReferencePath = $Reference.Path
        CandidatePath = $Candidate.Path
        Width = $Reference.Width
        Height = $Reference.Height
        SampleStep = $sampleStep
        SampledPixels = $sampledPixels
        MeanAbsoluteChannelDelta =
            if ($sampledChannels -eq 0) {
                0.0
            } else {
                [double]$absoluteDeltaSum / $sampledChannels
            }
        RootMeanSquareChannelDelta =
            if ($sampledChannels -eq 0) {
                0.0
            } else {
                [Math]::Sqrt(
                    [double]$squaredDeltaSum / $sampledChannels)
            }
        ChannelDeltaP95 = $p95
        MaximumChannelDelta = $maximumDelta
        ChangedPixelThreshold = $ChangedPixelThreshold
        ChangedPixelFraction =
            if ($sampledPixels -eq 0) {
                0.0
            } else {
                [double]$changedPixels / $sampledPixels
            }
        ReferenceSha256 = $Reference.Sha256
        CandidateSha256 = $Candidate.Sha256
    }
}

function Get-Oot3dFramebufferSequenceMetrics {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [ValidateCount(2, 60)]
        [string[]]$Paths,
        [ValidateRange(0, 255)]
        [int]$ChangedPixelThreshold = 2,
        [ValidateRange(1024, 10000000)]
        [int]$MaximumSampledPixels = 240000
    )

    $framebuffers = @(
        $Paths |
            ForEach-Object {
                Read-Oot3dFramebufferBmp -Path $_
            }
    )
    $pairs = @()
    for ($index = 1; $index -lt $framebuffers.Count; ++$index) {
        $pairs += Compare-Oot3dFramebufferBmp `
            -Reference $framebuffers[$index - 1] `
            -Candidate $framebuffers[$index] `
            -ChangedPixelThreshold $ChangedPixelThreshold `
            -MaximumSampledPixels $MaximumSampledPixels
    }
    $absoluteDelta =
        $pairs |
        Measure-Object -Property `
            MeanAbsoluteChannelDelta -Average -Maximum
    $rmsDelta =
        $pairs |
        Measure-Object -Property `
            RootMeanSquareChannelDelta -Average -Maximum
    $changedPixels =
        $pairs |
        Measure-Object -Property `
            ChangedPixelFraction -Average -Maximum
    [pscustomobject]@{
        FrameCount = $framebuffers.Count
        PairCount = $pairs.Count
        MeanAbsoluteChannelDelta =
            [double]$absoluteDelta.Average
        MaximumMeanAbsoluteChannelDelta =
            [double]$absoluteDelta.Maximum
        MeanRootMeanSquareChannelDelta =
            [double]$rmsDelta.Average
        MaximumRootMeanSquareChannelDelta =
            [double]$rmsDelta.Maximum
        MeanChangedPixelFraction =
            [double]$changedPixels.Average
        MaximumChangedPixelFraction =
            [double]$changedPixels.Maximum
        MaximumChannelDelta = [int](
            $pairs |
                Measure-Object -Property `
                    MaximumChannelDelta -Maximum
        ).Maximum
        FirstSha256 = $framebuffers[0].Sha256
        LastSha256 = $framebuffers[-1].Sha256
    }
}

Export-ModuleMember -Function `
    Read-Oot3dFramebufferBmp, `
    Get-Oot3dFramebufferMetrics, `
    Compare-Oot3dFramebufferBmp, `
    Get-Oot3dFramebufferSequenceMetrics
