# -*- coding: utf-8 -*-
"""
Classifier is an image classifier specialization of Net.
"""

import numpy as np

import caffe

import sys
sys.dont_write_bytecode = True

class Classifier(caffe.Net):
    """
    Classifier extends Net for image class prediction
    by scaling, center cropping, or oversampling.

    Parameters
    ----------
    image_dims : dimensions to scale input for cropping/sampling.
        Default is to scale to net input size for whole-image crop.
    mean, input_scale, raw_scale, channel_swap: params for
        preprocessing options.
    """
    def __init__(self, model_file, pretrained_file, image_dims=None,
                 mean=None, input_scale=None, raw_scale=None,
                 channel_swap=None):
        # 使用caffe的net基类初始化
        caffe.Net.__init__(self, model_file, pretrained_file, caffe.TEST)

        # configure pre-processing
        # 设置预处理参数
        # in_：输入blob
        in_ = self.inputs[0]
        # blobs[...]通过名称索引blob
        # 定义transformer输入和尺寸
        self.transformer = caffe.io.Transformer(
            {in_: self.blobs[in_].data.shape})
        # 维度变化
        self.transformer.set_transpose(in_, (2, 0, 1))
        # 设置mean
        if mean is not None:
            self.transformer.set_mean(in_, mean)
        # 设置scale
        if input_scale is not None:
            self.transformer.set_input_scale(in_, input_scale)
        # 设置raw_scale
        if raw_scale is not None:
            self.transformer.set_raw_scale(in_, raw_scale)
        # 设置通道交换
        if channel_swap is not None:
            self.transformer.set_channel_swap(in_, channel_swap)
        #
        self.crop_dims = np.array(self.blobs[in_].data.shape[2:])
        # for num,i in enumerate(self.blobs):
            # print num, " : ",i
        if not image_dims:
            image_dims = self.crop_dims
        self.image_dims = image_dims

    def predict(self, inputs, oversample=True):
        """
        Predict classification probabilities of inputs.

        Parameters
        ----------
        inputs : iterable of (H x W x K) input ndarrays.
        oversample : boolean
            average predictions across center, corners, and mirrors
            when True (default). Center-only prediction when False.

        Returns
        -------
        predictions: (N x C) ndarray of class probabilities for N images and C
            classes.
        """
        # Scale to standardize input dimensions.
        input_ = np.zeros((len(inputs),
                           self.image_dims[0],
                           self.image_dims[1],
                           inputs[0].shape[2]),
                          dtype=np.float32)
        for ix, in_ in enumerate(inputs):
            input_[ix] = caffe.io.resize_image(in_, self.image_dims)

        if oversample:
            # Generate center, corner, and mirrored crops.
            input_ = caffe.io.oversample(input_, self.crop_dims)
        else:
            # Take center crop.
            center = np.array(self.image_dims) / 2.0
            crop = np.tile(center, (1, 2))[0] + np.concatenate([
                -self.crop_dims / 2.0,
                self.crop_dims / 2.0
            ])
            crop = crop.astype(int)
            input_ = input_[:, crop[0]:crop[2], crop[1]:crop[3], :]

        # Classify
        caffe_in = np.zeros(np.array(input_.shape)[[0, 3, 1, 2]],
                            dtype=np.float32)
        for ix, in_ in enumerate(input_):
            caffe_in[ix] = self.transformer.preprocess(self.inputs[0], in_)
        out = self.forward_all(**{self.inputs[0]: caffe_in})
        predictions = out[self.outputs[0]]

        # For oversampling, average predictions across crops.
        if oversample:
            predictions = predictions.reshape((len(predictions) / 10, 10, -1))
            predictions = predictions.mean(1)

        return predictions