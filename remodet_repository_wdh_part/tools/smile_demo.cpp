// if not use OPENCV, note it.
#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
// if not use, note it.
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
// Google log & flags
#include "gflags/gflags.h"
#include "glog/logging.h"
// caffe
#include "caffe/proto/caffe.pb.h"
#include "caffe/caffe.hpp"
// remo, note the useless classes.
#include "caffe/remo/remo_front_visualizer.hpp"
#include "caffe/remo/net_wrap.hpp"
#include "caffe/remo/frame_reader.hpp"
#include "caffe/remo/data_frame.hpp"
#include "caffe/remo/basic.hpp"
#include "caffe/remo/res_frame.hpp"
#include "caffe/remo/visualizer.hpp"

#include "caffe/mask/bbox_func.hpp"

#include "caffe/tracker/basic.hpp"
#include <boost/foreach.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include "caffe/det/detwrap.hpp"
#include "caffe/smile/smile_net.hpp"

using namespace std;
using namespace caffe;
using std::string;
using std::vector;
namespace bfs = boost::filesystem;

int main(int nargc, char** args) {
   // ################################ NETWORK ################################
   // network input
   int resized_width = 512;
   int resized_height = 288;
   // Network config
   const std::string network_proto = "/home/ethan/ForZhangM/ReleaseTMP_20180124_PerFaceHeadHand_Det/test_2SSD-MA2-OHEM-PLA-LLR_PersonFaceHeadHand.prototxt";
   const std::string caffe_model = "/home/ethan/ForZhangM/ReleaseTMP_20180124_PerFaceHeadHand_Det/ResNetPoseDet_JointTrain_I_L_WithFaceHeadHand_1H_iter_240000.caffemodel";
   // Smile Net
  //  const std::string smile_network_proto = "/home/zhangming/Models/Results/SmileNet/ResCNN_Base_V2-I96-1-1-3FC/Proto/test_copy.prototxt";
  //  const std::string smile_network_model = "/home/zhangming/Models/Results/SmileNet/ResCNN_Base_V2-I96-1-1-3FC/Models/ResCNN_Base_V2-I96-1-1-3FC_iter_300000.caffemodel";
  //  const std::string smile_network_proto = "/home/zhangming/Models/Results/SmileNet/ResCNN_Base_V1-I96-1-1/Proto/test_copy.prototxt";
  //  const std::string smile_network_model = "/home/zhangming/Models/Results/SmileNet/ResCNN_Base_V1-I96-1-1/Models/ResCNN_Base_V1-I96-1-1_iter_20000.caffemodel";
   const std::string smile_network_proto = "/home/ethan/Models/Results/SmileNet/CNN_Base_V1-I64/test_copy.prototxt";
   const std::string smile_network_model = "/home/ethan/Models/Results/SmileNet/CNN_Base_V1-I64/CNN_Base_V1-I64_iter_300000.caffemodel";

   // GPU
   int gpu_id = 0;
   bool mode = true;  // use GPU
   // features
   const std::string proposals = "det_out";
   // display Size
   int max_dis_size = 1280;
   // active labels
   vector<int> active_label;
   active_label.push_back(3);
   // ################################ DATA ####################################
   // CAMERA
   const bool use_camera = true; // 0
   const int cam_width = 1280;
   const int cam_height = 720;
   // ################################ MAIN LOOP ################################
   // det_warpper
   caffe::DetWrapper<float> det_wrapper(network_proto,caffe_model,mode,gpu_id,proposals,max_dis_size);
   caffe::SmileNetWrapper smile_wrapper(smile_network_proto,smile_network_model);

   //  CAMERA
   if (use_camera) {
     cv::VideoCapture cap;
     if (!cap.open(0)) {
       LOG(FATAL) << "Failed to open webcam: " << 0;
     }
     cap.set(CV_CAP_PROP_FRAME_WIDTH, cam_width);
     cap.set(CV_CAP_PROP_FRAME_HEIGHT, cam_height);
     cv::Mat cv_img;
     cap >> cv_img;
     int count = 0;
     CHECK(cv_img.data) << "Could not load image.";
     while (1) {
       ++count;
       cv::Mat image;
       cap >> image;
       caffe::DataFrame<float> data_frame(count, image, resized_width, resized_height);
       // 绘制主网络的结果
       vector<LabeledBBox<float> > rois;
       det_wrapper.get_rois(data_frame, &rois);
       for (int i = 0; i < rois.size(); ++i) {
         if (rois[i].cid != 3) continue;  // Face
         rois[i].bbox.clip();
         float score;
         const float w = rois[i].bbox.get_width() * image.cols;
         const float h = rois[i].bbox.get_height() * image.rows;
         const float ratio = w / h;
         const bool rflag = ratio > 0.75 && ratio < 1.33;
         bool flag = smile_wrapper.is_smile(image, rois[i].bbox, &score) && rflag;
         LOG(INFO) << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>SCORE: " << score;
         // 笑脸绘制绿色框,否则绘制红色框
         const cv::Point point1(rois[i].bbox.x1_ * image.cols, rois[i].bbox.y1_ * image.rows);
         const cv::Point point2(rois[i].bbox.x2_ * image.cols, rois[i].bbox.y2_ * image.rows);
         int r = 255;
         int g = 0;
         int b = 0;
         if (flag) {
           r = 0; g = 255;
         }
         const cv::Scalar box_color(b, g, r);
         const int thickness = 2;
         cv::rectangle(image, point1, point2, box_color, thickness);
         // 显示score
         char tmp_str[256];
         snprintf(tmp_str, 256, "%.3f", score);
         cv::putText(image, tmp_str, cv::Point(rois[i].bbox.x1_ * image.cols, rois[i].bbox.y1_ * image.rows + 20),
            cv::FONT_HERSHEY_SIMPLEX, 0.75, cv::Scalar(255,0,0), 1);
       }
       cv::namedWindow("Smile", cv::WINDOW_AUTOSIZE);
       cv::imshow( "Smile", image);
       cv::waitKey(1);
     }
   }
   LOG(INFO) << "Finished.";
   return 0;
 }
