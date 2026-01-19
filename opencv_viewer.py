import cv2
import numpy as np
import socket

def find_esp32_ip():
    """
    自动查找ESP32的IP地址
    优先使用mDNS，失败则提示手动输入
    """
    # 方法1：尝试mDNS
    try:
        print("正在通过mDNS查找ESP32...")
        ip = socket.gethostbyname("esp32cam.local")
        print(f"✓ 找到ESP32: esp32cam.local -> {ip}")
        return ip
    except socket.gaierror:
        print("✗ mDNS解析失败")
        print("提示：ESP32需要启用mDNS服务")

    # 方法2：手动输入
    print("\n请手动输入ESP32的IP地址（或直接回车使用默认值）：")
    ip = input("ESP32 IP [192.168.65.123]: ").strip()
    return ip if ip else "192.168.65.123"

# 自动获取ESP32的IP
ESP32_IP = find_esp32_ip()
STREAM_URL = f"http://{ESP32_IP}/stream"

def main():
    print(f"\n连接到ESP32-CAM视频流: {STREAM_URL}")
    print("按 'q' 键退出\n")

    # 打开视频流
    cap = cv2.VideoCapture(STREAM_URL)

    if not cap.isOpened():
        print(f"错误：无法连接到 {STREAM_URL}")
        print("请检查：")
        print("1. ESP32的IP地址是否正确")
        print("2. ESP32是否已连接WiFi")
        print("3. HTTP服务器是否已启动")
        return

    print("视频流已连接！")

    frame_count = 0

    while True:
        ret, frame = cap.read()

        if not ret:
            print("无法读取帧，尝试重新连接...")
            break

        frame_count += 1

        # 在图像上显示帧数和IP
        cv2.putText(frame, f"Frame: {frame_count}", (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
        cv2.putText(frame, f"IP: {ESP32_IP}", (10, 60),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

        # 显示图像
        cv2.imshow('ESP32-CAM Stream', frame)

        # 按'q'退出
        if cv2.waitKey(1) & 0xFF == ord('q'):
            print("用户退出")
            break

    cap.release()
    cv2.destroyAllWindows()
    print(f"总共接收 {frame_count} 帧")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n程序被中断")
    except Exception as e:
        print(f"发生错误: {e}")
