# -*- coding: utf-8 -*-
import os
import sys
import caffe
import math

sys.dont_write_bytecode = True

sys.path.append('../')

from caffe import layers as L
from caffe import params as P
from caffe.proto import caffe_pb2

from DetectHeaderLayer import *
from ConvBNLayer import *

from VggNet import VGG16Net
from ResNet import ResNet101Net, ResNet152Net, ResNet50Net
from PvaNet import PvaNet
from YoloNet import YoloNet
from GoogleNet import Google_IP_V3_Net
from MultiScaleLayer import *

from PyLib.LayerParam.MultiBoxLossLayerParam import *
from PyLib.LayerParam.DetectionOutLayerParam import *
from PyLib.LayerParam.DetectionEvalLayerParam import *
from PyLib.LayerParam.McBoxLossLayerParam import *
from PyLib.LayerParam.DetectionMcOutLayerParam import *

# 在BaseNet顶层添加额外的卷积层
# 3/1/1卷积核： 尺度不变
def AddTopExtraConvLayers(net, use_pool=False, use_batchnorm=True, num_layers=0, channels=512, feature_layers=[]):
    # Add additional convolutional layers.
    last_layer = net.keys()[-1]
    from_layer = last_layer

    if use_pool:
        poolname = "{}_pool".format(last_layer)
        net[poolname] = L.Pooling(net[from_layer], pool=P.Pooling.MAX, kernel_size=3, stride=2)
        from_layer = poolname

    use_relu = True
    if num_layers > 0:
        for i in range(0, num_layers):
            out_layer = "{}_extra_conv{}".format(last_layer, i+1)
            ConvBNUnitLayer(net, from_layer, out_layer, use_batchnorm, use_relu, channels, 3, 1, 1)
            from_layer = out_layer

    # 最后一层添加进层列表
    feature_layers.append(from_layer)

    return net, feature_layers

# 创建yolo检测器
def YoloDetector(net, train=True, data_layer="data", gt_label="label", \
                net_width=300, net_height=300, basenet="Res50", use_layers=2, \
                extra_top_layers=0, extra_top_depth=512, \
                visualize=False, extra_data="data", eval_enable=True, **yoloparam):
    """
    创建YOLO检测器。
    train: TRAIN /TEST
    data_layer/gt_label: 数据输入和label输入。
    net_width/net_height: 网络的输入尺寸
    basenet: "vgg"/"res101"/"res50"/pva
    yoloparam: yolo检测器使用的参数列表。
    """
    # BaseNetWork
    # 构建基础网络，选择特征Layer
    final_layer_channels = 0
    if basenet == "VGG":
        net = VGG16Net(net, from_layer=data_layer, need_fc=False)
        final_layer_channels = 512
        # conv4_3 -> 1/8
        # conv5_3 -> 1/16
        if use_layers == 2:
            base_feature_layers = ['conv5_3']
        elif use_layers == 3:
            base_feature_layers = ['conv4_3', 'conv5_3']
        else:
            base_feature_layers = []
        # define added layers onto the top-layer
        add_layers = extra_top_layers
        add_channels = extra_top_depth
        if add_layers > 0:
            final_layer_channels = add_channels
        net, feature_layers = AddTopExtraConvLayers(net, use_pool=True, \
            use_batchnorm=True, num_layers=add_layers, channels=add_channels, \
            feature_layers=base_feature_layers)
    elif basenet == "Res101":
        net = ResNet101Net(net, from_layer=data_layer, use_pool5=False)
        final_layer_channels = 2048
        # res3b3-> 1/8
        # res4b22 -> 1/16
        # res5c -> 1/32
        if use_layers == 2:
            base_feature_layers = ['res4b22']
        elif use_layers == 3:
            base_feature_layers = ['res3b3', 'res4b22']
        else:
            base_feature_layers = []
        # define added layers onto the top-layer
        add_layers = extra_top_layers
        add_channels = extra_top_depth
        if add_layers > 0:
            final_layer_channels = add_channels
        net, feature_layers = AddTopExtraConvLayers(net, use_pool=False, \
            use_batchnorm=True, num_layers=add_layers, channels=add_channels, \
            feature_layers=base_feature_layers)
    elif basenet == "Res50":
        net = ResNet50Net(net, from_layer=data_layer, use_pool5=False)
        final_layer_channels = 2048
        # res3d-> 1/8
        # res4f -> 1/16
        # res5c -> 1/32
        if use_layers == 2:
            base_feature_layers = ['res4f']
        elif use_layers == 3:
            base_feature_layers = ['res3d', 'res4f']
        else:
            base_feature_layers = []
        # define added layers onto the top-layer
        add_layers = extra_top_layers
        add_channels = extra_top_depth
        if add_layers > 0:
            final_layer_channels = add_channels
        net, feature_layers = AddTopExtraConvLayers(net, use_pool=False, \
            use_batchnorm=True, num_layers=add_layers, channels=add_channels, \
            feature_layers=base_feature_layers)
    elif basenet == "PVA":
        net = PvaNet(net, from_layer=data_layer)
        final_layer_channels = 384
        if use_layers == 2:
            base_feature_layers = ['conv5_1/incep/pre', 'conv5_4']
        elif use_layers == 3:
            base_feature_layers = ['conv4_1/incep/pre', 'conv5_1/incep/pre', 'conv5_4']
        else:
            base_feature_layers = ['conv5_4']
        # Note: we do not add extra top layers for pvaNet
        feature_layers = base_feature_layers
    elif basenet == "Yolo":
        net = YoloNet(net, from_layer=data_layer)
        final_layer_channels = 1024
        if use_layers == 2:
            base_feature_layers = ['conv5_5', 'conv6_6']
        elif use_layers == 3:
            base_feature_layers = ['conv4_3','conv5_5', 'conv6_6']
        else:
            base_feature_layers = ['conv6_6']
        # Note: we do not add extra top layers for YoloNet
        feature_layers = base_feature_layers
    else:
        raise ValueError("only VGG16, Res50/101, PVA and Yolo are supported in current version.")

    # concat the feature_layers
    num_layers = len(feature_layers)
    if num_layers == 1:
        tags = ["Ref"]
    elif num_layers == 2:
        tags = ["Down","Ref"]
        down_methods = [["Reorg"]]
    else:
        if basenet == "Yolo":
            tags = ["Down","Down","Ref"]
            down_methods = [["MaxPool","Reorg"],["Reorg"]]          
        else: 
            tags = ["Down","Ref","Up"]
            down_methods = [["Reorg"]]
    # if use VGG, Norm may be used.
    # the interlayers can also be used if needed.
    # upsampleChannels must be the channels of Layers added onto the top.
    UnifiedMultiScaleLayers(net,layers=feature_layers, tags=tags, \
                            unifiedlayer="msfMap", dnsampleMethod=down_methods, \
                            upsampleMethod="Deconv", \
                            upsampleChannels=final_layer_channels)
    # create yolo detector header
    mcbox_layers = McDetectorHeader(net, \
        num_classes=yoloparam.get("mcloss_num_classes", 1), \
        feature_layer="msfMap", \
        normalization=yoloparam.get("mcheader_normalization", -1), \
        use_batchnorm=yoloparam.get("mcheader_use_batchnorm", False), \
        boxsizes=yoloparam.get("mcloss_boxsizes", []), \
        aspect_ratios=yoloparam.get("mcloss_aspect_ratios", []), \
        pwidths=yoloparam.get("mcloss_pwidths", []), \
        pheights=yoloparam.get("mcloss_pheights", []), \
        inter_layer_channels=yoloparam.get("mcheader_inter_layer_channels", 0), \
        kernel_size=yoloparam.get("mcheader_kernel_size", 1), \
        pad=yoloparam.get("mcheader_pad", 0))
    if train == True:
        # create loss
        mcboxloss_param = get_mcboxloss_param( \
           num_classes=yoloparam.get("mcloss_num_classes", 1), \
           overlap_threshold=yoloparam.get("mcloss_overlap_threshold", 0.6), \
           use_prior_for_matching=yoloparam.get("mcloss_use_prior_for_matching", True), \
           use_prior_for_init=yoloparam.get("mcloss_use_prior_for_init", False), \
           use_difficult_gt=yoloparam.get("mcloss_use_difficult_gt", True), \
           rescore=yoloparam.get("mcloss_rescore", True), \
           clip=yoloparam.get("mcloss_clip", True), \
           iters=yoloparam.get("mcloss_iters", 0), \
           iter_using_bgboxes=yoloparam.get("mcloss_iter_using_bgboxes", 10000), \
           background_box_loc_scale=yoloparam.get("mcloss_background_box_loc_scale", 0.01), \
           object_scale=yoloparam.get("mcloss_object_scale", 5), \
           noobject_scale=yoloparam.get("mcloss_noobject_scale", 1), \
           class_scale=yoloparam.get("mcloss_class_scale", 1), \
           loc_scale=yoloparam.get("mcloss_loc_scale", 1), \
           boxsize=yoloparam.get("mcloss_boxsizes", []), \
           aspect_ratio=yoloparam.get("mcloss_aspect_ratios", []), \
           pwidth=yoloparam.get("mcloss_pwidths", []), \
           pheight=yoloparam.get("mcloss_pheights", []), \
           background_label_id=yoloparam.get("mcloss_background_label_id", 0), \
           code_loc_type=yoloparam.get("mcloss_code_type",P.McBoxLoss.SSD)
           )
        loss_param = get_loss_param(normalization=yoloparam.get("mcloss_normalization",P.Loss.NONE))
        mcbox_layers.append(net[gt_label])
        net["mcbox_loss"] = L.McBoxLoss(*mcbox_layers, \
                          mcbox_loss_param=mcboxloss_param, \
                          loss_param=loss_param, \
                          include=dict(phase=caffe_pb2.Phase.Value('TRAIN')), \
                          propagate_down=[True, True, False])
        return net
    else:
        # create conf softmax layer
        det_mc_out_param = get_detection_mc_out_param( \
            num_classes=yoloparam.get("mcloss_num_classes", 1), \
            conf_threshold=yoloparam.get("mcdetout_conf_threshold", 0.01), \
            nms_threshold=yoloparam.get("mcdetout_nms_threshold", 0.45), \
            clip=yoloparam.get("mcloss_clip", True), \
            boxsize_threshold=yoloparam.get("mcdetout_boxsize_threshold", 0.001), \
            top_k=yoloparam.get("mcdetout_top_k", 100), \
            boxsize=yoloparam.get("mcloss_boxsizes", []), \
            aspect_ratio=yoloparam.get("mcloss_aspect_ratios", []), \
            pwidth=yoloparam.get("mcloss_pwidths", []), \
            pheight=yoloparam.get("mcloss_pheights", []), \
            visualize=yoloparam.get("mcdetout_visualize", False), \
            visual_conf_threshold=yoloparam.get("mcdetout_visualize_conf_threshold", 0.5), \
            visual_size_threshold=yoloparam.get("mcdetout_visualize_size_threshold", 0), \
            display_maxsize=yoloparam.get("mcdetout_display_maxsize", 1000), \
            line_width=yoloparam.get("mcdetout_line_width", 4), \
            color=yoloparam.get("mcdetout_color", [[0,255,0]]),\
            code_loc_type = yoloparam.get("mcdetout_code_type",P.McBoxLoss.SSD) )
        if visualize:
            mcbox_layers.append(net[extra_data])
        net.detection_out = L.DetectionMcOutput(*mcbox_layers, \
	  		detection_mc_output_param=det_mc_out_param, \
	  		include=dict(phase=caffe_pb2.Phase.Value('TEST')))

        if not visualize and eval_enable:
            # create eval layer
            det_eval_param = get_detection_eval_param( \
                 num_classes=yoloparam.get("mcloss_num_classes", 1) + 1, \
                 background_label_id=0, \
                 evaluate_difficult_gt=yoloparam.get("deteval_evaluate_difficult_gt",False), \
                 boxsize_threshold=yoloparam.get("deteval_boxsize_threshold",[0,0.01,0.05,0.1,0.15,0.2,0.25]), \
                 iou_threshold=yoloparam.get("deteval_iou_threshold",[0.9,0.75,0.5]), \
                 name_size_file=yoloparam.get("deteval_name_size_file",""))
            net.detection_eval = L.DetectionEvaluate(net.detection_out, net[gt_label], \
            	  detection_evaluate_param=det_eval_param, \
            	  include=dict(phase=caffe_pb2.Phase.Value('TEST')))
        if not eval_enable:
            net.slience = L.Silence(net.detection_out, ntop=0, \
                include=dict(phase=caffe_pb2.Phase.Value('TEST')))
        return net
