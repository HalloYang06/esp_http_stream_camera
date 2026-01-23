# ESP32-CAM 无线图传系统

基于 ESP-IDF 框架在 ESP32-S3 平台上实现的无线图传系统，支持 Web 配网和 MJPEG 视频流。

## 主要特性

- ✅ **智能 WiFi 配网** - AP 模式配网页面，首次使用自动进入配网模式
- ✅ **MJPEG 视频流** - 通过 HTTP 协议实时传输摄像头画面
- ✅ **浏览器观看** - 支持 Chrome、Firefox、Edge、Safari
- ✅ **OpenCV 兼容** - 可用 Python OpenCV 接收视频流
- ✅ **mDNS 服务** - 通过 `esp32cam.local` 域名访问（无需记 IP）
- ✅ **NVS 存储** - WiFi 配置持久化，重启后自动连接
- ✅ **自适应模式** - 未配网时自动开启 AP 模式，已配网自动连接 STA 模式

## 硬件要求

- ESP32-S3 开发板（带摄像头接口）
- GC0308 摄像头模块或兼容摄像头
- USB 数据线

## 快速开始

### 1. 首次使用 - WiFi 配网

首次烧录程序后，ESP32 会自动进入 AP 配网模式：

1. **连接 ESP32 热点**
   - WiFi 名称：`ESP32-CAM-Setup`
   - 密码：`12345678`

2. **打开配网页面**
   - 连接后自动跳转，或手动访问：`http://192.168.4.1`

3. **选择 WiFi 并输入密码**
   - 在配网页面选择您的家庭 WiFi
   - 输入密码
   - 点击"保存并连接"

4. **自动重启并连接**
   - ESP32 会保存配置并重启
   - 自动连接到您的 WiFi 网络
   - 配置会永久保存，下次开机自动连接

### 2. 查看 IP 地址

连接成功后，通过串口监视器查看 IP 地址：

```bash
idf.py monitor
```

日志输出示例：
```
I (xxx) WIFI: Got IP:192.168.1.123
I (xxx) HTTP: HTTP server started successfully
I (xxx) mDNS: Hostname: esp32cam.local
I (xxx) app_main: =================================================
I (xxx) app_main: System Init OK - HTTP Stream Ready
I (xxx) app_main: Open browser and visit:
I (xxx) app_main:   - http://192.168.1.123/
I (xxx) app_main:   - http://esp32cam.local/
I (xxx) app_main: =================================================
```

### 3. 访问视频流

#### 方式一：浏览器访问

- **通过 IP 地址**: `http://192.168.1.123/`
- **通过域名**: `http://esp32cam.local/` （推荐）

支持的浏览器：
- ✅ Chrome
- ✅ Firefox
- ✅ Edge
- ✅ Safari

#### 方式二：OpenCV 查看

安装依赖：
```bash
pip install opencv-python numpy
```

Python 示例代码：
```python
import cv2

# 方式1：使用IP地址
url = "http://192.168.1.123/stream"

# 方式2：使用mDNS域名（推荐）
# url = "http://esp32cam.local/stream"

cap = cv2.VideoCapture(url)

while True:
    ret, frame = cap.read()
    if ret:
        cv2.imshow('ESP32-CAM', frame)

        # 可以在这里添加图像处理代码
        # gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cap.release()
cv2.destroyAllWindows()
```

## HTTP API 端点

### 视频流服务器（STA 模式 - 连接 WiFi 后）

| 端点 | 方法 | 说明 |
|------|------|------|
| `/` | GET | 主页HTML，显示视频流和基本信息 |
| `/stream` | GET | MJPEG 视频流，用于直接访问或 OpenCV |
| `/favicon.ico` | GET | 网站图标（防止 404 错误） |

### 配网服务器（AP 模式 - 配网时）

| 端点 | 方法 | 说明 |
|------|------|------|
| `/` | GET | 配网页面，显示 WiFi 扫描和配置表单 |
| `/wifi/scan` | GET | 扫描附近 WiFi 网络，返回 JSON |
| `/wifi/save` | POST | 保存 WiFi 配置并重启 |

## 视频参数说明

- **分辨率**: 320x240 (QVGA)
- **格式**: MJPEG
- **帧率**: 约 15fps
- **JPEG 质量**: 50（范围 0-100，值越小质量越高）

### 调整视频质量

编辑 `main/esp_cam.c` 中的摄像头配置：

```c
config.jpeg_quality = 12;  // 0-63，数值越小质量越高，速度越慢
```

推荐设置：
- `10-15`: 高质量（较慢，适合静态场景）
- `20-30`: 中等质量（推荐）
- `40-50`: 低质量（更快，适合移动场景）

在 `main/network.c` 的 `stream_handler` 函数中调整转换质量：

```c
is_converted = frame2jpg(fb, 50, &_jpg_buf, &_jpg_buf_len);  // 修改这个值
```

## 编译和烧录

### 环境准备

确保已安装 ESP-IDF v5.0 或更高版本。

### 编译项目

```bash
cd esp_http_stream
idf.py build
```

### 烧录到开发板

```bash
idf.py flash
```

### 查看日志

```bash
idf.py monitor
```

退出监视器：`Ctrl+]`

### 一键完成（编译+烧录+监视）

```bash
idf.py build flash monitor
```

## 配置说明

### WiFi 配置清除

如需重新配网，可以清除保存的 WiFi 配置：

```c
// 在 main/main.c 中调用
wifi_clear_config();
esp_restart();
```

或者通过串口命令（需自行实现）清除 NVS。

### HTTP 服务器配置

#### 解决请求头过长问题

如果遇到 `431 Request Header Fields Too Large` 错误，需要增加 HTTP 服务器缓冲区：

```bash
idf.py menuconfig
```

导航至：
```
Component config --->
    HTTP Server --->
        Max HTTP Request Header Length = 2048  (默认512)
        Max HTTP URI Length = 1024  (默认512)
```

保存后重新编译。

### mDNS 域名修改

编辑 `main/network.c` 中的域名设置：

```c
mdns_hostname_set("esp32cam");  // 修改为你想要的域名
```

修改后可通过 `http://你的域名.local/` 访问。

## 故障排除

### 1. WiFi 配网问题

**问题**：找不到 ESP32-CAM-Setup 热点
- 检查 ESP32 是否正常启动（查看串口日志）
- 确认是首次使用或已清除配置
- 重启 ESP32 设备

**问题**：配网页面打不开
- 确认已连接到 ESP32 热点
- 手动访问：`http://192.168.4.1`
- 检查手机是否禁用了对无互联网 WiFi 的访问

**问题**：保存配置后无法连接
- 检查 WiFi 密码是否正确
- 确认 WiFi 频段为 2.4GHz（ESP32 不支持 5GHz）
- 查看串口日志的错误信息

### 2. 网络连接问题

**测试网络连通性**：

```bash
# Ping 测试
ping 192.168.1.123

# 端口测试（Windows PowerShell）
Test-NetConnection -ComputerName 192.168.1.123 -Port 80

# HTTP 响应测试
curl -v http://192.168.1.123/ 2>&1 | head -30
```

**问题**：无法访问视频流
- 确保 ESP32 和电脑/手机在同一网络
- 检查 IP 地址是否正确
- 尝试使用 mDNS 域名：`http://esp32cam.local/`
- 查看串口日志确认 HTTP 服务器已启动

### 3. 视频流问题

**问题**：视频卡顿或延迟高
- 降低 JPEG 质量（增加数值 40-50）
- 减少同时连接的客户端数量
- 确保 WiFi 信号良好（靠近路由器）
- 降低分辨率（修改 `frame_size`）

**问题**：图像是黑屏或花屏
- 检查摄像头是否正常连接
- 确认摄像头型号配置正确（GC0308）
- 检查串口日志的摄像头初始化信息
- 重启设备

**问题**：OpenCV 无法打开流
- 确认 opencv-python 已安装：`pip install opencv-python`
- 先在浏览器测试视频流是否正常
- 检查防火墙是否阻止了连接
- 使用 IP 地址而不是域名测试

### 4. 编译错误

**问题**：MACSTR 未定义
- 确保 `network.h` 包含了 `#include "esp_mac.h"`
- ESP-IDF 版本需 >= 5.0

**问题**：httpd_config_t 成员不存在
- 某些配置需要通过 menuconfig 设置，不是代码中的结构体成员
- 参考上面的"HTTP 服务器配置"章节

## 性能优化建议

### 1. 降低分辨率（提高帧率）

```c
config.frame_size = FRAMESIZE_QQVGA;  // 160x120（更快）
```

### 2. 调整 JPEG 质量

```c
config.jpeg_quality = 30;  // 降低质量，提高速度
```

### 3. 减少客户端连接

同时连接的客户端越少，性能越好。建议单客户端访问。

### 4. WiFi 优化

- 使用 2.4GHz WiFi（信号覆盖更好）
- 靠近路由器以获得更好的信号
- 避免 WiFi 信道拥挤

### 5. 增加栈大小

如果遇到栈溢出，可在 menuconfig 中增加任务栈大小。

## 项目结构

```
esp_http_stream/
├── main/
│   ├── main.c           # 主程序入口
│   ├── esp_cam.c        # 摄像头驱动和配置
│   ├── network.c        # WiFi、HTTP服务器、mDNS
│   ├── network.h        # 网络相关头文件
│   └── CMakeLists.txt
├── build/               # 编译输出目录
├── managed_components/  # ESP组件管理器依赖
├── CMakeLists.txt
├── sdkconfig           # ESP-IDF 配置文件
└── README.md           # 本文件
```

## 依赖组件

- `espressif/esp32-camera` - 摄像头驱动
- `espressif/esp_jpeg` - JPEG 编码
- `espressif/mdns` - mDNS 服务

## 技术栈

- **开发框架**: ESP-IDF v5.4.2
- **编程语言**: C
- **通信协议**: HTTP/1.1, mDNS
- **视频格式**: MJPEG
- **WiFi 模式**: AP + STA 双模式
- **存储**: NVS（Non-Volatile Storage）

## 后续开发建议

- [ ] 添加 WiFi 扫描列表到配网页面
- [ ] 支持手动输入静态 IP
- [ ] 添加 OTA（空中升级）功能
- [ ] 支持多客户端并发优化
- [ ] 添加图像处理功能（人脸检测、运动检测等）
- [ ] 支持录像和截图功能
- [ ] 添加 HTTPS 支持
- [ ] Web 界面美化和功能增强

## 常见问题 FAQ

**Q: 支持 5GHz WiFi 吗？**
A: 不支持，ESP32 只支持 2.4GHz WiFi。

**Q: 可以同时连接多少个客户端？**
A: 理论上支持多个，但建议不超过 3 个以保证流畅度。

**Q: 视频有延迟吗？**
A: 正常情况下延迟约 200-500ms，取决于网络状况和视频质量设置。

**Q: 可以用在室外吗？**
A: 可以，但需要注意 WiFi 覆盖范围和设备防护。

**Q: 如何提高视频质量？**
A: 降低 `jpeg_quality` 数值（10-20），但会降低帧率。

**Q: 支持夜视功能吗？**
A: 取决于您的摄像头模块是否支持红外夜视。

## 许可证

本项目遵循 MIT 许可证。

## 贡献

欢迎提交 Issue 和 Pull Request！

## 联系方式

如有问题或建议，欢迎创建 Issue 讨论。

---

**享受您的 ESP32-CAM 无线图传系统！** 🎥📡
