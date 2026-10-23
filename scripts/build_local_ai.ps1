param(
    [string]$Python='',
    [string]$Vivado='C:\Xilinx\Vivado\2018.3\bin\vivado.bat',
    [string]$Gcc='C:\Xilinx\SDK\2018.3\gnu\aarch32\nt\gcc-arm-none-eabi\bin\arm-none-eabi-gcc.exe',
    [switch]$RebuildFpga,[switch]$Test
)
$ErrorActionPreference='Stop'
if (-not $Python) {
    if ($env:Z7_PYTHON) { $Python=$env:Z7_PYTHON }
    elseif (Test-Path (Join-Path $env:USERPROFILE 'miniconda3\envs\machine\python.exe')) { $Python=Join-Path $env:USERPROFILE 'miniconda3\envs\machine\python.exe' }
    elseif (Test-Path (Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe')) { $Python=Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' }
    else { $Python='python' }
}
$taskRoot=Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $taskRoot
try {
    & ./scripts/generate_local_voices.ps1
    & $Python -B scripts/prepare_local_ai.py
    if($LASTEXITCODE){throw 'Local model generation failed'}
    & $Python -B scripts/generate_subtitle_assets.py --font C:\Windows\Fonts\msyh.ttc
    if($LASTEXITCODE){throw 'Font generation failed'}
    if($RebuildFpga) {
        & $Python -B scripts/run_audio_loopback.py simulate --xsim-bin (Split-Path -Parent $Vivado)
        if($LASTEXITCODE){throw 'Audio simulation failed'}
        & $Python -B scripts/build_remote_display.py --vivado $Vivado
        if($LASTEXITCODE){throw 'Vivado 2018.3 implementation failed'}
    }
    & $Python -B scripts/build_cloud_app.py --gcc $Gcc --mode zhen --speech --touch --arm-tls --arm-tls-packets --arm-tls-active-recv --low-latency-playback --uart-baud 2000000 --remote-display --local-ai --output-dir output/local_ai/microphone
    if($LASTEXITCODE){throw 'ARM build failed'}
    & $Python -B scripts/run_z7_nano_usb_host.py build --gcc $Gcc
    if($LASTEXITCODE){throw 'USB host build failed'}
    if($Test) {
        & ./scripts/generate_local_voices.ps1 -Rates -1,1 -Subdirectory heldout
        & ./scripts/generate_local_voices.ps1 -Rates 0 -Subdirectory unknown -InputFile tests/local_unknown.json
        & $Python -B scripts/validate_local_ai.py
        if($LASTEXITCODE){throw 'Local inference/UI validation failed'}
        & $Python -B scripts/build_cloud_app.py --gcc $Gcc --mode zhen --speech --touch --arm-tls --arm-tls-packets --arm-tls-active-recv --low-latency-playback --uart-baud 2000000 --remote-display --local-ai --selftest --selftest-once --selftest-pcm output/local_ai/board_selftest_pcm.c --output-dir output/local_ai/board_selftest
        if($LASTEXITCODE){throw 'On-board synthetic self-test build failed'}
    }
    Write-Output 'PASS_LOCAL_AI_BUILD'
} finally {Pop-Location}
