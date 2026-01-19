<<<<<<< HEAD
# esp-cam
基于esp-idf框架在esp32s3平台上实现的无线图传
## 功能说明
ESP32-CAM通过HTTP协议以MJPEG格式发送视频流，支持浏览器和OpenCV查看。

## 使用步骤

### 1. 编译并烧录程序
```bash
idf.py build
idf.py flash monitor
```

### 2. 查看串口日志获取IP地址
连接成功后会显示：
```
I (xxx) WIFI: Got IP:192.168.65.xxx
I (xxx) HTTP: HTTP server started successfully
I (xxx) app_main: =================================================
I (xxx) app_main: System Init OK - HTTP Stream Ready
I (xxx) app_main: Open browser and visit: http://[ESP32_IP]/
I (xxx) app_main: =================================================
```

记下这个IP地址。

### 3. 方式一：浏览器查看

打开浏览器访问：
- **主页**: `http://[ESP32_IP]/` （显示网页+视频）
- **直接流**: `http://[ESP32_IP]/stream` （仅视频流）

例如：`http://192.168.43.100/`

支持的浏览器：
- ✅ Chrome
- ✅ Firefox
- ✅ Edge
- ✅ Safari

### 4. 方式二：OpenCV查看

#### 安装依赖
```bash
pip install opencv-python numpy
```

#### 修改并运行脚本
1. 打开 `opencv_viewer.py`
2. 修改 `ESP32_IP` 为实际IP地址
3. 运行：
```bash
python opencv_viewer.py
```

#### OpenCV示例代码
```python
import cv2

ESP32_IP = "192.168.65.123"  # 修改为你的IP
url = f"http://{ESP32_IP}/stream"

cap = cv2.VideoCapture(url)

while True:
    ret, frame = cap.read()
    if ret:
        cv2.imshow('ESP32-CAM', frame)
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cap.release()
cv2.destroyAllWindows()
###(后续可以在opencv中运行图像处理)

```



## 视频参数

- **分辨率**: 320x240 (QVGA)
- **格式**: MJPEG
- **帧率**: 约15fps（可调整）
- **质量**: JPEG质量=12（可在代码中调整0-63）

## 调整视频质量

编辑 `esp_cam.c` 第134行：
```c
.jpeg_quality = 12,  // 0-63，数值越小质量越高
```

- `10-15`: 高质量（推荐）
- `20-30`: 中等质量
- `40-63`: 低质量（更快）

## 故障排除
### 1.ping测试 
        ```
        bash
        ping [your ip]
        ```
         
    2.进行端口测试
        ```bash

         powershell -Command "Test-NetConnection -ComputerName [your ip] -Port 80"
         ```
    3.相应测试
    ```bash
         curl -v http://[your ip]/ 2>&1 | head -30
    ```

### 问题1：浏览器无法连接
- 检查ESP32和电脑在同一WiFi网络
- 检查IP地址是否正确
- 查看串口日志确认HTTP服务器已启动

### 问题2：视频卡顿
- 降低JPEG质量（增加数值）
- 确保WiFi信号良好
- 减少连接的客户端数量

### 问题3：OpenCV无法打开
- 确认已安装opencv-python：`pip install opencv-python`
- 尝试在浏览器中打开，确认流正常
- 检查防火墙设置

### 问题4：图像是黑屏
- 检查摄像头是否正常初始化
- 检查摄像头供电
- 查看串口日志是否有错误

## 代码说明

### HTTP端点

| 端点 | 说明 | 用途 |
|------|------|------|
| `/` | 主页HTML | 显示简单网页和视频流 |
| `/stream` | MJPEG视频流 | 浏览器/OpenCV/VLC查看 |

### 当前配置

- **摄像头格式**: JPEG
- **LCD显示**: 已禁用（JPEG模式无法直接显示）
- **HTTP服务器端口**: 80

### 如需同时支持LCD显示和HTTP流

需要使用RGB转JPEG编码，或者使用双摄像头配置（较复杂）。

## 性能优化建议

1. **降低分辨率**（更快帧率）：
   ```c
   .frame_size = FRAMESIZE_QQVGA,  // 160x120
   ```

2. **增加帧率**（降低质量）：
   ```c
   .jpeg_quality = 30,  // 降低质量
   ```

3. **减少客户端**：
   同时连接的客户端越少，性能越好


## 许可证


=======
# esp-camera-http-stream
>>>>>>> ab70dbfb87bfc9d752edb0f49188efdf80d23f01
