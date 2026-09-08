param([Parameter(Mandatory=$true)][string]$VulkanSdk)
$ErrorActionPreference = 'Stop'
$shaderRoot = Join-Path $PSScriptRoot '../libs/pure3d/pddi/vulkan/shaders'
$compiler = Join-Path $VulkanSdk 'Bin/glslangValidator.exe'
$validator = Join-Path $VulkanSdk 'Bin/spirv-val.exe'
foreach ($toolPath in @($compiler, $validator)) {
    if (!(Test-Path -LiteralPath $toolPath)) { throw "Missing shader tool: $toolPath" }
}
$binaryPath = [System.IO.Path]::GetTempFileName()
try {
    foreach ($variant in @('lit', 'smoke')) {
        $sourcePath = Join-Path $shaderRoot ($variant + '_pbr.frag')
        & $compiler -V $sourcePath -o $binaryPath
        if ($LASTEXITCODE -ne 0) { throw "Shader compilation failed: $variant" }
        & $validator --target-env vulkan1.0 $binaryPath
        if ($LASTEXITCODE -ne 0) { throw "SPIR-V validation failed: $variant" }
    }
    # Validate both variants before regenerating their embedded headers.
    foreach ($variant in @('lit', 'smoke')) {
        $symbol = $variant + '_pbr_frag_spv'
        & $compiler -V (Join-Path $shaderRoot ($variant + '_pbr.frag')) --vn $symbol -o (Join-Path $shaderRoot ($symbol + '.h'))
        if ($LASTEXITCODE -ne 0) { throw "Shader header generation failed: $variant" }
    }
} finally {
    Remove-Item -LiteralPath $binaryPath
}
