#ifndef _MDNS_SERVICE_H_
#define _MDNS_SERVICE_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 mDNS 服务
 * @return ESP_OK 成功，其他失败
 */
esp_err_t mdns_service_start(void);

#ifdef __cplusplus
}
#endif

#endif // _MDNS_SERVICE_H_
