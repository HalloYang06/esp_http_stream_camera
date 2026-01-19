import cv2

# 快速测试ESP32-CAM视频流
url = "http://192.168.65.123/stream"
print(f"连接到: {url}")

cap = cv2.VideoCapture(url)

if not cap.isOpened():
    print("错误：无法打开视频流")
else:
    print("成功！视频流已连接。按q退出。")

    while True:
        ret, frame = cap.read()
        if ret:
            cv2.imshow('ESP32-CAM', frame)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    cap.release()
    cv2.destroyAllWindows()
