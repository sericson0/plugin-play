# Tone player: synthesizes a 1 s 440 Hz sine WAV and loops it via SoundPlayer
# (renders through this powershell.exe's audio session on the default device).
$sr = 44100; $amp = 0.15; $freq = 440.0
$wav = Join-Path $env:TEMP "pluginplay_tone.wav"

$n = $sr  # 1 second mono 16-bit
$dataLen = $n * 2
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)
$bw.Write([System.Text.Encoding]::ASCII.GetBytes("RIFF")); $bw.Write([int](36 + $dataLen))
$bw.Write([System.Text.Encoding]::ASCII.GetBytes("WAVEfmt ")); $bw.Write([int]16)
$bw.Write([int16]1); $bw.Write([int16]1)          # PCM, mono
$bw.Write([int]$sr); $bw.Write([int]($sr * 2))    # sample rate, byte rate
$bw.Write([int16]2); $bw.Write([int16]16)         # block align, bits
$bw.Write([System.Text.Encoding]::ASCII.GetBytes("data")); $bw.Write([int]$dataLen)
for ($i = 0; $i -lt $n; $i++) {
    $s = [int16]([math]::Round($amp * 32767 * [math]::Sin(2 * [math]::PI * $freq * $i / $sr)))
    $bw.Write($s)
}
$bw.Flush()
[System.IO.File]::WriteAllBytes($wav, $ms.ToArray())

$player = New-Object System.Media.SoundPlayer($wav)
$player.PlayLooping()
Start-Sleep -Seconds 120   # parent kills us when the test is done
