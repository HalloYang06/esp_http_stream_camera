#ifndef _CONFIG_SERVER_H_
#define _CONFIG_SERVER_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动配网 HTTP 服务器
 * @return ESP_OK 成功，其他失败
 */
esp_err_t config_server_start(void);

/**
 * @brief 停止配网 HTTP 服务器
 */
void config_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif // _CONFIG_SERVER_H_
