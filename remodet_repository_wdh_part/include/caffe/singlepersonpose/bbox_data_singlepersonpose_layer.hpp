#ifndef CAFFE_MASK_BBOX_DATA_SINGLEPERSONPOSE_LAYER_HPP_
#define CAFFE_MASK_BBOX_DATA_SINGLEPERSONPOSE_LAYER_HPP_

#include <vector>

#include "caffe/blob.hpp"
#include "caffe/singlepersonpose/bbox_data_transformer_singlepersonpose.hpp"
#include "caffe/mask/anno_image_loader.hpp"
#include "caffe/pose/pose_image_loader.hpp"
#include "caffe/internal_thread.hpp"
#include "caffe/layer.hpp"
#include "caffe/layers/base_data_layer.hpp"
#include "caffe/proto/caffe.pb.h"
#include "caffe/mask/bbox_func.hpp"

namespace caffe {
template <typename Dtype>
class BBoxDataSinglePersonPoseLayer : public BasePrefetchingDataLayer<Dtype> {
 public:
  explicit BBoxDataSinglePersonPoseLayer(const LayerParameter& param)
      : BasePrefetchingDataLayer<Dtype>(param), bbox_data_transform_param_(param.unified_data_transform_param()) {}
  virtual ~BBoxDataSinglePersonPoseLayer();
  virtual void DataLayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  virtual inline const char* type() const { return "BBoxDataSinglePersonPose"; }
  virtual inline int ExactNumBottomBlobs() const { return 0; }

  virtual inline int ExactNumTopBlobs() const { return 2; }

 protected:
  //  prefetch随机数
  shared_ptr<Caffe::RNG> prefetch_rng_;

  // 输入样本顺序随机乱序
  virtual void ShuffleLists();

  // 载入一个minibatch
  virtual void load_batch(Batch<Dtype>* batch);

  void ReadAnnoDataFromXml(const int bindex, const string& xml_file, const string& root_dir,
                           AnnoData<Dtype>* anno);
  // 所有XML文件的集合列表 root<--->path pair
  vector<pair<std::string, std::string> > lines_;

  // 当前加载的XML编号
  int lines_id_;
  // unused.
  Blob<Dtype> transformed_label_;

  // 数据转换器参数
  UnifiedTransformationParameter bbox_data_transform_param_;

  // 数据转换器
  shared_ptr<BBoxDataSinglePersonPoseTransformer<Dtype> > bbox_data_transformer_;

  // RGB颜色通道均值
  vector<Dtype> mean_values_;

  // Add_parts
  bool Add_parts_;
  int num_parts_ = 18;
  int num_limbs_ = 17;
};

}

#endif
