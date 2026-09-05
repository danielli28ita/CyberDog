# CyberDog

中文在前，English below.

**下载 / Download：[最新版 exe（Releases）](https://github.com/danielli28ita/CyberDog/releases/latest)** —— 单个文件，双击即用，Windows 10/11，不用安装。

<p align="center">
<img src="screenshots/showcase.png" width="560" alt="CyberDog：属性面板和站在任务栏上的比格" />
</p>

一条住在 Windows 任务栏上的 3D 比格犬。它站在任务栏上，后腿被任务栏挡住、前腿踩在任务栏前面，像趴在栏杆上。默认待在屏幕右下角，不打扰你干活。

## 1.1 有什么新的

- 最大化普通窗口时不再把狗藏掉；只有独占全屏 / 投影才会隐藏。
- 可以把狗拖到扩展显示器，位置会记住。
- 启动优先读 exe 旁边的存档；没有存档时可选新建或从别的文件夹导入。

（旧版 [1.0](https://github.com/danielli28ita/CyberDog/releases/tag/v1.0) 仍可下载，不会被删掉。）

## 它会做什么

- **自己过日子**：发呆、伸懒腰、抖毛、坐下、卖萌、闲逛、玩网球、扑光标、打翻食盆、冲向屏幕、趴下睡觉（睡觉时跳上垫子，打轻轻的呼噜）。
- **有性格**：每条狗领养时随机生成一套性格，动作偏好不一样；亲密度和它当下的需求也会影响它选什么做。
- **有眼神**：光标靠近时盯着你看，平时东张西望、斜眼、眨眼。
- **会叫**：叫声、哼唧、喘气、闻东西都是真实录音；22:00–08:00 自动静音，捣蛋的声音每小时有上限。
- **不添乱**：没人理它时自动缩小到 55%；别的程序全屏时自动隐藏；锁屏时暂停；换显示器或改缩放会自己重启。
- **管你健康**：每 30 分钟提醒喝水、起身活动一次（两项错开），离开电脑时暂停。
- **记事**：托盘菜单里写备忘和时间，到点它跑到屏幕中间叫两声、举气泡提醒你；戳一下算看过。
- **报天气**：在托盘里填一个城市（支持全球任意城市，同名时优先中国和欧洲），启动时报一句当天天气。不填就完全不联网。
- **教你玩**：偶尔冒一条操作提示；很久没摸它，会提醒你摸头能加亲密度。

## 怎么和它互动

| 操作 | 效果 |
|---|---|
| 光标在它头上慢慢移动 | 摸头：冒红心，亲密度上升（每分钟最多 +3），它会乖 90 秒 |
| 光标在它头上快速来回 | 打它：亲密度下降（每分钟最多 −3），它缩起来哼唧 |
| 左键点一下 | 戳它：亲密度小幅上升；备忘提醒时点一下表示「看到了」 |
| 左键按住身体拖动 | 搬家：松手的位置就是它新的常驻位置；拖到任务栏附近会自动贴回去 |
| 右键点它 | 打开 / 关闭属性面板：名字、性格、亲密度、领养天数、各种计数 |
| 点或拖过它之后 60 秒内 | 它才会离开角落到处逛、玩球、闹腾；之后回到角落只做原地动作 |

托盘图标右键菜单：改名、声音开关、备忘录、天气城市、语言（中文 / English / Italiano）、打开数据目录、退出。

## 数据放在哪

启动时优先用 exe 旁边的 `CyberDog-data\`。没有 `cyberdog.ini`（或旧名 `jdog.ini`）时会问你：新建一条狗、从其他文件夹导入、或取消退出。程序目录不可写时，新建/导入会退到 `%LOCALAPPDATA%\CyberDog\`。里面主要是：

- `cyberdog.ini`：名字、性格、亲密度、设置、常驻位置、计数，不到 1 KB。
- `plugin.memo.txt`：备忘录，看过且过期一周的自动清掉。

没有日志文件。删掉整个文件夹再启动，等于重新选新建或导入。

## 自己编译

需要 Visual Studio 2022 和 Windows SDK。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

产物是 `build\bin\Release\CyberDog.exe`，单文件静态链接。C++20，Win32 + Direct3D 12 + DirectComposition。

代码分三层：`core\` 是与平台无关的狗本身（动作、性格、亲密度、手势、眼神、模型、存档、多语言），`overlay\` 是 Windows 上的窗口、渲染、托盘、音频，`plugins\` 是天气、备忘、健康提醒、操作提示四个插件。`tests\` 是核心层的回归用例。

## 许可证

MIT，见 `LICENSE`。天气数据来自 [Open-Meteo](https://open-meteo.com/)，地名来自 [OpenStreetMap Nominatim](https://nominatim.org/)。叫声录音来自 CC0 素材库，见 `overlay/res/sounds/CREDITS.txt`。

---

# CyberDog (English)

**Download: [latest exe (Releases)](https://github.com/danielli28ita/CyberDog/releases/latest)** — one file, double-click to run, Windows 10/11, nothing to install.

<p align="center"><img src="screenshots/showcase.png" width="560" alt="CyberDog: stats panel and the beagle on the taskbar" /></p>

A 3D beagle that lives on the Windows taskbar. Its hind legs are hidden behind the taskbar and its front paws rest on it, like a dog leaning on a railing. It stays in the bottom-right corner by default and keeps out of your way.

## What’s new in 1.1

- No longer hides when you only maximize a normal window; only exclusive fullscreen / presentation mode hides it.
- Drag it onto an extended monitor; the spot is remembered across restarts.
- Prefers save data next to the exe; if none is found, you can create a new dog or import from another folder.

(Older [1.0](https://github.com/danielli28ita/CyberDog/releases/tag/v1.0) stays available.)

## What it does

- **Lives its own life**: idles, stretches, shakes, sits, acts cute, wanders, plays with a tennis ball, pounces on the cursor, flips its bowl, charges the screen, lies down to sleep (on a cushion, with a quiet snore).
- **Has a personality**: each dog gets a random personality when adopted; affinity and current needs also shape what it chooses to do.
- **Looks around**: stares at the cursor when it is near, otherwise glances about, side-eyes and blinks.
- **Makes sounds**: barks, whimpers, panting and sniffing are real recordings; silent 22:00–08:00, mischief sounds are rate-limited.
- **Stays out of the way**: shrinks to 55% when ignored, hides under full-screen apps, pauses on lock screen, restarts itself when displays or scaling change.
- **Looks after you**: reminds you to drink water and stand up every 30 minutes (staggered), paused while you are away.
- **Keeps notes**: write a note and a time in the tray menu; when due it runs to the middle, barks twice and shows a bubble. Poke it to acknowledge.
- **Reports the weather**: set a city in the tray menu (any city worldwide; ties prefer China and Europe) and it announces today's weather at launch. No city, no network at all.
- **Teaches you**: occasional usage tips; if you have not petted it for a while it reminds you that petting raises affinity.

## How to interact

| Input | Effect |
|---|---|
| Move the cursor slowly over its head | Petting: hearts appear, affinity rises (at most +3 per minute), it behaves for 90 s |
| Move the cursor quickly back and forth over its head | Hitting: affinity drops (at most −3 per minute), it cowers and whimpers |
| Left-click | Poke: small affinity gain; during a note reminder it means "seen" |
| Hold the left button on its body and drag | Move house: the release point becomes its new home; near the taskbar it snaps back onto it |
| Right-click it | Toggle the stats panel: name, personality, affinity, days adopted, counters |
| Within 60 s after a click or drag | Only then will it leave the corner to wander, play ball or misbehave; afterwards it goes back and does in-place actions |

Tray icon menu: rename, sound on/off, notes, weather city, language (中文 / English / Italiano), open data folder, quit.

## Where the data lives

On launch it prefers `CyberDog-data\` next to the exe. If there is no `cyberdog.ini` (or legacy `jdog.ini`), it asks: create a new dog, import from another folder, or cancel. If the program folder is not writable, create/import falls back to `%LOCALAPPDATA%\CyberDog\`. Main files:

- `cyberdog.ini`: name, personality, affinity, settings, home position, counters. Under 1 KB.
- `plugin.memo.txt`: notes; seen notes older than a week are pruned.

No log files. Delete the folder and relaunch to choose create or import again.

## Build it yourself

Visual Studio 2022 and the Windows SDK.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output is `build\bin\Release\CyberDog.exe`, a single statically linked file. C++20, Win32 + Direct3D 12 + DirectComposition.

Three layers: `core\` is the platform-independent dog (actions, personality, affinity, gestures, gaze, model, save, i18n), `overlay\` is the Windows side (window, rendering, tray, audio), `plugins\` holds the weather, notes, health and tips plugins. `tests\` has the regression tests for the core layer.

## License

MIT, see `LICENSE`. Weather from [Open-Meteo](https://open-meteo.com/), place names from [OpenStreetMap Nominatim](https://nominatim.org/). Dog sounds are CC0 recordings, see `overlay/res/sounds/CREDITS.txt`.
