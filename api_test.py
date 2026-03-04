import http.client
import mimetypes
from codecs import encode

conn = http.client.HTTPSConnection("115.190.73.223")
dataList = []
boundary = 'wL36Yn8afVp8Ag7AmP8qZ0SA4n1v9T'
dataList.append(encode('--' + boundary))
dataList.append(encode('Content-Disposition: form-data; name=file; filename={0}'.format('a2b6aeff25049c56e32eed1ee9101181.png')))

fileType = mimetypes.guess_type('/Users/tudao/Downloads/a2b6aeff25049c56e32eed1ee9101181.png')[0] or 'application/octet-stream'
dataList.append(encode('Content-Type: {}'.format(fileType)))
dataList.append(encode(''))

with open('/Users/tudao/Downloads/a2b6aeff25049c56e32eed1ee9101181.png', 'rb') as f:
   dataList.append(f.read())
dataList.append(encode('--'+boundary+'--'))
dataList.append(encode(''))
body = b'\r\n'.join(dataList)
payload = body
headers = {
   'User-Agent': 'Apifox/1.0.0 (https://apifox.com)',
   'Accept': '*/*',
   'Host': '115.190.73.223',
   'Connection': 'keep-alive',
   'Content-Type': 'multipart/form-data; boundary=--------------------------944382644541775493126553',
   'Content-type': 'multipart/form-data; boundary={}'.format(boundary)
}
conn.request("POST", "/xiaozhi/admin/images", payload, headers)
res = conn.getresponse()
data = res.read()
print(data.decode("utf-8"))