#include <algorithm>
#include <vector>
#include "caffe/filler.hpp"
#include "caffe/layers/conv_dw_layer.hpp"

namespace caffe {

template <typename Dtype>
void ConvolutionDepthwiseLayer<Dtype>::LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  ConvolutionParameter conv_param = this->layer_param_.convolution_param();
  if (conv_param.has_kernel_h() && conv_param.has_kernel_w()) {
    kernel_h_ = conv_param.kernel_h();
    kernel_w_ = conv_param.kernel_w();
  } else {
    if (conv_param.kernel_size_size() == 1) {
      kernel_h_ = conv_param.kernel_size(0);
      kernel_w_ = conv_param.kernel_size(0);
    } else {
      kernel_h_ = conv_param.kernel_size(0);
      kernel_w_ = conv_param.kernel_size(1);
    }
  }
  if (conv_param.has_stride_h() && conv_param.has_stride_w()) {
    stride_h_ = conv_param.stride_h();
    stride_w_ = conv_param.stride_w();
  } else {
    if (conv_param.stride_size() == 1) {
      stride_h_ = conv_param.stride(0);
      stride_w_ = conv_param.stride(0);
    } else {
      stride_h_ = conv_param.stride(0);
      stride_w_ = conv_param.stride(1);
    }
  }
  if (conv_param.has_pad_h() && conv_param.has_pad_w()) {
    pad_h_ = conv_param.pad_h();
    pad_w_ = conv_param.pad_w();
  } else {
    if (conv_param.pad_size() == 1) {
      pad_h_ = conv_param.pad(0);
      pad_w_ = conv_param.pad(0);
    } else {
      pad_h_ = conv_param.pad(0);
      pad_w_ = conv_param.pad(1);
    }
  }
  if (conv_param.dilation_size() > 0) {
    if (conv_param.dilation_size() == 1) {
      dilation_h_ = conv_param.dilation(0);
      dilation_w_ = conv_param.dilation(0);
    } else {
      dilation_h_ = conv_param.dilation(0);
      dilation_w_ = conv_param.dilation(1);
    }
  } else {
    dilation_h_ = 1;
    dilation_w_ = 1;
  }
  group_ = conv_param.group();
  num_output_ = conv_param.num_output();
  CHECK_EQ(num_output_ % group_, 0);
  CHECK_EQ(bottom[0]->channels() % group_, 0);
  vector<int> weight_shape(4);
  weight_shape[0] = num_output_;
  weight_shape[1] = bottom[0]->channels() / group_;
  weight_shape[2] = kernel_h_;
  weight_shape[3] = kernel_w_;
  vector<int> bias_shape;
  if (conv_param.bias_term()) {
    bias_shape.push_back(num_output_);
  }
  if (this->blobs_.size() == 0) {
    if (conv_param.bias_term()) {
      this->blobs_.resize(2);
    } else {
      this->blobs_.resize(1);
    }
    this->blobs_[0].reset(new Blob<Dtype>(weight_shape));
    shared_ptr<Filler<Dtype> > weight_filler(GetFiller<Dtype>(conv_param.weight_filler()));
    weight_filler->Fill(this->blobs_[0].get());
    if (conv_param.bias_term()) {
      this->blobs_[1].reset(new Blob<Dtype>(bias_shape));
      shared_ptr<Filler<Dtype> > bias_filler(GetFiller<Dtype>(conv_param.bias_filler()));
      bias_filler->Fill(this->blobs_[1].get());
    }
  }
  this->param_propagate_down_.resize(this->blobs_.size(), true);

  // rsvd
  vector<int> top_shape;
  top_shape.push_back(bottom[0]->num());
  top_shape.push_back(num_output_);
  top_shape.push_back((bottom[0]->height() + 2 * pad_h_ - (dilation_h_ * (kernel_h_ - 1) + 1)) / stride_h_ + 1);
  top_shape.push_back((bottom[0]->width() + 2 * pad_w_ - (dilation_w_ * (kernel_w_ - 1) + 1)) / stride_w_ + 1);
  top[0]->Reshape(top_shape);
  // buffer
  // [No,Nc/g,D,D,N,H,W]
  vector<int> weight_buffer_shape;
  weight_buffer_shape.push_back(num_output_);
  weight_buffer_shape.push_back(bottom[0]->channels() / group_);
  weight_buffer_shape.push_back(kernel_h_);
  weight_buffer_shape.push_back(kernel_w_);
  weight_buffer_shape.push_back(bottom[0]->num());
  weight_buffer_shape.push_back(top[0]->height());
  weight_buffer_shape.push_back(top[0]->width());
  weight_buffer_.Reshape(weight_buffer_shape);
  // [N,H,W]
  vector<int> weight_multiplier_shape;
  weight_multiplier_shape.push_back(bottom[0]->num());
  weight_multiplier_shape.push_back(top[0]->height());
  weight_multiplier_shape.push_back(top[0]->width());
  weight_multiplier_.Reshape(weight_multiplier_shape);
  caffe_gpu_set(weight_multiplier_.count(), Dtype(1), weight_multiplier_.mutable_gpu_data());
  if (this->layer_param_.convolution_param().bias_term())
  {
    // [Nc, N, H, W]
    vector<int> bias_buffer_shape;
    bias_buffer_shape.push_back(bottom[0]->channels());
    bias_buffer_shape.push_back(bottom[0]->num());
    bias_buffer_shape.push_back(top[0]->height());
    bias_buffer_shape.push_back(top[0]->width());
    bias_buffer_.Reshape(bias_buffer_shape);
    // [N,H,W]
    vector<int> bias_multiplier_shape;
    bias_multiplier_shape.push_back(bottom[0]->num());
    bias_multiplier_shape.push_back(top[0]->height());
    bias_multiplier_shape.push_back(top[0]->width());
    bias_multiplier_.Reshape(bias_multiplier_shape);
    // 1
    caffe_gpu_set(bias_multiplier_.count(), Dtype(1), bias_multiplier_.mutable_gpu_data());
  }
}

template <typename Dtype>
void ConvolutionDepthwiseLayer<Dtype>::Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  vector<int> top_shape;
  top_shape.push_back(bottom[0]->num());
  top_shape.push_back(num_output_);
  top_shape.push_back((bottom[0]->height() + 2 * pad_h_ - (dilation_h_ * (kernel_h_ - 1) + 1)) / stride_h_ + 1);
  top_shape.push_back((bottom[0]->width() + 2 * pad_w_ - (dilation_w_ * (kernel_w_ - 1) + 1)) / stride_w_ + 1);
  top[0]->Reshape(top_shape);
}

template <typename Dtype>
void ConvolutionDepthwiseLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  const int num = top[0]->num();
  const int channels = top[0]->channels();
  const int top_height = top[0]->height();
  const int top_width = top[0]->width();
  const int bottom_height = bottom[0]->height();
  const int bottom_width = bottom[0]->width();
  const int bottom_channels = bottom[0]->channels();
  const int bottom_spatial_size = bottom_height * bottom_width;
  const int filter_inner_dim = kernel_h_ * kernel_w_ * bottom_channels / group_;
  const int input_group_channels = bottom_channels / group_;
  const int output_group_channels = channels / group_;
  const int bottom_group_spatial_size = bottom_spatial_size * input_group_channels;
  const Dtype* bottom_data_base = bottom[0]->cpu_data();
  const Dtype* weight_data_base = this->blobs_[0]->cpu_data();
  Dtype* top_data = top[0]->mutable_cpu_data();
  for (int n = 0; n < num; ++n) {
    for (int c = 0; c < channels; ++c) {
      for (int h = 0; h < top_height; ++h) {
        for (int w = 0; w < top_width; ++w) {
          // current weights: [Nc/g,D,D]
          const Dtype* weight_data = weight_data_base + c * filter_inner_dim;
          // current group
          const int cg = c / output_group_channels;
          // current bottom_data [N, Nc, IH, IW]
          const Dtype* bottom_data = bottom_data_base + bottom[0]->offset(n) + cg * bottom_group_spatial_size;
          Dtype value = 0;
          // compute over input_group_channels
          for (int g = 0; g < input_group_channels; ++g) {
            for (int kh = 0; kh < kernel_h_; ++kh) {
              for (int kw = 0; kw < kernel_w_; ++kw) {
                int h_in = -pad_h_ + h * stride_h_ + kh * dilation_h_;
                int w_in = -pad_w_ + w * stride_w_ + kw * dilation_w_;
                if ((h_in >= 0) && (h_in < bottom_height) && (w_in >= 0) && (w_in < bottom_width)) {
                  int offset = (g * bottom_height + h_in) * bottom_width + w_in;
                  value += (*weight_data) * bottom_data[offset];
                }
                ++weight_data;
              }
            }
          }
          *top_data = value;
          top_data++;
        }
      }
    }
  }
  if (this->layer_param_.convolution_param().bias_term()) {
    top_data = top[0]->mutable_cpu_data();
    const Dtype* bias_data = this->blobs_[1]->cpu_data();
    for (int n = 0; n < num; ++n) {
      for (int c = 0; c < channels; ++c) {
        for (int h = 0; h < top_height; ++h) {
          for (int w = 0; w < top_width; ++w) {
            *top_data += bias_data[c];
            ++top_data;
          }
        }
      }
    }
  }
}

template <typename Dtype>
void ConvolutionDepthwiseLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {
  const int num = top[0]->num();
  const int channels = top[0]->channels();
  const int top_height = top[0]->height();
  const int top_width = top[0]->width();
  const int bottom_height = bottom[0]->height();
  const int bottom_width = bottom[0]->width();
  const int bottom_channels = bottom[0]->channels();
  const int bottom_spatial_size = bottom_height * bottom_width;
  const int filter_inner_dim = kernel_h_ * kernel_w_ * bottom_channels / group_;
  const int input_group_channels = bottom_channels / group_;
  const int output_group_channels = channels / group_;
  const int bottom_group_spatial_size = bottom_spatial_size * input_group_channels;
  caffe_set(bottom[0]->count(), Dtype(0), bottom[0]->mutable_cpu_diff());
  // bias
  if (this->layer_param_.convolution_param().bias_term() && this->param_propagate_down_[1]) {
    const Dtype* top_diff = top[0]->cpu_diff();
    Dtype* bias_diff = this->blobs_[1]->mutable_cpu_diff();
    for (int n = 0; n < num; ++n) {
      for (int c = 0; c < channels; ++c) {
        for (int h = 0; h < top_height; ++h) {
          for (int w = 0; w < top_width; ++w) {
            bias_diff[c] += *top_diff;
            ++top_diff;
          }
        }
      }
    }
  }
  // weights
  if (this->param_propagate_down_[0]) {
    const Dtype* top_diff = top[0]->cpu_diff();
    const Dtype* bottom_data_base = bottom[0]->cpu_data();
    Dtype* weight_diff_base = this->blobs_[0]->mutable_cpu_diff();
    for (int n = 0; n < num; ++n) {
      for (int c = 0; c < channels; ++c) {
        for (int h = 0; h < top_height; ++h) {
          for (int w = 0; w < top_width; ++w) {
            // [Nc/g,D,D]
            Dtype* weight_diff = weight_diff_base + c * filter_inner_dim;
            // current group
            const int cg = c / output_group_channels;
            // current bottom_data [N, Nc, IH, IW]
            const Dtype* bottom_data = bottom_data_base + bottom[0]->offset(n) + cg * bottom_group_spatial_size;
            // collect diff over input_group_channels
            for (int g = 0; g < input_group_channels; ++g) {
              for (int kh = 0; kh < kernel_h_; ++kh) {
                for (int kw = 0; kw < kernel_w_; ++kw) {
                  int h_in = -pad_h_ + h * stride_h_ + kh * dilation_h_;
                  int w_in = -pad_w_ + w * stride_w_ + kw * dilation_w_;
                  if ((h_in >= 0) && (h_in < bottom_height) && (w_in >= 0) && (w_in < bottom_width)) {
                    int offset = (g * bottom_height + h_in) * bottom_width + w_in;
                    *weight_diff += bottom_data[offset] * (*top_diff);
                  }
                  ++weight_diff;
                }
              }
            }
            ++top_diff;
          }
        }
      }
    }
  }
  // input
  if (propagate_down[0]) {
    const Dtype* top_diff = top[0]->cpu_diff();
    const Dtype* weight_data_base = this->blobs_[0]->cpu_data();
    Dtype* bottom_diff_base = bottom[0]->mutable_cpu_diff();
    for (int n = 0; n < num; ++n) {
      for (int c = 0; c < channels; ++c) {
        for (int h = 0; h < top_height; ++h) {
          for (int w = 0; w < top_width; ++w) {
            // [Nc/g,D,D]
            const Dtype* weight_data = weight_data_base + c * filter_inner_dim;
            // current group
            const int cg = c / output_group_channels;
            // current bottom_diff [N, Nc, IH, IW]
            Dtype* bottom_diff = bottom_diff_base + bottom[0]->offset(n) + cg * bottom_group_spatial_size;
            for (int g = 0; g < input_group_channels; ++g) {
              for (int kh = 0; kh < kernel_h_; ++kh) {
                for (int kw = 0; kw < kernel_w_; ++kw) {
                  int h_in = -pad_h_ + h * stride_h_ + kh * dilation_h_;
                  int w_in = -pad_w_ + w * stride_w_ + kw * dilation_w_;
                  if ((h_in >= 0) && (h_in < bottom_height) && (w_in >= 0) && (w_in < bottom_width)) {
                    int offset = (g * bottom_height + h_in) * bottom_width + w_in;
                    bottom_diff[offset] += (*weight_data) * (*top_diff);
                  }
                  ++weight_data;
                }
              }
            }
            ++top_diff;
          }
        }
      }
    }
  }
}

#ifdef CPU_ONLY
STUB_GPU(ConvolutionDepthwiseLayer);
#endif

INSTANTIATE_CLASS(ConvolutionDepthwiseLayer);
REGISTER_LAYER_CLASS(ConvolutionDepthwise);

}  // namespace caffe
