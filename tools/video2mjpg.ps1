# ============================================================
# video2mjpg.ps1 —— 把普通视频（MP4/AVI/MKV/WEBM…）批量转成
# Memoria OS VIDEO 模式可播放的 .mjpg（裸 MJPEG 帧流）
#
# 用法（先装 ffmpeg，见 README 视频章节）：
#   powershell -ExecutionPolicy Bypass -File video2mjpg.ps1
#   powershell -ExecutionPolicy Bypass -File video2mjpg.ps1 -InputDir ".\我的视频" -Width 320 -Height 240 -Fps 10
#
# 参数：
#   InputDir  源视频文件夹（默认 .\videos_in）
#   OutDir    输出文件夹（默认 .\videos_out）
#   Width/Height  输出分辨率（默认 320x240，别超过 480x320）
#   Fps       输出帧率（默认 10，ESP32 软解建议 8~15）
#   Quality   JPEG 质量 2(高画质大文件)~31(低画质小文件)，默认 8
# ============================================================
param(
    [string]$InputDir = ".\videos_in",
    [string]$OutDir   = ".\videos_out",
    [int]$Width  = 320,
    [int]$Height = 240,
    [int]$Fps    = 10,
    [int]$Quality = 8
)

$ErrorActionPreference = "Stop"
if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    Write-Host "[错误] 没找到 ffmpeg。先安装:" -ForegroundColor Red
    Write-Host "   winget install ffmpeg" -ForegroundColor Yellow
    Write-Host "   或去 https://www.gyan.dev/ffmpeg/builds/ 下载 release 版，把 bin 目录加进 PATH。"
    exit 1
}
if (-not (Test-Path $InputDir)) {
    Write-Host "[错误] 源目录不存在: $InputDir（建个文件夹把视频放进去，或 -InputDir 指定）" -ForegroundColor Red
    exit 1
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$videos = Get-ChildItem $InputDir -File | Where-Object {
    $_.Extension -match '\.(mp4|avi|mkv|webm|mov|flv|wmv|ts|m4v)$'
}
if ($videos.Count -eq 0) {
    Write-Host "[提示] $InputDir 里没有找到视频文件。" -ForegroundColor Yellow
    exit 0
}

Write-Host "共 $($videos.Count) 个视频，转 ${Width}x${Height} @ ${Fps}fps，质量 $Quality"
$idx = 0
foreach ($v in $videos) {
    $idx++
    $out = Join-Path $OutDir ($v.BaseName + ".mjpg")
    Write-Host ("  [{0}/{1}] {2} ... " -f $idx, $videos.Count, $v.Name) -NoNewline
    & ffmpeg -y -i $v.FullName -vf "scale=$Width`:$Height" -r $Fps -q:v $Quality -c:v mjpeg -f mjpeg $out 2>$null
    if ($LASTEXITCODE -eq 0 -and (Test-Path $out)) {
        Write-Host ("OK  {0} KB" -f [math]::Round((Get-Item $out).Length / 1KB))
    } else {
        Write-Host "失败，看上面 ffmpeg 报错" -ForegroundColor Red
    }
}
Write-Host "完成。把 $OutDir 里的 .mjpg 拷进 TF 卡 /mem_fat/video/ 即可播放。"
