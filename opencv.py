
import cv2
import numpy as np


ESP32_IP = "192.168.65.123" 
STREAM_URL = f"http://{ESP32_IP}/stream"

def main():
    print(f"连接到ESP32-CAM视频流: {STREAM_URL}")
    print("按 'q' 键退出")

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

        # 在图像上显示帧数
        cv2.putText(frame, f"Frame: {frame_count}", (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)

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
