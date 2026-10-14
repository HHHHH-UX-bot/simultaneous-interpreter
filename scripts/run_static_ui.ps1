param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('assets','simulate','build','probe','program')]
    [string]$Action,
    [string]$VivadoPath,
    [string]$PythonPath
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$assets = Join-Path $root 'output\static_ui'
$out = Join-Path $root 'output\fpga_static'
$temporary = Join-Path $root 'tmp'
New-Item -ItemType Directory -Path (Join-Path $out 'logs') -Force | Out-Null
$programResult = Join-Path $out 'hardware\program_result.json'
$attemptId = [guid]::NewGuid().ToString()
$startedAt = [DateTime]::UtcNow.ToString('o')
if ($Action -eq 'program') {
    New-Item -ItemType Directory -Path (Split-Path -Parent $programResult) -Force | Out-Null
    [ordered]@{Status='pending';AttemptId=$attemptId;StartedAtUTC=$startedAt;ScreenAcceptance='pending_user_confirmation'} |
        ConvertTo-Json | Set-Content -LiteralPath $programResult -Encoding utf8
}
trap {
    if ($Action -eq 'program') {
        [ordered]@{Status='failed';AttemptId=$attemptId;StartedAtUTC=$startedAt;FailedAtUTC=[DateTime]::UtcNow.ToString('o');ErrorMessage=$_.Exception.Message;ScreenAcceptance='unverified'} |
            ConvertTo-Json | Set-Content -LiteralPath $programResult -Encoding utf8
    }
    throw $_
}
if (-not $PythonPath) {
    $pythonCommand = Get-Command python -ErrorAction SilentlyContinue
    if ($pythonCommand) { $PythonPath = $pythonCommand.Source }
}

function Invoke-Checked {
    param([string]$Tool,[string[]]$Arguments,[string]$Log,[string]$Marker)
    Write-Output "Running $(Split-Path -Leaf $Tool); log: $Log"
    & $Tool @Arguments 2>&1 | Tee-Object -FilePath $Log | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath $Log -Tail 25 | Write-Output
        throw "Tool failed with exit code $LASTEXITCODE"
    }
    if ($Marker -and -not (Select-String -LiteralPath $Log -SimpleMatch $Marker -Quiet)) {
        Get-Content -LiteralPath $Log -Tail 25 | Write-Output
        throw "Missing success marker $Marker"
    }
    if (Select-String -LiteralPath $Log -SimpleMatch 'CRITICAL WARNING:' -Quiet) {
        throw "Critical warning in $Log"
    }
    if ($Marker) { Write-Output $Marker }
}

function Test-Assets {
    $manifest = Get-Content -LiteralPath (Join-Path $assets 'asset_manifest.json') -Raw | ConvertFrom-Json
    if ($manifest.status -ne 'passed') { throw 'Static UI assets have not passed validation.' }
    if ((Get-FileHash -LiteralPath (Join-Path $root 'output\ui\index.html')).Hash -ne $manifest.html_sha256) { throw 'HTML differs from rendered assets; rerun assets.' }
    if ((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'render_static_ui.py')).Hash -ne $manifest.generator_sha256) { throw 'Asset generator changed; rerun assets.' }
    foreach ($property in $manifest.files.PSObject.Properties) {
        if ((Get-FileHash -LiteralPath (Join-Path $assets $property.Name)).Hash -ne $property.Value) { throw "Asset changed: $($property.Name)" }
    }
}

function Get-InputHashes {
    $names = @(
        'rtl/static_ui_top.v','rtl/static_ui_video.v','rtl/hdmi_clock.v','rtl/async_rst_sync.v',
        'rtl/dvi_encoder.v','rtl/dvi_transmitter_top.v','rtl/serializer_10_to_1.v',
        'sim/tb_static_ui_video.sv','sim/tb_static_ui_top.sv','sim/tb_tmds_encoder.sv',
        'scripts/render_static_ui.py','scripts/generate_tmds_vectors.py',
        'scripts/run_static_ui.ps1','scripts/build_static_ui.tcl','scripts/hardware_static_ui.tcl',
        'constraints/hdmi.xdc','output/ui/index.html',
        'output/static_ui/asset_manifest.json','output/static_ui/static_ui_config.vh',
        'output/static_ui/static_ui_tilemap.mem','output/static_ui/static_ui_tiles.mem',
        'output/static_ui/static_ui_palette.mem','output/static_ui/static_ui_expected.mem'
    )
    foreach ($name in $names) {
        [pscustomobject]@{File=$name;SHA256=(Get-FileHash -LiteralPath (Join-Path $root $name)).Hash}
    }
}

function Assert-Hashes {
    param($Verified)
    foreach ($current in @(Get-InputHashes)) {
        $saved = @($Verified | Where-Object { $_.File -eq $current.File })
        if ($saved.Count -ne 1 -or $saved[0].SHA256 -ne $current.SHA256) { throw "Evidence is stale for $($current.File); rerun the required stage." }
    }
}

function Stage-Data {
    param([string]$Directory)
    New-Item -ItemType Directory -Path $Directory -Force | Out-Null
    foreach ($name in @('static_ui_tilemap.mem','static_ui_tiles.mem','static_ui_palette.mem','static_ui_expected.mem')) {
        Copy-Item -LiteralPath (Join-Path $assets $name) -Destination (Join-Path $Directory $name) -Force
    }
}

if ($Action -eq 'assets') {
    if (-not $PythonPath) { throw 'Supply the machine environment interpreter using -PythonPath.' }
    Invoke-Checked -Tool $PythonPath -Arguments @('-X','utf8',(Join-Path $PSScriptRoot 'render_static_ui.py')) -Log (Join-Path $out 'logs\assets.log') -Marker '"status": "passed"'
    exit
}
Test-Assets
if (-not $VivadoPath) {
    $launcher = Get-Command vivado.bat -ErrorAction SilentlyContinue
    if ($launcher) { $VivadoPath = $launcher.Source }
    elseif ($env:XILINX_VIVADO) { $VivadoPath = Join-Path $env:XILINX_VIVADO 'bin\vivado.bat' }
    else { throw 'Supply -VivadoPath or initialize the Vivado command environment.' }
}
if (-not (Test-Path -LiteralPath $VivadoPath -PathType Leaf)) { throw 'Vivado launcher missing.' }
$bin = Split-Path -Parent $VivadoPath
$install = Split-Path -Parent $bin

if ($Action -eq 'simulate') {
    if (-not $PythonPath) { throw 'Supply -PythonPath for the machine environment.' }
    $before = @(Get-InputHashes)
    [pscustomobject]@{Status='simulating'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'simulation_result.json') -Encoding utf8
    Invoke-Checked -Tool $PythonPath -Arguments @('-X','utf8',(Join-Path $PSScriptRoot 'generate_tmds_vectors.py')) -Log (Join-Path $out 'logs\tmds_vectors.log') -Marker 'TMDS fixture:'
    $simdir = Join-Path $temporary 'static_ui_sim'
    Stage-Data $simdir
    foreach ($name in @('tmds_input.mem','tmds_expected.mem')) {
        Copy-Item -LiteralPath (Join-Path $temporary "sim\$name") -Destination (Join-Path $simdir $name) -Force
    }
    Push-Location $simdir
    try {
        $rtl = @('static_ui_top.v','static_ui_video.v','hdmi_clock.v','async_rst_sync.v','dvi_encoder.v','dvi_transmitter_top.v','serializer_10_to_1.v') | ForEach-Object { Join-Path $root "rtl\$_" }
        $tests = @('tb_static_ui_video.sv','tb_static_ui_top.sv','tb_tmds_encoder.sv') | ForEach-Object { Join-Path $root "sim\$_" }
        $glbl = Join-Path $install 'data\verilog\src\glbl.v'
        Invoke-Checked -Tool (Join-Path $bin 'xvlog.bat') -Arguments (@('--sv','-i',$assets)+$rtl+$tests+@($glbl)) -Log (Join-Path $out 'logs\xvlog.log')
        foreach ($test in @(@('tb_static_ui_video','PASS_STATIC_UI_VIDEO'),@('tb_tmds_encoder','PASS_TMDS_ENCODER'),@('tb_static_ui_top','PASS_STATIC_UI_TOP'))) {
            $top = $test[0]
            Invoke-Checked -Tool (Join-Path $bin 'xelab.bat') -Arguments @('-L','unisims_ver','-L','secureip',"work.$top",'work.glbl','-s',$top,'--debug','off') -Log (Join-Path $out "logs\xelab_$top.log")
            Invoke-Checked -Tool (Join-Path $bin 'xsim.bat') -Arguments @($top,'-runall','-log',(Join-Path $out "logs\xsim_$top.log")) -Log (Join-Path $out "logs\simulate_$top.log") -Marker $test[1]
        }
        Assert-Hashes $before
        [pscustomobject]@{Status='passed';SourceHashes=$before;Tests=@('full_image_two_frames','independent_tmds_model','serial_symbols_all_lanes_active_and_blanking')} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $out 'simulation_result.json') -Encoding utf8
        Write-Output 'PASS_ALL_STATIC_SIMULATIONS'
    } finally { Pop-Location }
} elseif ($Action -eq 'build') {
    $sim = Get-Content -LiteralPath (Join-Path $out 'simulation_result.json') -Raw | ConvertFrom-Json
    if ($sim.Status -ne 'passed') { throw 'Simulation must pass before build.' }
    Assert-Hashes $sim.SourceHashes
    [pscustomobject]@{Status='building'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'build_result.json') -Encoding utf8
    $builddir = Join-Path $temporary 'static_ui_build'
    Stage-Data $builddir
    Push-Location $builddir
    try {
        Invoke-Checked -Tool $VivadoPath -Arguments @('-mode','batch','-source',(Join-Path $PSScriptRoot 'build_static_ui.tcl'),'-log',(Join-Path $out 'logs\build.log'),'-journal',(Join-Path $builddir 'build.jou')) -Log (Join-Path $out 'logs\build_console.log') -Marker 'PASS_STATIC_BUILD:'
        Assert-Hashes $sim.SourceHashes
        $result = Get-Content -LiteralPath (Join-Path $out 'build_result.json') -Raw | ConvertFrom-Json
        $result | Add-Member -NotePropertyName Status -NotePropertyValue 'passed' -Force
        $result | Add-Member -NotePropertyName SourceHashes -NotePropertyValue $sim.SourceHashes -Force
        $result | Add-Member -NotePropertyName BitstreamSHA256 -NotePropertyValue (Get-FileHash -LiteralPath (Join-Path $out 'static_ui_top.bit')).Hash -Force
        $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $out 'build_result.json') -Encoding utf8
    } finally { Pop-Location }
} else {
    $build = Get-Content -LiteralPath (Join-Path $out 'build_result.json') -Raw | ConvertFrom-Json
    if ($build.Status -ne 'passed') { throw 'Build must pass before hardware stages.' }
    Assert-Hashes $build.SourceHashes
    if ((Get-FileHash -LiteralPath (Join-Path $out 'static_ui_top.bit')).Hash -ne $build.BitstreamSHA256) { throw 'Bitstream differs from verified build.' }
    if (-not (Get-NetTCPConnection -State Listen -LocalPort 3121 -ErrorAction SilentlyContinue)) {
        Start-Process -FilePath (Join-Path $bin 'hw_server.bat') -ArgumentList '-s','tcp::3121' -WindowStyle Hidden
        for ($i=0;$i -lt 20;$i++) {
            Start-Sleep -Milliseconds 500
            if (Get-NetTCPConnection -State Listen -LocalPort 3121 -ErrorAction SilentlyContinue) { break }
        }
    }
    Push-Location (Join-Path $temporary 'static_ui_build')
    try {
        $marker = if ($Action -eq 'probe') { 'PASS_STATIC_HW_TARGET:' } else { 'PASS_STATIC_PROGRAM:' }
        $hardwareArgs = @('-mode','batch','-source',(Join-Path $PSScriptRoot 'hardware_static_ui.tcl'),'-log',(Join-Path $out "logs\$Action.log"),'-journal',(Join-Path $temporary "static_ui_$Action.jou"),'-tclargs',$Action)
        if ($Action -eq 'program') { $hardwareArgs += @($attemptId,$build.BitstreamSHA256) }
        Invoke-Checked -Tool $VivadoPath -Arguments $hardwareArgs -Log (Join-Path $out "logs\${Action}_console.log") -Marker $marker
        if ($Action -eq 'program') {
            Assert-Hashes $build.SourceHashes
            if ((Get-FileHash -LiteralPath (Join-Path $out 'static_ui_top.bit')).Hash -ne $build.BitstreamSHA256) { throw 'Bitstream changed during programming.' }
            $result = Get-Content -LiteralPath $programResult -Raw | ConvertFrom-Json
            if ($result.Status -ne 'configured' -or $result.AttemptId -ne $attemptId -or $result.BitstreamSHA256 -ne $build.BitstreamSHA256 -or
                $result.DONE -ne 1 -or $result.EOS -ne 1 -or $result.CRC_ERROR -ne 0 -or -not $result.Target) { throw 'Programming evidence does not match this verified attempt.' }
            $result | Add-Member -NotePropertyName Status -NotePropertyValue 'passed' -Force
            $result | Add-Member -NotePropertyName StartedAtUTC -NotePropertyValue $startedAt -Force
            $result | Add-Member -NotePropertyName VerifiedAtUTC -NotePropertyValue ([DateTime]::UtcNow.ToString('o')) -Force
            $result | Add-Member -NotePropertyName BuildEvidenceSHA256 -NotePropertyValue (Get-FileHash -LiteralPath (Join-Path $out 'build_result.json')).Hash -Force
            $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $programResult -Encoding utf8
        }
    } finally { Pop-Location }
}
