# esp_http_stream

基于 ESP-IDF 的 ESP32-S3 图像终端实验工程。当前仓库已经从早期的“纯网页 MJPEG 图传”演进为一个包含 LCD 显示、LVGL 触摸 UI、拍照上传、AI 分析和本地目标检测的综合示例，同时保留了 AP 配网、HTTP 视频流和 mDNS 等网络模块。

## 项目概览

当前代码库里同时存在两条能力线：

1. 默认运行路径
   `main/main.c` 当前默认启动的是本地屏幕交互流程：连接 Wi-Fi、初始化 I2C / PCA9557 / LCD / LVGL / 摄像头 / 检测器，然后进入触摸 UI。
2. 保留的网络功能
   `main/config_server.c`、`main/http_server.c`、`main/mdns_service.c` 仍然在仓库中，支持 AP 配网、MJPEG 视频流和 mDNS 发现，但这部分启动代码目前在 `main/main.c` 中被注释掉了，不会默认启用。

如果你是第一次看这个工程，建议先把它理解成“带屏幕和触摸交互的 ESP32-S3 摄像头终端”，而不是单纯的网页图传 Demo。

## 当前已实现功能

- ESP32-S3 + 摄像头采集，默认使用 `RGB565`、`320x240(QVGA)`
- LCD 实时显示和 LVGL 触摸界面
- 本地 UI 操作：`Capture`、`Start`、`Detect`、`Cancel`
- 行人 / 人脸检测接口，检测结果可直接叠加在画面上
- 抓拍后通过 HTTP 上传图片到外部服务器
- 将上传后的图片 URL 发送给外部分析服务，并把结果显示在屏幕文本框中
- Wi-Fi 配置持久化到 NVS
- 保留 AP 配网页、MJPEG HTTP 视频流和 mDNS 服务代码
- 提供 Python 辅助脚本用于 OpenCV 拉流和接口联调

## 当前分支的重要说明

这部分建议先看，能省很多排查时间。

- `main/main.c` 里目前写死了测试用的 `WIFI_SSID` 和 `WIFI_PASSWORD`，首次启动时会自动写入 NVS。
- 因为存在上面的自动写入逻辑，当前分支通常不会进入 AP 配网页面。
- `start_mdns_service()` 和 `start_http_server()` 在 `main/main.c` 里被整段注释，因此默认不会启动网页视频流。
- `components/BSP/bsp_ui.c` 中还写死了图片上传服务器地址、分析服务器地址，以及一套外部视觉接口的调用配置。这些更适合作为开发期配置，不适合直接公开部署。

换句话说，README 里如果只写“连上热点打开网页看视频”，已经和这条分支的实际行为不一致了。

## 硬件与环境

从当前 BSP、`sdkconfig` 和依赖配置来看，工程目标环境大致如下：

- 芯片：ESP32-S3
- Flash：16 MB
- PSRAM：已启用
- 摄像头：当前引脚表按 `GC0308` / DVP 接口配置
- 屏幕与触摸：LCD + FT5x06 触摸
- IO 扩展：PCA9557
- 开发框架：ESP-IDF 5.x

仓库里的 BSP 命名为 `bsp_lichuang`，说明这套外设适配针对的是一块带屏幕、触摸和摄像头的 ESP32-S3 板卡组合；如果你换板，优先检查 `components/BSP/inc/bsp_camera.h`、`bsp_lcd.h`、`bsp_touch.h` 和 `bsp_pca9557.h`。

## 依赖组件

`main/idf_component.yml` 当前声明了这些核心依赖：

- `espressif/mdns`
- `espressif/esp32-camera`
- `lvgl/lvgl`
- `espressif/esp_lcd_touch_ft5x06`
- `espressif/esp-dl`
- `espressif/human_face_detect`
- `espressif/pedestrian_detect`

`dependencies.lock` 中还能看到 `esp-dl` 依赖 `idf >= 5.3`，所以建议直接使用较新的 ESP-IDF 5.x 环境。

## 编译与烧录

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

当前工程使用的分区表为：

```text
partitions_16mb.csv
```

分区布局包含一个较大的 `factory` 应用分区和一个 `fatfs` 数据分区，适合继续扩展模型、资源文件或本地缓存。

## 默认运行流程

按当前 `main/main.c` 的逻辑，设备上电后的主流程如下：

1. 初始化 NVS。
2. 如果 NVS 中没有 Wi-Fi 配置，则把源码里的测试 Wi-Fi 写入 NVS。
3. 以 STA 模式连接路由器。
4. 初始化 I2C、PCA9557、LCD、LVGL、触摸和摄像头。
5. 初始化检测器。
6. 创建本地 UI，等待屏幕按钮交互。

### 屏幕 UI 行为

- `Capture`
  抓拍当前图像，压缩为 JPEG，上传到外部 HTTP 服务器，再把图片 URL 发给分析服务，最终把返回文本显示到结果框。
- `Start`
  进入摄像头实时预览界面。
- `Detect`
  在实时预览界面中切换检测开关；当前默认检测类型是 `DETECTION_PEDESTRIAN`。
- `Cancel`
  退出预览界面并停止当前检测。

## 如果你想恢复网页图传功能

仓库里网络模块还在，但默认没有启用。要重新走“浏览器 / OpenCV 拉流”的路线，至少需要处理下面两件事：

1. 在 `main/main.c` 中重新启用：
   - `start_mdns_service();`
   - `start_http_server();`
2. 如果你希望使用 AP 配网页，而不是硬编码 Wi-Fi：
   - 去掉或改写首次启动时自动保存 Wi-Fi 的那段测试逻辑
   - 让未配置状态进入 `wifi_init_smartconfig()` 对应的 AP 配网流程

### 已实现但默认未启用的 HTTP 端点

#### 视频流服务

| 路径 | 方法 | 说明 |
| --- | --- | --- |
| `/` | GET | 简单预览页，页面中直接嵌入 `/stream` |
| `/stream` | GET | MJPEG 视频流 |
| `/favicon.ico` | GET | 返回空响应，避免浏览器 404 |

#### AP 配网服务

| 路径 | 方法 | 说明 |
| --- | --- | --- |
| `/` | GET | 配网页面 |
| `/wifi/save` | POST | 保存 Wi-Fi 配置到 NVS 并重启 |

当前代码里没有实现旧 README 中提到的 `/wifi/scan` 接口，所以这部分文档也不再保留。

## Python 辅助脚本

仓库根目录下有几份联调脚本：

- `opencv.py`
  通过固定 IP 读取 `/stream`。
- `opencv_viewer.py`
  优先尝试 `esp32cam.local`，失败后再让你手工输入 IP。
- `test.py`
  一个最小化的视频流读取样例。
- `api_test.py`
  用于单独验证图片上传接口。
- `test_api1.py`
  先上传图片，再调用外部视觉分析接口。

这些脚本更偏开发调试用途，运行前请先检查里面的 IP、文件路径和接口配置。

## 目录结构

```text
esp_http_stream/
|-- main/
|   |-- main.c             # 默认启动流程，当前主入口
|   |-- wifi_config.c      # Wi-Fi 配置的 NVS 存取
|   |-- wifi_manager.c     # STA / AP 模式初始化
|   |-- config_server.c    # AP 配网页面
|   |-- http_server.c      # MJPEG HTTP 视频流
|   |-- mdns_service.c     # mDNS 服务
|-- components/
|   |-- BSP/               # 摄像头、LCD、触摸、LVGL、UI、PCA9557 等板级支持
|   |-- human_detect/      # 人脸 / 行人检测封装
|-- managed_components/    # 组件管理器下载的依赖
|-- partitions_16mb.csv    # 16 MB 分区表
|-- sdkconfig              # 当前工程配置
|-- opencv.py              # OpenCV 拉流脚本
|-- opencv_viewer.py       # 自动发现 / 手工输入 IP 的拉流脚本
|-- api_test.py            # 上传接口测试脚本
|-- test_api1.py           # 上传 + 分析联调脚本
`-- README.md
```

## 代码层面的已知注意事项

- README 只描述了当前仓库状态，没有替你清理源码中的开发期硬编码配置。
- 如果要把这个项目公开发布，建议优先移除或外置：
  - 测试 Wi-Fi 账号
  - 外部服务器地址
  - API Key
- 当前默认路径更偏“本地屏幕 AI 终端”；如果你的目标是稳定网页图传，还需要把网络启动路径重新接回 `app_main`。

## 后续可继续完善的方向

- 把 Wi-Fi、服务器地址和检测模式改成可配置项
- 用 `menuconfig`、NVS 或私有配置头文件替代硬编码敏感信息
- 让 AP 配网、LCD UI 和 HTTP 流媒体三条路径能共存
- 增加检测类型切换和阈值设置
- 为上传与分析链路补充超时、重试和错误提示
- 补一份“当前硬件接线 / 板卡型号”说明，降低移植门槛

## 许可证

本项目使用 MIT License，详见 `LICENSE`。
