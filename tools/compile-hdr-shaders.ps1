param([Parameter(Mandatory=$true)][string]$VulkanSdk)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'compile-pbr-shaders.ps1') -VulkanSdk $VulkanSdk
$shaderRoot = Join-Path $PSScriptRoot '../libs/pure3d/pddi/vulkan/shaders'
$compiler = Join-Path $VulkanSdk 'Bin/glslangValidator.exe'
$validator = Join-Path $VulkanSdk 'Bin/spirv-val.exe'
$sources = @('hdr_resolve.vert', 'hdr_resolve.frag', 'volumetric_light.frag', 'volumetric_froxel.comp', 'hdr_exposure.comp', 'simple.frag', 'compact.frag',
    'lit_legacy.frag', 'smoke_legacy.frag', 'lit_phong.frag', 'smoke_phong.frag',
    'lit_toon.frag', 'smoke_toon.frag', 'lit_outline.frag', 'smoke_outline.frag')
$binaryPath = [System.IO.Path]::GetTempFileName()
try {
    foreach ($source in $sources) {
        & $compiler -V (Join-Path $shaderRoot $source) -o $binaryPath
        if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $source" }
        & $validator --target-env vulkan1.0 $binaryPath
        if ($LASTEXITCODE -ne 0) { throw "Validation failed: $source" }
    }
    foreach ($source in $sources) {
        $symbol = $source.Replace('.', '_') + '_spv'
        & $compiler -V (Join-Path $shaderRoot $source) --vn $symbol -o (Join-Path $shaderRoot ($symbol + '.h'))
        if ($LASTEXITCODE -ne 0) { throw "Header generation failed: $source" }
    }
} finally { Remove-Item -LiteralPath $binaryPath }
