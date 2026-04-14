#include "detection.h"
 #include "esp_log.h"
 #include <string.h>

 static const char *TAG = "bsp_detection";

 // 检测器句柄（C++ 对象指针）
 static void *g_face_detector = NULL;
 static void *g_pedestrian_detector = NULL;

 // C++ 接口桥接函数（在 .cpp 文件中实现）
 extern void* create_face_detector(void);
 extern void* create_pedestrian_detector(void);
 extern int run_detection(void *detector, uint8_t *img_data, int width, int height,
                          detection_result_t *results, int max_results);
 extern void destroy_detector(void *detector);
static SemaphoreHandle_t det_mutex = NULL;
 esp_err_t bsp_detection_init(detection_type_t type)
 {
     ESP_LOGI(TAG, "Initializing detector type: %d", type);
    if (det_mutex == NULL) {
        det_mutex = xSemaphoreCreateMutex();
        if (det_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_FAIL;
        }
    }
    xSemaphoreTake(det_mutex,portMAX_DELAY);
     if (type == DETECTION_FACE) {
         if (g_face_detector == NULL) {
             g_face_detector = create_face_detector();
             if (g_face_detector == NULL) {
                 ESP_LOGE(TAG, "Failed to create face detector");
                 return ESP_FAIL;
             }
         }
     } else if (type == DETECTION_PEDESTRIAN) {
         if (g_pedestrian_detector == NULL) {
             g_pedestrian_detector = create_pedestrian_detector();
             if (g_pedestrian_detector == NULL) {
                 ESP_LOGE(TAG, "Failed to create pedestrian detector");
                 return ESP_FAIL;
             }
         }
     }
     xSemaphoreGive(det_mutex);
     ESP_LOGI(TAG, "Detector initialized successfully");
     return ESP_OK;
 }

 int bsp_detection_run(detection_type_t type, camera_fb_t *fb,
                       detection_result_t *results, int max_results)
 {
     if (fb == NULL || results == NULL) {
         ESP_LOGE(TAG, "Invalid parameters");
         return 0;
     }

     // 确保图像格式为 RGB565
     if (fb->format != PIXFORMAT_RGB565) {
         ESP_LOGE(TAG, "Unsupported pixel format: %d (need RGB565)", fb->format);
         return 0;
     }

     void *detector = (type == DETECTION_FACE) ? g_face_detector : g_pedestrian_detector;
     if (detector == NULL) {
         ESP_LOGE(TAG, "Detector not initialized");
         return 0;
     }

     // 执行检测
     int count = run_detection(detector, fb->buf, fb->width, fb->height,
                              results, max_results);

     if (count > 0) {
         ESP_LOGI(TAG, "Detection completed, found %d objects", count);
         for (int i = 0; i < count; i++) {
             ESP_LOGI(TAG, "  Object %d: box(%d,%d,%d,%d) score=%.2f",
                      i+1, results[i].box.x, results[i].box.y,
                      results[i].box.w, results[i].box.h, results[i].score);
         }
     }

     return count;
 }

 void bsp_detection_deinit(detection_type_t type)
 {
     if (type == DETECTION_FACE && g_face_detector != NULL) {
         destroy_detector(g_face_detector);
         g_face_detector = NULL;
     } else if (type == DETECTION_PEDESTRIAN && g_pedestrian_detector != NULL) {
         destroy_detector(g_pedestrian_detector);
         g_pedestrian_detector = NULL;
     }
 }

 void bsp_detection_draw_results(uint16_t *framebuffer, int width, int height,
                                 detection_result_t *results, int count)
 {
     const uint16_t color_green = 0x07E0;  // RGB565 绿色
     const uint16_t color_red = 0xF800;    // RGB565 红色
     const int thickness = 2;              // 线条粗细

     for (int i = 0; i < count; i++) {
         detection_result_t *res = &results[i];
         uint16_t color = (res->score > 0.7f) ? color_green : color_red;

         int x1 = res->box.x;
         int y1 = res->box.y;
         int x2 = res->box.x + res->box.w;
         int y2 = res->box.y + res->box.h;

         // 边界检查
         if (x1 < 0) x1 = 0;
         if (y1 < 0) y1 = 0;
         if (x2 >= width) x2 = width - 1;
         if (y2 >= height) y2 = height - 1;

         // 绘制矩形框（加粗）
         for (int t = 0; t < thickness; t++) {
             // 上下边
             for (int x = x1; x <= x2; x++) {
                 if (y1 + t < height) framebuffer[(y1 + t) * width + x] = color;
                 if (y2 - t >= 0) framebuffer[(y2 - t) * width + x] = color;
             }
             // 左右边
             for (int y = y1; y <= y2; y++) {
                 if (x1 + t < width) framebuffer[y * width + (x1 + t)] = color;
                 if (x2 - t >= 0) framebuffer[y * width + (x2 - t)] = color;
             }
         }

         // 绘制关键点（仅人脸检测）
         for (int k = 0; k < 5; k++) {
             int kx = res->keypoints[k].x;
             int ky = res->keypoints[k].y;
             if (kx > 0 && ky > 0 && kx < width && ky < height) {
                 // 绘制小圆点
                 for (int dy = -2; dy <= 2; dy++) {
                     for (int dx = -2; dx <= 2; dx++) {
                         int px = kx + dx;
                         int py = ky + dy;
                         if (px >= 0 && px < width && py >= 0 && py < height) {
                             framebuffer[py * width + px] = 0xFFFF; // 白色
                         }
                     }
                 }
             }
         }
     }
 }