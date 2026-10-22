param(
    [string]$Python = 'C:\Users\Administrator\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe',
    [string]$Vivado = 'C:\Xilinx\Vivado\2018.3\bin\vivado.bat',
    [string]$Gcc = 'C:\Xilinx\SDK\2018.3\gnu\aarch32\nt\gcc-arm-none-eabi\bin\arm-none-eabi-gcc.exe',
    [switch]$RebuildFpga
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $taskRoot
try {
    & $Python scripts/generate_subtitle_assets.py --font C:\Windows\Fonts\msyh.ttc
    if($LASTEXITCODE){throw 'Font generation failed'}
    if($RebuildFpga) {
        $taskXsim=Split-Path -Parent $Vivado
        & $Python scripts/run_audio_loopback.py simulate --xsim-bin $taskXsim
        if($LASTEXITCODE){throw 'Audio simulation failed'}
        & $Python scripts/run_audio_loopback.py build --vivado $Vivado --xsim-bin $taskXsim
        if($LASTEXITCODE){throw 'Vivado implementation failed'}
    }
    & $Python scripts/build_cloud_app.py --gcc $Gcc --mode zhen --speech --touch --arm-tls --arm-tls-packets --arm-tls-active-recv --low-latency-playback --uart-baud 2000000 --output-dir output/tts/microphone_final
    if($LASTEXITCODE){throw 'ARM application build failed'}
    & $Python scripts/run_z7_nano_usb_host.py build --gcc $Gcc
    if($LASTEXITCODE){throw 'Touch host build failed'}
    Write-Host 'PASS_TTS_BUILD'
} finally {Pop-Location}
