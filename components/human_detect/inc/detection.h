#ifndef _DETECTION_H_
 #define _DETECTION_H_

 #include "esp_err.h"
 #include "esp_camera.h"
 #include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

 #ifdef __cplusplus
 extern "C" {
 #endif

 // 检测类型
 typedef enum {
     DETECTION_FACE = 0,       // 人脸检测
     DETECTION_PEDESTRIAN = 1  // 行人检测
 } detection_type_t;

 // 边界框
 typedef struct {
     int x;  // 左上角 X 坐标
     int y;  // 左上角 Y 坐标
     int w;  // 宽度
     int h;  // 高度
 } detection_box_t;

 // 关键点（用于人脸检测）
 typedef struct {
     int x;
     int y;
 } detection_keypoint_t;

 // 检测结果
 typedef struct {
     detection_box_t box;                // 边界框
     float score;                        // 置信度分数 (0.0-1.0)
     detection_keypoint_t keypoints[5];  // 人脸关键点（眼睛、鼻子、嘴角）
 } detection_result_t;

 /**
  * @brief 初始化检测器
  * @param type 检测类型（人脸或行人）
  * @return ESP_OK 成功
  */
 esp_err_t bsp_detection_init(detection_type_t type);

 /**
  * @brief 执行检测
  * @param type 检测类型
  * @param fb 摄像头帧缓冲（必须是 RGB565 格式）
  * @param results 结果数组
  * @param max_results 最大结果数
  * @return 检测到的对象数量
  */
 int bsp_detection_run(detection_type_t type, camera_fb_t *fb,
                       detection_result_t *results, int max_results);

 /**
  * @brief 在帧缓冲上绘制检测结果
  * @param framebuffer RGB565 帧缓冲
  * @param width 宽度
  * @param height 高度
  * @param results 检测结果
  * @param count 结果数量
  */
 void bsp_detection_draw_results(uint16_t *framebuffer, int width, int height,
                                 detection_result_t *results, int count);

 /**
  * @brief 释放检测器
  * @param type 检测类型
  */
 void bsp_detection_deinit(detection_type_t type);

 #ifdef __cplusplus
 }
 #endif

 #endif // _DETECTION_H_