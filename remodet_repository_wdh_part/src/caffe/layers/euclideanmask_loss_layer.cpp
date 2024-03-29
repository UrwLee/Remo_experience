#include <vector>

#include "caffe/layers/euclideanmask_loss_layer.hpp"
#include "caffe/util/math_functions.hpp"

namespace caffe {

template <typename Dtype>
void EuclideanmaskLossLayer<Dtype>::Reshape(
  const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top) {
  LossLayer<Dtype>::Reshape(bottom, top);
  CHECK_EQ(bottom[0]->count(1), bottom[1]->count(1))
      << "Inputs must have the same dimension.";
  diff_.ReshapeLike(*bottom[0]);
}

template <typename Dtype>
void EuclideanmaskLossLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {
  int count_per_ch = bottom[0]->shape()[2] * bottom[0]->shape()[3];
  int batch = bottom[0]->shape()[0];
  // channel = num_part + 1
  int channel = bottom[0]->shape()[1];
  Dtype loss = 0;
  for(int n=0; n<batch; n++){
    for(int c=0; c<channel; c++){
      const Dtype* mask_this = bottom[2]->cpu_data() + (n*channel + c);
      // c == channel-1 -> bkg
      // used to ignore some parts, i.e., ears / nose for difference of MPII/COCO
      Dtype mask = (c!=channel-1) ? *mask_this : 1;
      if(mask > 0.5){
        caffe_sub(
            count_per_ch,
            bottom[0]->cpu_data() + (n*channel + c)*count_per_ch,
            bottom[1]->cpu_data() + (n*channel + c)*count_per_ch,
            diff_.mutable_cpu_data() + (n*channel + c)*count_per_ch);
        Dtype dot = caffe_cpu_dot(count_per_ch,
                                  diff_.cpu_data() + (n*channel + c)*count_per_ch,
                                  diff_.cpu_data() + (n*channel + c)*count_per_ch);

        loss += dot / bottom[0]->num() / Dtype(2);
      }
    }
  }
  top[0]->mutable_cpu_data()[0] = loss;
}

template <typename Dtype>
void EuclideanmaskLossLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {

  int count_per_ch = bottom[0]->shape()[2] * bottom[0]->shape()[3];
  int batch = bottom[0]->shape()[0];
  int channel = bottom[0]->shape()[1];

  // backpropagate to preds and gts
  for (int i = 0; i < 2; ++i) {
    if (propagate_down[i]) {
      const Dtype sign = (i == 0) ? 1 : -1;

      for(int n=0; n<batch; n++){
        for(int c=0; c<channel; c++){
          const Dtype* mask_this = bottom[2]->cpu_data() + (n*channel + c);
          Dtype mask = (c!=channel-1) ? *mask_this : 1;
          const Dtype alpha = (mask > 0.5 ? Dtype(1) : Dtype(0)) * sign * top[0]->cpu_diff()[0] / bottom[i]->num();

          caffe_cpu_axpby(
              count_per_ch,                                     // count
              alpha,                                            // alpha
              diff_.cpu_data() + (n*channel + c)*count_per_ch,  // X
              Dtype(0),                                         // beta
              bottom[i]->mutable_cpu_diff() + (n*channel + c)*count_per_ch); 
        }
      }
    }
  }
}

#ifdef CPU_ONLY
STUB_GPU(EuclideanmaskLossLayer);
#endif

INSTANTIATE_CLASS(EuclideanmaskLossLayer);
REGISTER_LAYER_CLASS(EuclideanmaskLoss);

}  // namespace caffe
