# 资源实测脚本：预热后按 1 秒间隔采样 CPU、私有工作集和 GPU 内存，打印中位数和 P95。
#
# 为什么单独成文件而不是内联到命令里：含反斜杠和正则的 PowerShell 经过
# bash 再转义会被吃掉字符，这个坑已经踩过两次，记在「已经犯过的错」里。
#
# 用法：
#   powershell -NoProfile -File tools\measure.ps1 -State "持续渲染" -PetArgs "60","9999" -Samples 300
#
# -Samples 默认 300，对应规程要求的「连续 5 分钟、间隔 1 秒」。
# 少于 300 的结果只能算预备测量，不能回写 roadmap §7。

param(
    [Parameter(Mandatory = $true)][string]$State,
    [int]$Seconds = 400,        # 进程跑多久
    [int]$IdleSeconds = 9999,   # 空闲降级阈值。给大值等于永不停止呈现
    [int]$HideAfter = 0,        # 多少秒后自动隐藏，0 表示不隐藏
    [int]$WarmupSeconds = 120,
    [int]$Samples = 300,
    [string]$Exe = ".\build\bin\Release\CyberDog.exe",
    [string]$Process = "CyberDog" # 性能计数器里的进程名（不带 .exe）。Debug 构建叫 pet
)

# 参数逐个传，不要用数组。从 bash 调过来时数组会被并成一个字符串，
# 程序只收到一个参数，于是测出来的是另一个工况的数。这个坑踩过一次。
# pet.exe 的参数顺序：argv[1]=退出秒数 argv[2]=降级阈值秒 argv[3]=自动隐藏秒。
# 中间不要塞占位值，塞了 argv[3] 就变成占位值，自动隐藏不触发，
# 测出来的会是另一个工况的数——这个错犯过一次，工况 C 白测了一轮。
$PetArgs = @("$Seconds", "$IdleSeconds")
if ($HideAfter -gt 0) { $PetArgs += "$HideAfter" }

$cores = (Get-CimInstance Win32_Processor | Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum

Write-Output "工况: $State"
Write-Output "进程: $Exe $($PetArgs -join ' ')"
Write-Output "逻辑核: $cores   预热: ${WarmupSeconds}s   采样: $Samples 次 x 1s"
if ($Samples -lt 300) {
    Write-Output "注意: 采样少于 300 次，不满足规程，结果只能当预备测量"
}
Write-Output ""

# 忽略光标位置。不设这个的话，鼠标只要停在窗口矩形里，程序就永远进不了
# 空闲态，测出来的「空闲档」其实是渲染档的数。这个错犯过一次。
$env:PET_IGNORE_CURSOR = "1"

$p = Start-Process -FilePath $Exe -ArgumentList $PetArgs -PassThru -WindowStyle Hidden
Start-Sleep -Seconds $WarmupSeconds

$counters = @(
    "\Process($Process)\% Processor Time",
    "\Process($Process)\Working Set - Private"
)
$s = Get-Counter -Counter $counters -SampleInterval 1 -MaxSamples $Samples -ErrorAction SilentlyContinue

$cpu = @($s.CounterSamples | Where-Object { $_.Path -like "*processor time*" } | ForEach-Object { $_.CookedValue / $cores }) | Sort-Object
$mem = @($s.CounterSamples | Where-Object { $_.Path -like "*working set*" } | ForEach-Object { $_.CookedValue / 1MB }) | Sort-Object

# GPU 内存。核显上这部分从系统内存出，和私有工作集分不干净，两项都要记。
$gpu = @()
$gpuSet = Get-Counter -ListSet "GPU Process Memory" -ErrorAction SilentlyContinue
if ($gpuSet) {
    $paths = $gpuSet.PathsWithInstances | Where-Object { $_ -like "*pid_$($p.Id)*" }
    if ($paths) {
        $gs = Get-Counter -Counter $paths -MaxSamples 1 -ErrorAction SilentlyContinue
        $gpu = $gs.CounterSamples | Where-Object { $_.CookedValue -gt 0 }
    }
}

function Pct([array]$a, [double]$q) {
    if ($a.Count -eq 0) { return 0 }
    $i = [int][math]::Floor($a.Count * $q)
    if ($i -ge $a.Count) { $i = $a.Count - 1 }
    return $a[$i]
}

Write-Output "--- 结果 ---"
Write-Output ("CPU 中位     {0:N3} %" -f (Pct $cpu 0.5))
Write-Output ("CPU P95      {0:N3} %" -f (Pct $cpu 0.95))
Write-Output ("RSS 私有中位 {0:N1} MB" -f (Pct $mem 0.5))
Write-Output ("RSS 私有 P95 {0:N1} MB" -f (Pct $mem 0.95))
if ($gpu.Count -gt 0) {
    foreach ($g in $gpu) {
        $name = $g.Path.Substring($g.Path.LastIndexOf("\") + 1)
        Write-Output ("GPU {0,-18} {1:N1} MB" -f $name, ($g.CookedValue / 1MB))
    }
} else {
    Write-Output "GPU 内存: 取不到该进程的计数器实例"
}

Write-Output ""
Write-Output "记录行（复制到你的实测记录里）："
$date = Get-Date -Format "yyyy-MM-dd"
$os = (Get-CimInstance Win32_OperatingSystem).BuildNumber
Write-Output ("| $date | Debug | $State | {0:N3}% | {1:N3}% | {2:N1} MB | 见上 | ${Samples}s | Win11 b$os / Arc 核显 | 待判 |" -f (Pct $cpu 0.5), (Pct $cpu 0.95), (Pct $mem 0.5))

$p.WaitForExit(600000) | Out-Null
Write-Output ""
Write-Output "进程已退出"
