#include "human_face_detect.hpp"
 #include "pedestrian_detect.hpp"
 #include "detection.h"
 #include "esp_log.h"
 #include <list>

 static const char *TAG = "detection_bridge";

 extern "C" {

 void* create_face_detector(void)
 {
     try {
         ESP_LOGI(TAG, "Creating HumanFaceDetect instance");
         return new HumanFaceDetect();
     } catch (const std::exception &e) {
         ESP_LOGE(TAG, "Exception creating face detector: %s", e.what());
         return nullptr;
     } catch (...) {
         ESP_LOGE(TAG, "Unknown exception creating face detector");
         return nullptr;
     }
 }

 void* create_pedestrian_detector(void)
 {
     try {
         ESP_LOGI(TAG, "Creating PedestrianDetect instance");
         return new PedestrianDetect();
     } catch (const std::exception &e) {
         ESP_LOGE(TAG, "Exception creating pedestrian detector: %s", e.what());
         return nullptr;
     } catch (...) {
         ESP_LOGE(TAG, "Unknown exception creating pedestrian detector");
         return nullptr;
     }
 }

 int run_detection(void *detector, uint8_t *img_data, int width, int height,
                   detection_result_t *results, int max_results)
 {
     if (!detector || !img_data || !results) {
         ESP_LOGE(TAG, "Invalid parameters for detection");
         return 0;
     }

     try {
         // 准备图像结构
         dl::image::img_t img;
         img.data = img_data;
         img.width = width;
         img.height = height;
         img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565;

         // 执行检测
         std::list<dl::detect::result_t> *detect_results = nullptr;

         // 尝试作为人脸检测器
         HumanFaceDetect *face_det = static_cast<HumanFaceDetect*>(detector);
         if (face_det) {
             detect_results = &(face_det->run(img));
         } else {
             // 尝试作为行人检测器
             PedestrianDetect *ped_det = static_cast<PedestrianDetect*>(detector);
             if (ped_det) {
                 detect_results = &(ped_det->run(img));
             }
         }

         if (!detect_results) {
             ESP_LOGW(TAG, "No detection results");
             return 0;
         }

         // 转换结果
         int count = 0;
         for (auto &res : *detect_results) {
             if (count >= max_results) break;

             results[count].box.x = res.box[0];
             results[count].box.y = res.box[1];
             results[count].box.w = res.box[2] - res.box[0];
             results[count].box.h = res.box[3] - res.box[1];
             results[count].score = res.score;

             // 复制关键点（如果有）
             if (res.keypoint.size() >= 10) {
                 for (int i = 0; i < 5; i++) {
                     results[count].keypoints[i].x = res.keypoint[i * 2];
                     results[count].keypoints[i].y = res.keypoint[i * 2 + 1];
                 }
             } else {
                 // 清空关键点
                 for (int i = 0; i < 5; i++) {
                     results[count].keypoints[i].x = 0;
                     results[count].keypoints[i].y = 0;
                 }
             }

             count++;
         }

         return count;

     } catch (const std::exception &e) {
         ESP_LOGE(TAG, "Exception during detection: %s", e.what());
         return 0;
     } catch (...) {
         ESP_LOGE(TAG, "Unknown exception during detection");
         return 0;
     }
 }

 void destroy_detector(void *detector)
 {
     if (detector) {
         try {
             // 尝试删除人脸检测器
             HumanFaceDetect *face_det = static_cast<HumanFaceDetect*>(detector);
             if (face_det) {
                 delete face_det;
                 return;
             }

             // 尝试删除行人检测器
             PedestrianDetect *ped_det = static_cast<PedestrianDetect*>(detector);
             if (ped_det) {
                 delete ped_det;
             }
         } catch (...) {
             ESP_LOGE(TAG, "Exception destroying detector");
         }
     }
 }

 } // extern "C"