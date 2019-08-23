#ifndef CAFFE_MINIHAND_DATA_TRANSFORMER_HPP_
#define CAFFE_MINIHAND_DATA_TRANSFORMER_HPP_

#include <vector>
#include <opencv2/core/core.hpp>
#include <opencv2/opencv.hpp>
using namespace cv;

#include "caffe/blob.hpp"
#include "caffe/common.hpp"
#include "caffe/proto/caffe.pb.h"
#include "caffe/tracker/bounding_box.hpp"
#include "caffe/mask/bbox_func.hpp"
#include "caffe/mask/unified_data_transformer.hpp"

namespace caffe {

template <typename Dtype>
struct HandAnnoData {
  string image_path;  // 图片路径
  int image_width;    // 图片长宽
  int image_height;
  int num_hands;      // 数量
  vector<LabeledBBox<Dtype> > hands;  // 定义
};

template <typename Dtype>
class MinihandTransformer {
 public:
  // 构造函数
  explicit MinihandTransformer(const MinihandTransformationParameter& param, Phase phase)
      : param_(param), phase_(phase) {}
  virtual ~MinihandTransformer() {}

  // anno -> 输入标注
  // image -> 返回图像
  // bboxes -> 返回的Gt-Boxes
  // bool -> 处理成功，返回true，否则返回false
  bool Transform(HandAnnoData<Dtype>& anno, cv::Mat* image, vector<LabeledBBox<Dtype> >* bboxes);
  // 处理裁剪问题
  void transCrop(BoundingBox<Dtype>& crop_roi, HandAnnoData<Dtype>& anno);
  // 裁剪成功返回true，否则返回false
  bool getCropBBox(HandAnnoData<Dtype>& anno, BoundingBox<Dtype>* crop_roi, Phase phase);

  MinihandTransformationParameter param_;
  Phase phase_;
};

}

#endif
