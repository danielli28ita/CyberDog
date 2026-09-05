# CyberDog

中文在前，English below.

一条住在 Windows 任务栏上的 3D 比格犬。C++20，Win32 + Direct3D 12 + DirectComposition，单个 exe，静态链接，不装东西。默认不联网；只有你在托盘里填了天气城市，它才会在启动时查一次天气。

它会自己闲逛、玩网球、卖萌、捣蛋、睡觉；每 30 分钟提醒你喝水和起身；可以记备忘；摸它、打它、拖它都有反应，亲密度会记住。界面支持中文 / English / Italiano。

---

## 目录

1. [构建与运行](#构建与运行)
2. [代码框架](#代码框架)
3. [一帧是怎么画出来的](#一帧是怎么画出来的)
4. [关键机制与踩过的坑](#关键机制与踩过的坑)
5. [行为与操作逻辑（对应代码）](#行为与操作逻辑对应代码)
6. [怎么改](#怎么改)
7. [调试环境变量与工具](#调试环境变量与工具)
8. [存档与数据目录](#存档与数据目录)
9. [许可证](#许可证)

## 构建与运行

需要 Visual Studio 2022（MSVC 14.4x）、随它安装的 CMake，以及 Windows SDK（着色器在构建期用 `fxc` 编译，运行时不依赖 `d3dcompiler_47.dll`）。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\bin\Release\pet_tests.exe
.\build\bin\Release\CyberDog.exe
```

Release 产物是静态链接 CRT 的单文件 `CyberDog.exe`（GUI 子系统），只依赖 Windows 10/11 自带的系统库。Debug 构建的可执行文件叫 `pet.exe`，带控制台，能看到运行日志。

## 代码框架

```
core/          纯 C++20，不依赖平台和引擎。桌宠和将来的游戏引擎项目都链接它
  personality  性格：一个种子展开成 8 个 [0,1] 的参数（外向、黏人、好奇、懒散、胆小、活泼、捣蛋、卖萌）
  proxy_mesh   代理体：用圆角长方体和球搭的比格，按部件（Part）分组，每个部件有枢轴
  proxy_pose   姿态：眼神系统写进部件姿态（头、耳、眼睑、眉）
  gaze         眼神：看光标、看观察者、斜眼、随机扫视、眨眼
  action       动作系统：ActionPlayer 播动作（状态机 + 每个动作一个 tick 函数），ActionSelector 按性格和需求打分自选
  action_catalog 动作目录表：一行一个动作（打分权重、代价、时长、是否离开原地）
  interaction  手势识别：戳、摸、打、拖，从鼠标位置和按键状态判
  bond         亲密度：容易涨难掉，软下限，打过摸过之后「乖」一段时间
  hearts       冒心的粒子
  sound_synth  程序合成的音效：一段声音 = 一份配方（几层音调 / 噪声）
  save         存档：key=value 文本
  i18n         三语文案表：tr(Str::X)
  plugin_api   插件契约：IPlugin / IHostServices（pet-action、save、net 三个服务）
overlay/       Win32 覆盖层，只在 Windows 编译
  win32_window 整个显示器大小的透明置顶窗口；点击穿透靠 SetWindowRgn
  d3d_context  D3D12 设备、交换链、DirectComposition 合成、alpha 回读
  mesh_renderer 画部件树；两遍（不透明 / 半透明）；逐部件裁剪矩形（任务栏当栏杆）
  desktop_metrics 任务栏、工作区、DPI、全屏检测
  idle_controller 空闲三级降级：停止呈现 → 释放交换链 → 释放设备
  bubble_window 气泡（分层窗口，GDI 画一次）
  stats_panel / rename_dialog / memo_dialog  三个窗口，自绘
  ui_style     统一配色、字体、按钮、菜单项的画法
  tray_icon    托盘图标与自绘菜单
  audio        XAudio2 播放合成音效；22:00–08:00 静音
  http         WinHTTP，后台线程取文本，主线程回调
  user_data    存档路径
  main_app.cpp 宿主主循环：把上面所有东西接起来
plugins/       只依赖 core 的插件契约
  health       喝水 / 久坐提醒（两个错开 15 分钟的计时器）
  memo         备忘录（解析时间、到点提醒、存文件）
  weather      天气（Nominatim 地理编码 → Open-Meteo 天气）
  tips         操作提示（久没摸头教摸头，其余轮换）
tests/         core 与插件的回归用例（一个 exe，65 项）
tools/         构建、测量、截图、探针脚本
```

依赖方向只有一个：`overlay` → `plugins` → `core`。`core` 里不许出现 `<windows.h>`、D3D 或引擎头文件，CMake 里也不链接它们。

### 数据流

```
鼠标 / 托盘 / 定时器
   │
   ▼
main_app 主循环（每帧或每次唤醒）
   ├─ interaction: 鼠标样本 → 手势事件（戳 / 摸 / 打 / 拖）
   ├─ bond: 手势 → 亲密度、乖
   ├─ 插件 on_tick(HostClock) → request_pet_action(动作 + 气泡)
   ├─ ActionSelector.choose() → ActionPlayer.start()
   ├─ ActionPlayer.tick(dt) → 狗的位置、姿态、待播音效
   ├─ gaze.update() → 头、眼、眉、眼睑的目标
   ├─ hearts.update()
   ├─ compute_part_world(): 部件姿态 → 每个部件的世界矩阵
   ├─ mesh_renderer.draw()：两遍绘制，逐部件裁剪
   ├─ d3d_context.end_frame()：Present1 带脏矩形；需要时把 alpha 拷回 CPU
   └─ rebuild_region()：alpha 掩码 → SetWindowRgn（点击穿透）
```

## 一帧是怎么画出来的

1. **窗口**是整个主显示器那么大的 `WS_POPUP` 窗口，`WS_EX_NOREDIRECTIONBITMAP | TOPMOST | TOOLWINDOW | NOACTIVATE`。画面由 DirectComposition 合成，交换链是预乘 alpha 的 BGRA。
2. **相机**固定（`kEye`、`kTarget`、`kFovY`），沿原方向拉远到能盖住整个显示器，再在投影矩阵第 3 行加镜头平移，把舞台原点摆到右下角。狗站在舞台平面 y=0 上，+x 是屏幕左，+z 朝观众。
3. **姿态**：每帧先清零所有部件姿态，`ActionPlayer::apply` 写身体、腿、尾、道具，`pose_from_gaze` 写头、耳、眼，`HeartFx::apply` 写心。`compute_part_world` 按父子关系乘出世界矩阵。部件矩阵是 T(-pivot)·S·R·T(pivot+offset)，顶点本来就在模型空间的绝对位置，所以不转不动时矩阵接近单位阵，**平移行不是位置**——要位置就把枢轴点变换过去。
4. **存在感**：没人互动时狗缩到 55%，缩放绕它自己的脚做（位置不变）。
5. **任务栏当栏杆**：部件枢轴的舞台 z 在巢的 z 之后（后腿、躯干、阴影）就套裁剪矩形（裁到看得见的任务栏顶边），之前的（前腿、头）不裁，画在任务栏上面。冲屏时整条狗都在前面。
6. **呈现**：`Present1` 带脏矩形（这一帧和上一帧画过的所有东西的并集），否则全屏合成会让整机卡顿。
7. **点击穿透**：`WM_NCHITTEST` 返回 `HTTRANSPARENT` 只在同一线程的窗口之间有效，对别的程序无效。所以每 33–66 ms 把画过的区域的 alpha 拷回 CPU，按 8 px 格子做掩码，膨胀一圈后 `SetWindowRgn`。区域外的点击落到下面的程序。**区域同时会裁画面**，所以回读范围必须覆盖画过的一切（狗、球、心、一滩）。
8. **空闲降级**：一段时间没互动就停止呈现（最后一帧留在合成器里，狗还在），更久释放交换链，再久释放设备。任何互动、提醒、微动作都会短暂唤醒。

## 关键机制与踩过的坑

| 机制 | 做法 | 为什么这么做 |
|---|---|---|
| 不抢焦点 | 窗口 `NOACTIVATE`，`WM_MOUSEACTIVATE` 回 `MA_NOACTIVATE`，永远不 `SetCapture`，松开靠 `GetAsyncKeyState` 轮询 | 一旦成为前台的全屏窗口，系统按全屏应用处理，用户操作不了别的 |
| 全屏应用时隐藏 | 自己判前台窗口：属于别的进程、不是桌面或任务栏、矩形盖满显示器 | `SHQueryUserNotificationState` 的 `QUNS_BUSY` 会把我们自己的全屏覆盖层当成全屏应用，一秒一闪 |
| 任务栏顶边 | 窗口矩形顶边再往下 7 逻辑像素 | Win11 的任务栏窗口比看得见的高一条，按矩形裁会留一条空隙 |
| 手势 | 不按键、光标在头上：慢 = 摸，快速来回 = 打；按住身体拖 = 拖；点一下 = 戳 | 进头部区域的第一帧不算位移，跨屏的一大步不会误判成打 |
| 待在角落 | 巢在舞台右端；没人按过它之前不许自选离开原地的动作；按过之后 60 秒内会撒欢 | 狗站到屏幕中间会挡事 |
| 任意拖放 | 横向改舞台 x，纵向改地面抬高 `groundY`；松手处即巢并存档；离任务栏 40 逻辑像素内吸附回任务栏 | 用户要自己决定它待在哪 |
| 睡觉不挡时钟 | 趴睡时按「未抬之前的底边」算需要抬多少，平滑抬到任务栏之上；不累积 | 累积版会把狗抬出屏幕 |
| 圆角模型 | `add_rounded_box`：内缩长方体 ⊕ 球，经纬网参数化 | 纯方块太多直角 |
| 天气地名 | 先问 OSM Nominatim（`accept-language` 跟界面语言走），同名地方优先中国、其次欧洲、再按重要度；失败退回 Open-Meteo 的地理编码 | Open-Meteo 按中文搜「罗马」给了澳大利亚的 Roma |
| 天气解析 | 先切出 `current` / `daily` 对象再找键 | 真实响应里 `current_units` 排在前面，扁平查找会撞上单位字符串 |
| 音效 | 配方表 + 半余弦包络 | 直线包络的拐角听起来像「咔」 |
| 静音时段 | 22:00–08:00 不出声，没有开关 | 项目约束 |

## 行为与操作逻辑（对应代码）

狗做什么、你能对它做什么、每一条由哪段代码实现。改行为先找这张表。

| 行为 / 操作 | 逻辑 | 代码 |
|---|---|---|
| 出场 | 启动后从任务栏后面钻出来、抖一下、叫一声，3 秒后自我介绍，没起过名再提醒改名 | `ActionKind::Entrance`，`tick_entrance()`（`core/src/action.cpp`）；介绍与命名提醒在 `main_app.cpp` 的 `introStage` |
| 待在右下角 | 巢在舞台右端；没人按过它之前只做原地动作（发呆、伸懒腰、抖身、坐、卖萌、睡）；点或拖过它之后 60 秒内才会闲逛、玩球、扑光标、打翻碗、冲屏 | `ActionSelector::set_roam_allowed`，`action_roams()`（`core/src/action_catalog.cpp`）；60 秒窗口 `kRoamAfterInteractionMs`（`main_app.cpp`） |
| 自选动作 | 每个动作按性格 8 项 × 权重 + 需求 4 项 × 权重 + 常数打分，光标在不在、乖不乖、亲密度做修正，随机抽高分的；做完更新需求 | `ActionSelector::score / choose`（`core/src/action.cpp`）；表在 `action_catalog.cpp` |
| 缩小 | 没互动时缩到 55%，绕自己的脚缩；按下、拖、提醒、气泡期间恢复原大 | `presence` 与 `viewProjEff`（`main_app.cpp`） |
| 回巢 / 睡觉 | 空闲阈值到了：不在巢就走回去，再按懒散度决定趴下睡（睡时抬到任务栏之上、垫子出现、打呼） | `ActionKind::ReturnHome / Sleep`，`tick_sleep()`；`IdleController`；`sleepLift`、`matScale`（`main_app.cpp`） |
| 摸 | 不按键、光标在头上慢慢移动：每帧一个 PetTick；亲密度 +1.5/秒但一分钟封顶 3 点；冒心 + 红字「好感度 +1」；乖 90 秒 | `GestureTracker`（`core/src/interaction.cpp`），`Bond::apply(Pet)`（`core/src/bond.cpp`，`kPetPerMinute`），`HeartFx`，`fxTag` |
| 打 | 光标在头上快速来回：一次 Hit；一分钟内最多掉 3 点，软下限 10；狗缩起来哼唧 | `GestureTracker`（`kHitSpeed`、反转计数），`Bond::apply(Hit)`，`ActionKind::Cower` |
| 戳 | 按下不动松开：Poke，+0.5；备忘提醒期间戳一下 = 看到了 | `Gesture::Poke`；`memo.acknowledge` |
| 拖 | 按住身体移动：横向改舞台 x，纵向抬高地面；松手处成为巢并存档，离任务栏 40 逻辑像素内吸附回去 | `Gesture::Drag*`；`groundY`、`home_x / home_y`，`ActionPlayer::set_home`（`main_app.cpp`） |
| 右键 | 取消当前互动，开 / 关属性面板 | `cancelRequested`，`StatsPanel`（`overlay/src/stats_panel.cpp`），行由 `open_stats_panel` 整理 |
| 眼神 | 光标在附近就盯着看；否则随机扫视（六成抬眼）、斜眼、看你、眨眼 | `GazeController`（`core/src/gaze.cpp`），`pose_from_gaze` |
| 喝水 / 久坐提醒 | 两个计时器错开 15 分钟，各 30 分钟一次；离开电脑超过 5 分钟暂停；喝水 = 轻叫→字幕→尿尿，久坐 = 大跳→字幕→踢球 | `plugins/health`，`ActionKind::RemindWater / RemindStand` |
| 备忘 | 托盘窗口写内容和时间；到点跑到中间坐下叫两声、气泡 20 秒；已看过且过期一周自动清 | `plugins/memo`（`parse_due`），`MemoDialog`，`ActionKind::RemindMemo` |
| 天气 | 填了城市才联网：Nominatim 地理编码（同名优先中国、欧洲）→ Open-Meteo 天气，启动 9 秒后报一句 | `plugins/weather`（`pick_place`、`compose_weather_line`），`overlay/src/http.cpp` |
| 操作提示 | 启动 3 分钟后第一条，之后每 20–35 分钟一条；20 分钟没摸头优先教摸头，否则轮换 6 条 | `plugins/tips`，`HostClock::sinceLastPetSeconds` |
| 声音 | 五段真实录音（叫、轻叫、哼唧、喘气、闻）内嵌 exe，其余合成；22:00–08:00 静音；捣蛋音效每小时最多 6 次 | `overlay/src/audio.cpp`（`load_wav_resource`），`core/src/sound_synth.cpp`，`overlay/res/sounds/` |
| 全屏 / 锁屏 / 换显示器 | 别的程序全屏时隐藏并释放显卡资源；锁屏暂停；显示器或 DPI 变了重启自己 | `fullscreen_or_presenting()`（`desktop_metrics.cpp`），`SessionEvent`（`win32_window.cpp`） |
| 存档 | 名字、种子、亲密度、开关、语言、巢的位置、计数；30 秒一存，退出必存 | `core/src/save.cpp`，`flush_save`（`main_app.cpp`），`user_data.cpp` |

## 怎么改

**加一个动作**
1. `core/include/core/action.h` 的 `ActionKind` 末尾加名字（顺序不能动，插件按下标引用）。
2. `core/src/action_catalog.cpp` 加一行：打分权重（8 个性格 + 4 个需求）、常数、光标加成、是否要光标、乖着时禁不禁、亲密度系数、做完之后需求怎么变。会离开原地的动作还要加进 `action_roams`。
3. `core/src/action.cpp`：`start()` 里定时长和初值，写一个 `tick_xxx(dt)`，在 `tick()` 的 switch 里接上。姿态通过 `bodyLift_`、`legRot_`、`legScaleY_`、`headPitch_` 等成员表达，`apply()` 统一写进部件。
4. `core/src/i18n.cpp` 的动作名表加一行（三种语言）。
5. 想看效果：`PET_ACTION=<下标>` 启动就播这个动作。

**加一段音效**：`core/include/core/sound_synth.h` 的 `SoundId` 末尾加名字，`core/src/sound_synth.cpp` 的 `kRecipes` 加一份配方（`tone` / `noise` 各层：起点、长度、频率、增益、谐波数、起音、收音、颤音）。动作里 `pendingSound_ = snd::Xxx` 就会播。

**换一个角色**：写一份 `CharacterProfile`（基线性格、偏移幅度、不变量），换掉 `personality_from_seed` 用的那份；代理体在 `proxy_mesh.cpp` 的 `build_proxy_beagle`，部件枚举在 `proxy_mesh.h`。

**加一句话 / 加一种语言**：`core/include/core/i18n.h` 的 `Str` 加名字，`core/src/i18n.cpp` 的 `kTable` 加一行；加语言就给 `Entry` 加一列、`Lang` 加一项、`pick()` 加一个 case，托盘子菜单会自动多一项。

**加一个插件**：实现 `IPlugin`（`id / on_load / on_tick / on_unload`），需要狗做事就 `host.request_pet_action`，需要存东西用 `read/write_plugin_data`，需要联网用 `fetch_text`（回调在主线程）。在 `plugins/` 下建目录、`CMakeLists.txt` 加进去，`main_app.cpp` 里创建并 `on_load`。看 `plugins/health` 最简单。

**改配色和窗口样式**：`overlay/include/overlay/ui_style.h` 的颜色常量；三个窗口和托盘菜单都从这里取。

**改狗的位置 / 大小**：`main_app.cpp` 顶部的 `kEye / kTarget / kFovY`、`kSmallPresence`；巢的位置在 `player.set_home(stage_min() + 0.25, 0)`。

**改提醒间隔**：托盘没有界面，存档里 `health_minutes`，或环境变量 `PET_HEALTH_MINUTES`。

## 调试环境变量与工具

| 变量 | 作用 |
|---|---|
| `PET_NO_ENTRANCE=1` | 不播出场 |
| `PET_ACTION=n` | 启动 1 秒后播第 n 个动作 |
| `PET_OPEN=stats\|memo\|rename\|weather\|menu` | 启动 1.5 秒后打开对应窗口 / 托盘菜单（截图用） |
| `PET_DEBUG_HEARTS=1` | 每 0.6 秒冒一颗心 |
| `PET_DEBUG_MASK=1` | 打印状态切换、命中掩码 |
| `PET_IGNORE_CURSOR=1` | 光标不算互动（测空闲档） |
| `PET_IGNORE_IDLE=1` | 系统空闲不暂停提醒 |
| `PET_HEALTH_MINUTES=n` | 提醒间隔 |
| `PET_ALLOW_MULTI=1` | 允许第二个实例 |

命令行参数（测试用）：`CyberDog.exe <多少秒后退出> <空闲阈值秒> [多少秒后自动隐藏]`。

`tools/` 里的脚本都是 PowerShell 5.1 能跑的 ASCII 文件：`measure.ps1`（CPU / 内存 / 显存采样）、`capture.ps1`（截屏）、`probe_input.ps1`（点击后查 `WindowFromPoint`、捕获、前台）、`region_probe.ps1`（覆盖层可见性与区域包围盒）、`quns.ps1`（系统通知状态与前台窗口）、`pet_sim.ps1`（模拟摸 / 打）、`right_click.ps1`、`drag_sim.ps1`（模拟拖动）、`taskbar_rect.ps1`、`pixel_column.ps1`（逐行采样颜色）、`check_ctrl.py`（控制字符扫描）、`export_public.py`（白名单导出）。

## 存档与数据目录

数据放在程序旁边的 `CyberDog-data\`（便携；程序目录不可写时退回 `%LOCALAPPDATA%\Jdog\`，第一次切换会把老文件搬过来）。托盘菜单「打开数据目录」直接打开。

- `cyberdog.ini`：名字、性格种子、亲密度、声音、命名提醒、天气城市、语言、领养日、基础位置（`home_x` / `home_y`）、启动 / 摸 / 打 / 玩球次数。不到 1 KB。
- `plugin.memo.txt`：备忘录，一行一条；已看过且过期 7 天的自动清掉。

没有日志文件。删掉整个目录就是一条新狗。

## 许可证

MIT，见 `LICENSE`。天气数据来自 [Open-Meteo](https://open-meteo.com/)，地名来自 [OpenStreetMap Nominatim](https://nominatim.org/)（ODbL，请遵守其使用政策：每次启动最多一次请求，UA 写明用途）。

---

# CyberDog (English)

A 3D beagle that lives on the Windows taskbar. C++20, Win32 + Direct3D 12 + DirectComposition, a single statically linked exe, nothing to install. Offline by default; it only fetches the weather once per launch if you set a city in the tray menu.

It wanders, plays with a tennis ball, acts cute, misbehaves, sleeps; reminds you to drink water and stand up every 30 minutes; keeps notes; reacts to petting, hitting and dragging, and remembers how much it likes you. UI in Chinese / English / Italian.

## Build and run

Visual Studio 2022 (MSVC 14.4x), the CMake that ships with it, and the Windows SDK (shaders are compiled at build time with `fxc`, so there is no runtime dependency on `d3dcompiler_47.dll`).

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\bin\Release\pet_tests.exe
.\build\bin\Release\CyberDog.exe
```

The Release artifact is a single `CyberDog.exe` (GUI subsystem, static CRT) that only needs the system libraries of Windows 10/11. The Debug build is called `pet.exe` and has a console with logs.

## Code layout

```
core/          pure C++20, no platform or engine headers. Linked by the pet and, later, by a game-engine project
  personality  one seed → eight [0,1] traits (extroversion, clinginess, curiosity, laziness, timidity, liveliness, mischief, charm)
  proxy_mesh   the proxy body: rounded boxes and spheres grouped into parts, each with a pivot
  proxy_pose   gaze → head / ear / eyelid / brow poses
  gaze         look at the cursor, look at the viewer, side-eye, wander, blink
  action       ActionPlayer (state machine, one tick function per action) and ActionSelector (scores actions by personality and needs)
  action_catalog one row per action: score weights, costs, duration, whether it leaves the spot
  interaction  gestures from cursor position and button state: poke, pet, hit, drag
  bond         affinity: easy up, hard down, soft floor, "behaving" after pet / hit
  hearts       heart particles
  sound_synth  procedural sounds: one recipe (tone / noise layers) per sound
  save         key=value save file
  i18n         three-language string table: tr(Str::X)
  plugin_api   IPlugin / IHostServices (pet-action, save, net)
overlay/       Win32 layer, Windows only
  win32_window monitor-sized transparent topmost window; click-through via SetWindowRgn
  d3d_context  D3D12 device, swap chain, DirectComposition, alpha readback
  mesh_renderer part-tree drawing, opaque + blended passes, per-part scissor (taskbar as a railing)
  desktop_metrics taskbar, work area, DPI, full-screen detection
  idle_controller three-stage idle: stop presenting → release swap chain → release device
  bubble_window speech bubble (layered window, drawn once with GDI)
  stats_panel / rename_dialog / memo_dialog  owner-drawn windows
  ui_style     shared colours, fonts, button and menu drawing
  tray_icon    tray icon and owner-drawn menu
  audio        XAudio2 playback; silent 22:00–08:00
  http         WinHTTP on a worker thread, callbacks on the main thread
  user_data    save path
  main_app.cpp host main loop that wires everything together
plugins/       depend only on the core plugin contract
  health       drink / stand reminders (two timers 15 minutes apart)
  memo         notes (time parsing, due reminders, file storage)
  weather      Nominatim geocoding → Open-Meteo forecast
  tips         usage tips (teaches petting when neglected, rotates the rest)
tests/         regression tests for core and plugins (one exe, 65 checks)
tools/         build, measurement, screenshot and probe scripts
```

Dependencies point one way: `overlay` → `plugins` → `core`. `core` must not include `<windows.h>`, D3D or engine headers.

### Data flow

```
mouse / tray / timers
   │
   ▼
main_app loop (every frame or wake-up)
   ├─ interaction: pointer samples → gesture events
   ├─ bond: gestures → affinity, "behaving"
   ├─ plugins on_tick(HostClock) → request_pet_action(action + bubble)
   ├─ ActionSelector.choose() → ActionPlayer.start()
   ├─ ActionPlayer.tick(dt) → position, pose, pending sound
   ├─ gaze.update() → head / eye / brow / lid targets
   ├─ hearts.update()
   ├─ compute_part_world(): part poses → world matrices
   ├─ mesh_renderer.draw(): two passes, per-part scissor
   ├─ d3d_context.end_frame(): Present1 with dirty rects; alpha readback when needed
   └─ rebuild_region(): alpha mask → SetWindowRgn (click-through)
```

## How a frame is made

1. **Window**: a `WS_POPUP` window the size of the primary monitor, `WS_EX_NOREDIRECTIONBITMAP | TOPMOST | TOOLWINDOW | NOACTIVATE`, composed by DirectComposition with a premultiplied BGRA swap chain.
2. **Camera**: fixed (`kEye`, `kTarget`, `kFovY`), pulled back to cover the monitor, with a lens shift in the projection's third row that places the stage origin at the bottom-right. The dog stands on y=0; +x is screen left, +z is towards the viewer.
3. **Pose**: all part poses are cleared each frame; `ActionPlayer::apply` writes body, legs, tail and props, `pose_from_gaze` writes head, ears and eyes, `HeartFx::apply` writes hearts. `compute_part_world` multiplies parent chains. A part matrix is T(-pivot)·S·R·T(pivot+offset) and vertices are already at absolute model positions, so an unrotated part's matrix is close to identity: **the translation row is not the position**; transform the pivot point instead.
4. **Presence**: without interaction the dog shrinks to 55%, scaled about its own feet.
5. **Taskbar as a railing**: parts whose pivot z is behind the home z (hind legs, torso, shadow) are drawn with a scissor at the visible taskbar top; parts in front (front legs, head) are drawn over the taskbar. When it charges at the screen the whole dog is in front.
6. **Present**: `Present1` with a dirty rect (union of everything drawn this and last frame); full-screen composition every frame would stall the whole machine.
7. **Click-through**: returning `HTTRANSPARENT` from `WM_NCHITTEST` only works between windows of the same thread. So every 33–66 ms the alpha of the drawn area is read back, turned into an 8 px cell mask, dilated and applied with `SetWindowRgn`. Clicks outside the region go to whatever is below. **The region also clips the picture**, so the readback must cover everything drawn (dog, ball, hearts, puddle).
8. **Idle**: after a while without interaction presenting stops (the last frame stays composed, the dog is still visible), later the swap chain and then the device are released. Interaction, reminders and micro-actions wake it briefly.

## Key mechanisms and lessons

| Mechanism | Approach | Why |
|---|---|---|
| Never steal focus | `NOACTIVATE`, `MA_NOACTIVATE`, never `SetCapture`, release detected by polling `GetAsyncKeyState` | A foreground full-screen window is treated as a full-screen app and locks the user out |
| Hide under full-screen apps | Own check: foreground window of another process, not the desktop or taskbar, covering its monitor | `SHQueryUserNotificationState` reports our own overlay as a full-screen app and made the dog blink |
| Taskbar top edge | Window rect top + 7 logical px | On Windows 11 the taskbar window is taller than the visible bar; clipping at the rect left a gap |
| Gestures | No button: slow over the head = pet, fast back-and-forth = hit; press on the body and move = drag; click = poke | The first sample after entering the head area carries no displacement, so a jump across the screen is never a hit |
| Stay in the corner | Home at the right end of the stage; roaming actions only within 60 s after a click or drag | A dog in the middle of the screen gets in the way |
| Drag anywhere | Horizontal drag moves the stage x, vertical drag raises the ground (`groundY`); the release point becomes home and is saved; within 40 logical px of the taskbar it snaps back onto it | The user decides where it lives |
| Sleep above the clock | While lying down, the needed lift is computed from the unlifted bottom edge and smoothed, never accumulated | The accumulating version lifted the dog off-screen |
| Rounded body | `add_rounded_box`: inset box ⊕ sphere, parametrised on a lat/long grid | Plain boxes had too many right angles |
| Place names | OSM Nominatim first (`accept-language` follows the UI language), prefer China, then Europe, then importance; fall back to Open-Meteo geocoding | Open-Meteo returned Roma, Australia for "罗马" |
| Weather parsing | Extract the `current` / `daily` objects before looking up keys | The real response lists `current_units` first and a flat search hits the unit strings |
| Sounds | Recipe table, half-cosine envelopes | Linear envelope corners click |
| Quiet hours | Silent 22:00–08:00, no switch | Project constraint |

## Behaviours and interaction logic (with the code that implements them)

| Behaviour / input | Logic | Code |
|---|---|---|
| Entrance | Climbs out from behind the taskbar, shakes, barks; introduces itself after 3 s and reminds you to name it | `ActionKind::Entrance`, `tick_entrance()` (`core/src/action.cpp`); `introStage` in `main_app.cpp` |
| Stays in the corner | Home is at the right end of the stage; until you click or drag it, only in-place actions (idle, stretch, shake, sit, charm, sleep); for 60 s after a click/drag it may wander, play ball, pounce, flip the bowl, charge | `ActionSelector::set_roam_allowed`, `action_roams()` (`core/src/action_catalog.cpp`); `kRoamAfterInteractionMs` (`main_app.cpp`) |
| Picks actions | Each action is scored: personality × weights + needs × weights + constant, adjusted by cursor presence, "behaving" and affinity; a high scorer is drawn at random; needs update afterwards | `ActionSelector::score / choose` (`core/src/action.cpp`); table in `action_catalog.cpp` |
| Shrinks | 55% when nobody interacts, scaled about its own feet; full size while pressed, dragged, reminding or speaking | `presence`, `viewProjEff` (`main_app.cpp`) |
| Goes home / sleeps | After the idle threshold it walks home, then may lie down depending on laziness (lifted above the taskbar, cushion appears, snores) | `ActionKind::ReturnHome / Sleep`, `tick_sleep()`; `IdleController`; `sleepLift`, `matScale` |
| Petting | No button, slow movement over the head: one PetTick per frame; +1.5/s but capped at 3 per minute; hearts and a red "Affinity +1" tag; behaves for 90 s | `GestureTracker` (`core/src/interaction.cpp`), `Bond::apply(Pet)` (`kPetPerMinute`), `HeartFx`, `fxTag` |
| Hitting | Fast back-and-forth over the head: one Hit; at most −3 per minute, floor 10; the dog cowers and whimpers | `GestureTracker` (`kHitSpeed`, reversal count), `Bond::apply(Hit)`, `ActionKind::Cower` |
| Poke | Press and release without moving: +0.5; during a note reminder it means "seen" | `Gesture::Poke`; `memo.acknowledge` |
| Drag | Press on the body and move: horizontal changes stage x, vertical raises the ground; the release point becomes home and is saved; snaps to the taskbar within 40 logical px | `Gesture::Drag*`; `groundY`, `home_x / home_y`, `ActionPlayer::set_home` |
| Right-click | Cancels the current interaction and toggles the stats panel | `cancelRequested`, `StatsPanel`, `open_stats_panel` |
| Gaze | Follows the cursor when near; otherwise wanders (60% upward glances), side-eyes, looks at you, blinks | `GazeController` (`core/src/gaze.cpp`), `pose_from_gaze` |
| Drink / stand reminders | Two timers 15 min apart, every 30 min; paused when you are away for 5 min; water = soft bark → bubble → pee, stand = big jump → bubble → kick | `plugins/health`, `ActionKind::RemindWater / RemindStand` |
| Notes | Tray window with text and time; when due it runs to the middle, sits, barks twice, bubble for 20 s; seen notes older than 7 days are pruned | `plugins/memo` (`parse_due`), `MemoDialog`, `ActionKind::RemindMemo` |
| Weather | Only with a city set: Nominatim geocoding (China, then Europe preferred) → Open-Meteo, announced 9 s after launch | `plugins/weather` (`pick_place`, `compose_weather_line`), `overlay/src/http.cpp` |
| Tips | First tip 3 min after launch, then every 20–35 min; if not petted for 20 min it teaches petting, otherwise rotates six tips | `plugins/tips`, `HostClock::sinceLastPetSeconds` |
| Sound | Five real recordings (bark, soft bark, whimper, pant, sniff) embedded in the exe, the rest synthesized; silent 22:00–08:00; mischief sounds at most 6 per hour | `overlay/src/audio.cpp` (`load_wav_resource`), `core/src/sound_synth.cpp`, `overlay/res/sounds/` |
| Full screen / lock / display change | Hides and releases GPU resources under another app's full-screen window; pauses when locked; restarts itself on display or DPI change | `fullscreen_or_presenting()` (`desktop_metrics.cpp`), `SessionEvent` (`win32_window.cpp`) |
| Save | Name, seed, affinity, switches, language, home, counters; written every 30 s and on exit | `core/src/save.cpp`, `flush_save` (`main_app.cpp`), `user_data.cpp` |

## How to change things

**Add an action**: append to `ActionKind` (order matters, plugins reference by index); add a row to `action_catalog.cpp` (and to `action_roams` if it moves the dog); add duration and a `tick_xxx` in `action.cpp` and hook it into `tick()`; add the action name to the table in `i18n.cpp`. Try it with `PET_ACTION=<index>`.

**Add a sound**: append to `SoundId`, add a recipe to `kRecipes` in `sound_synth.cpp`, set `pendingSound_` in an action.

**Change the character**: write a `CharacterProfile` and use it in `personality_from_seed`; the body is `build_proxy_beagle` in `proxy_mesh.cpp`.

**Add a string or a language**: add a `Str` and a `kTable` row in `i18n`; for a language add a column to `Entry`, a `Lang` value and a case in `pick()`. The tray submenu picks it up.

**Add a plugin**: implement `IPlugin`; use `request_pet_action`, `read/write_plugin_data`, `fetch_text` (callback on the main thread); add the directory to CMake and create it in `main_app.cpp`. `plugins/health` is the smallest example.

**Colours and window style**: `overlay/include/overlay/ui_style.h`.

**Dog position and size**: `kEye / kTarget / kFovY`, `kSmallPresence` at the top of `main_app.cpp`; the home is `player.set_home(stage_min() + 0.25, 0)`.

## Debug environment variables and tools

`PET_NO_ENTRANCE=1`, `PET_ACTION=n`, `PET_OPEN=stats|memo|rename|weather|menu`, `PET_DEBUG_HEARTS=1`, `PET_DEBUG_MASK=1`, `PET_IGNORE_CURSOR=1`, `PET_IGNORE_IDLE=1`, `PET_HEALTH_MINUTES=n`, `PET_ALLOW_MULTI=1`. Command line: `CyberDog.exe <exit after seconds> <idle threshold seconds> [auto-hide after seconds]`.

Scripts in `tools/` are ASCII PowerShell 5.1: `measure.ps1` (CPU / RAM / GPU sampling), `capture.ps1`, `probe_input.ps1`, `region_probe.ps1`, `quns.ps1`, `pet_sim.ps1`, `right_click.ps1`, `drag_sim.ps1`, `taskbar_rect.ps1`, `pixel_column.ps1`, plus `check_ctrl.py` and `export_public.py`.

## Save file and data folder

Data lives in `CyberDog-data\` next to the exe (portable; if the program folder is not writable it falls back to `%LOCALAPPDATA%\Jdog\`, migrating old files once). "Open data folder" in the tray menu opens it.

- `cyberdog.ini`: name, personality seed, affinity, sound, name reminder, weather city, language, adoption date, home position (`home_x` / `home_y`), launch / pet / hit / ball counters. Under 1 KB.
- `plugin.memo.txt`: notes, one per line; seen notes older than 7 days are pruned automatically.

No log files. Delete the folder for a new dog.

## License

MIT, see `LICENSE`. Weather from [Open-Meteo](https://open-meteo.com/), place names from [OpenStreetMap Nominatim](https://nominatim.org/) (ODbL; respect its usage policy: at most one request per launch, with a descriptive User-Agent).
