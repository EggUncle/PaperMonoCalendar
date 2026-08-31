# 界面效果与配图说明

以下均为**离线软件预览，不是设备实拍**，也不是 BLE 连接、RTC 校时或刷新质量的验收证据。
日期固定为虚构的 2028/02/18，时间为 14:32，电量为 63%；活动按固定合成表生成，不读取任何真实记录。

## 已同步示意

![已同步软件预览，全部数据为虚构](images/calendar-synced.png)

- 左侧是完整月历，日期数字统一在格子右上角。
- 1、2、3、4 日依次演示四档 Bayer 黑白点阵密度；高档日期数字反白。
- 18 日为“今天”：即使合成活动等级为 4，也省略底纹，只保留匹配格子形状的矩形框。
- 右侧示例为 13 个活跃日、当前连续 3 天、最长连续 4 天；这些数值与合成日期表一致，不是用户统计。
- 顶部示意 `HUB / SYNCED`、电池图标和百分比；右侧日期是 `YYYY / MM / DD`。

## 等待 Hub 示意

![等待 Hub 的软件预览，全部数据为虚构](images/calendar-waiting.png)

顶部改为 `HUB / WAITING`，活动未加载时没有底纹、统计显示零；RTC 页面仍显示。
WAITING 不代表已完成设备启动检查，SYNCED 也不证明是 v3 校时包。真实状态解释见[排障](troubleshooting.md)。

## 来源与保真范围

生成器直接从 `PaperMonoCalendar.ino` 抽取并在主机上运行 `drawCalendar`、`drawBattery`、日期映射、活动点阵和像素数字绘制等纯 UI 函数，不另写一套独立页面坐标。

- 5×7 数字字形和 Bayer 点阵来自当前固件源码。
- Font0 和 FreeSans24pt7b 字体读取自已安装的 M5GFX 0.2.28；字体原始头文件不复制进本仓库，来源声明见 [NOTICE](images/NOTICE.md)。
- 主机使用最小的黑白画布适配器替代 M5Canvas。整数矩形和点阵可复现；字形间距/基线实现不代表完整 M5GFX 排版引擎，因此不声称与真机逐像素一致。
- 上方 800×480 对应屏幕内容；底部额外 40px 是“软件预览／虚构数据／非实拍”说明，不是固件新增状态栏。
- 不模拟墨水屏光照、残影、刷新闪动、灰阶波形、控制器休眠、实际电量或 RTC 精度。

不使用含真实活动数据的旧设计截图，不需要 API Key，不连接 Hub、Codex 或设备。

## 复现与校验

需要 Python 3 标准库、C++17 编译器（默认 `c++`，可用 `CXX` 指定），以及通过 `sh tools/setup.sh` 安装的 M5GFX 0.2.28。

```sh
# 查看 Arduino 用户库目录，替换下面的示例路径。
arduino-cli config get directories.user
python3 tools/render_previews.py --font-dir /path/to/Arduino/libraries/M5GFX/src/lgfx/Fonts

# 不覆盖图片，只确认已提交图片的像素与当前源码一致。
python3 tools/render_previews.py --font-dir /path/to/Arduino/libraries/M5GFX/src/lgfx/Fonts --check
python3 -m unittest discover -s tests -v
```

脚本在临时目录编译主机适配器，完成后自动清理；仅写入 `docs/images/calendar-synced.png` 和 `calendar-waiting.png`，不写设备。
PNG 只包含 IHDR、IDAT、IEND，不包含 EXIF、GPS、账号、路径或文本元数据。GitHub Actions 会在固定依赖安装后重新渲染并检查像素是否匹配。
