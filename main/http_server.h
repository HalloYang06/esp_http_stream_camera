#ifndef _HTTP_SERVER_H_
#define _HTTP_SERVER_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 HTTP 视频流服务器
 * @return ESP_OK 成功，其他失败
 */
esp_err_t http_server_start(void);

/**
 * @brief 停止 HTTP 视频流服务器
 */
void http_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif // _HTTP_SERVER_H_
