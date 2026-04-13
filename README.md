# esp_http_stream

`esp_http_stream` 是一个基于 ESP32-S3 和 ESP-IDF 的嵌入式视觉交互项目，融合了无线图传、本地显示、触摸交互、图像抓拍上传、云端分析和本地目标检测等能力。项目既保留了原有的 Web 配网与 HTTP 视频流接口，也扩展了 LCD + LVGL 的本地 UI，适合作为智能摄像头、视觉终端和物联网图像交互设备的基础工程。

## 项目简介

本项目最初围绕 ESP32 摄像头图传能力展开，后续逐步加入了屏幕显示、触摸操作、图片上传和视觉识别等模块，形成了一个集“采集、显示、传输、交互、识别”于一体的综合示例工程。

从功能形态上看，这个项目同时覆盖了两类使用方式：

- 网络访问
  通过 HTTP / MJPEG 在浏览器或 OpenCV 中查看摄像头画面，并支持配网页面与 mDNS 访问。
- 本地交互
  通过 LCD + LVGL + 触摸界面在设备端直接预览图像、启动检测、抓拍上传并查看分析结果。

## 主要功能

- 无线图传
  基于 HTTP 提供 MJPEG 视频流，可在浏览器中直接访问，也可由 OpenCV 等上位机程序读取。
- Web 配网
  支持 AP 模式下的网页配置，用户可通过手机或电脑填写 Wi-Fi 信息完成联网。
- Wi-Fi 配置持久化
  通过 NVS 保存网络配置，设备重启后可自动恢复连接。
- mDNS 访问
  支持通过 `esp32cam.local` 访问设备，降低记忆 IP 地址的成本。
- 本地屏幕显示
  通过 LCD 实时显示摄像头画面，并结合 LVGL 构建本地图形界面。
- 触摸交互
  支持触摸按钮操作，提供 `Start`、`Capture`、`Detect`、`Cancel` 等基础交互。
- 图片抓拍上传
  可将拍摄到的图像压缩为 JPEG 后上传至外部服务器。
- 云端图像分析
  上传图片后可将图片 URL 发送到外部分析服务，并将识别结果回显到界面。
- 本地视觉检测
  集成行人检测与人脸检测接口，可在摄像头画面上直接叠加检测框。
- 模块化工程结构
  网络、BSP、UI、检测等能力彼此拆分，便于后续扩展和移植。

## 功能架构

项目整体可以分为 4 个核心层次：

1. 采集与板级驱动层
   由 `components/BSP` 提供摄像头、LCD、触摸、I2C、PCA9557 等硬件驱动支持。
2. 网络与服务层
   由 `main/wifi_manager.c`、`config_server.c`、`http_server.c`、`mdns_service.c` 提供联网、配网、图传和服务发现能力。
3. 本地交互层
   由 `components/BSP/bsp_ui.c` 和 `bsp_lvgl.c` 构建本地屏幕 UI、预览和触摸交互。
4. 视觉处理层
   由 `components/human_detect` 封装人脸检测、行人检测以及结果绘制逻辑。

## 典型功能流程

### 1. 无线图传流程

1. 摄像头采集图像帧。
2. HTTP 服务输出 MJPEG 视频流。
3. 用户通过浏览器访问 `/` 或直接读取 `/stream`。
4. 上位机也可以通过 OpenCV 直接拉流处理。

### 2. 本地交互流程

1. 系统初始化 Wi-Fi、LCD、LVGL、触摸与摄像头。
2. 屏幕显示本地 UI。
3. 用户通过触摸按钮进入预览、抓拍、检测等功能。
4. 检测结果或云端分析结果显示在屏幕文本区域中。

### 3. 抓拍上传与分析流程

1. 设备抓取当前摄像头帧。
2. 将图像压缩为 JPEG。
3. 通过 HTTP multipart/form-data 上传到外部服务器。
4. 获取图片 URL 后再提交给分析服务。
5. 将返回文本显示在设备界面上。

## 硬件与环境

根据当前 BSP 和工程配置，项目主要面向以下硬件环境：

- 主控芯片：ESP32-S3
- Flash：16 MB
- PSRAM：已启用
- 摄像头：当前按 GC0308 / DVP 接口进行配置
- 触摸芯片：FT5x06
- IO 扩展：PCA9557
- 屏幕：LCD 显示模块

开发环境建议：

- ESP-IDF 5.x
- Python 3.x
- 已正确安装 ESP-IDF 工具链与驱动

## 依赖组件

`main/idf_component.yml` 中声明了以下核心依赖：

- `espressif/mdns`
- `espressif/esp32-camera`
- `lvgl/lvgl`
- `espressif/esp_lcd_touch_ft5x06`
- `espressif/esp-dl`
- `espressif/human_face_detect`
- `espressif/pedestrian_detect`

这些依赖共同支撑了图传、UI、触摸和视觉检测能力。

## HTTP 接口说明

项目保留了原有网络接口，便于继续进行网页图传或上位机接入。

### 视频流接口

| 路径 | 方法 | 说明 |
| --- | --- | --- |
| `/` | GET | 视频预览主页 |
| `/stream` | GET | MJPEG 视频流 |
| `/favicon.ico` | GET | 浏览器图标请求占位 |

### 配网接口

| 路径 | 方法 | 说明 |
| --- | --- | --- |
| `/` | GET | AP 模式下的配网页面 |
| `/wifi/save` | POST | 保存 Wi-Fi 配置并重启 |

### mDNS 服务

- 主机名：`esp32cam.local`
- 访问方式：`http://esp32cam.local/`

## 本地 UI 功能说明

当前界面中包含以下主要操作：

- `Start`
  进入摄像头实时预览界面。
- `Capture`
  抓拍当前画面并上传到服务器，再调用分析服务获取文本结果。
- `Detect`
  开启或关闭检测功能，可用于行人或人脸检测。
- `Cancel`
  退出预览界面，停止检测并恢复主界面控件。

这套本地 UI 让设备即使不依赖浏览器，也能完成图像查看、检测和结果展示。

## 快速开始

### 1. 编译工程

```bash
idf.py set-target esp32s3
idf.py build
```

### 2. 烧录并查看日志

```bash
idf.py flash monitor
```

### 3. 使用方式

- 如果走本地交互路径：
  烧录后可在屏幕上直接查看 UI 并进行触摸操作。
- 如果走网络图传路径：
  可通过网页或 OpenCV 访问 HTTP 视频流接口。

### 4. OpenCV 调试

仓库中提供了以下辅助脚本：

- `opencv.py`
  使用固定 IP 拉取视频流。
- `opencv_viewer.py`
  尝试通过 `esp32cam.local` 查找设备并拉流。
- `test.py`
  用于最小化验证 `/stream` 是否可用。

## 目录结构

```text
esp_http_stream/
|-- main/
|   |-- main.c              # 主入口
|   |-- wifi_config.c       # Wi-Fi 配置存储
|   |-- wifi_manager.c      # Wi-Fi 模式管理
|   |-- config_server.c     # Web 配网服务
|   |-- http_server.c       # HTTP 视频流服务
|   |-- mdns_service.c      # mDNS 服务
|-- components/
|   |-- BSP/                # 摄像头、LCD、触摸、LVGL、UI 等板级驱动
|   |-- human_detect/       # 人脸/行人检测模块
|-- managed_components/     # 组件管理器拉取的依赖
|-- partitions_16mb.csv     # 分区表
|-- sdkconfig               # 工程配置
|-- opencv.py               # OpenCV 拉流脚本
|-- opencv_viewer.py        # 自动发现并拉流脚本
|-- api_test.py             # 上传接口测试脚本
|-- test_api1.py            # 上传与分析联调脚本
`-- README.md
```

## 项目特点

- 兼顾网络图传与本地屏幕交互两种使用模式
- 既能作为 Demo，也适合作为后续产品原型开发基础
- 具备视觉采集、检测、上传、分析的完整链路
- 工程分层清晰，便于后续裁剪、扩展和移植

## 可扩展方向

- 增加更多视觉模型与检测目标
- 支持录像、抓图存储与历史记录
- 增强 Web 页面交互能力
- 引入 OTA 升级
- 将服务器地址、Wi-Fi、检测参数做成可配置项
- 支持更多屏幕和摄像头模组

## 许可证

本项目使用 MIT License，详见 `LICENSE`。
