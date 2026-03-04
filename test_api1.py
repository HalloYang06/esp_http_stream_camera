import http.client
import mimetypes
from codecs import encode
import requests
import json

# ========== 第一步：上传图片到服务器 ==========
SERVER_HOST = "115.190.73.223"
UPLOAD_ENDPOINT = "/xiaozhi/admin/images"
LOCAL_FILE_PATH = r"C:\Users\27352\Pictures\微信图片_20251224130525_152_12.jpg"
FILE_NAME = "微信图片_20251224130525_152_12.jpg"

def upload_image():
    boundary = 'wL36Yn8afVp8Ag7AmP8qZ0SA4n1v9T'
    dataList = []
    dataList.append(encode('--' + boundary))
    dataList.append(encode(f'Content-Disposition: form-data; name=file; filename={FILE_NAME}'))

    fileType = mimetypes.guess_type(LOCAL_FILE_PATH)[0] or 'application/octet-stream'
    dataList.append(encode('Content-Type: {}'.format(fileType)))
    dataList.append(encode(''))

    with open(LOCAL_FILE_PATH, 'rb') as f:
        dataList.append(f.read())
    dataList.append(encode('--'+boundary+'--'))
    dataList.append(encode(''))
    body = b'\r\n'.join(dataList)
    payload = body
    headers = {
        'User-Agent': 'Apifox/1.0.0 (https://apifox.com)',
        'Accept': '*/*',
        'Host': SERVER_HOST,
        'Connection': 'keep-alive',
        'Content-Type': f'multipart/form-data; boundary={boundary}',
        'Content-Length': str(len(payload))
    }

    conn = http.client.HTTPConnection(SERVER_HOST, timeout=10)
    try:
        conn.request("POST", UPLOAD_ENDPOINT, payload, headers)
        res = conn.getresponse()
        data = res.read().decode("utf-8")
        print(f"上传图片状态码：{res.status}")
        print(f"上传响应：{data}")
        if res.status == 200:
            # 解析返回的JSON，拿到图片URL
            result = json.loads(data)
            return f"http://{SERVER_HOST}{result['url']}"
        return None
    except Exception as e:
        print(f"上传失败：{str(e)}")
        return None
    finally:
        conn.close()

# ========== 第二步：调用 GLM-4.6v 视觉接口 ==========
GLM_API_URL = "https://open.bigmodel.cn/api/paas/v4/chat/completions"
API_KEY = "640874cba211c88cbfdba211e005a15f.n5aU0HZDrTmYd2ji"
QUESTION = "图片里的主要内容是什么? 如果图片里有人物, 描述下这个人物在干嘛"

def call_glm(image_url):
    payload = {
        "model": "glm-4.6v",
        "messages": [
            {
                "role": "user",
                "content": [
                    {"type": "image_url", "image_url": {"url": image_url}},
                    {"type": "text", "text": QUESTION}
                ]
            }
        ],
        "thinking": {"type": "enabled"}
    }
    headers = {
        "Authorization": f"Bearer {API_KEY}",
        "Content-Type": "application/json"
    }
    try:
        response = requests.post(GLM_API_URL, headers=headers, json=payload, timeout=30)
        print(f"GLM 接口状态码：{response.status_code}")
        if response.status_code == 200:
            result = response.json()
            print("AI 分析结果：")
            print(json.dumps(result, ensure_ascii=False, indent=2))
        else:
            print(f"GLM 接口失败：{response.text}")
    except Exception as e:
        print(f"调用 GLM 失败：{str(e)}")

# ========== 主函数：一键执行 ==========
if __name__ == "__main__":
    print("=== 开始上传图片 ===")
    image_url = upload_image()
    if image_url:
        print(f"\n=== 开始调用 GLM 接口，图片URL：{image_url} ===")
        call_glm(image_url)
    else:
        print("图片上传失败，无法继续。")