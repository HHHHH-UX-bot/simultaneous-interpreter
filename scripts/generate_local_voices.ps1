param([int[]]$Rates=@(-2,0,2),[string]$Subdirectory='voices',[string]$InputFile='assets/local_phrases.json')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskOut=Join-Path $taskRoot ('output/local_ai/'+$Subdirectory)
New-Item -ItemType Directory -Force -Path $taskOut | Out-Null
$taskPhrases=Get-Content -LiteralPath (Join-Path $taskRoot $InputFile) -Raw -Encoding utf8 | ConvertFrom-Json
$taskVoice=New-Object -ComObject SAPI.SpVoice
foreach($taskLanguage in @('zh','en')) {
    $taskVoice.Voice=$taskVoice.GetVoices() | Where-Object {$_.GetDescription() -match $(if($taskLanguage -eq 'zh'){'Huihui'}else{'Zira'})} | Select-Object -First 1
    for($taskIndex=0;$taskIndex -lt $taskPhrases.Count;$taskIndex++) {
        foreach($taskRate in $Rates) {
            $taskFile=Join-Path $taskOut ('{0}_{1}_{2}.wav' -f $taskLanguage,$taskIndex,($taskRate+2))
            $taskStream=New-Object -ComObject SAPI.SpFileStream
            $taskStream.Format.Type=22 # SAFT22kHz16BitMono; training resamples to 16k.
            $taskStream.Open($taskFile,3,$false)
            $taskVoice.AudioOutputStream=$taskStream
            $taskVoice.Rate=$taskRate
            [void]$taskVoice.Speak($taskPhrases[$taskIndex].$taskLanguage)
            $taskStream.Close()
        }
    }
}
Write-Output 'PASS_LOCAL_SYNTHETIC_VOICES'
