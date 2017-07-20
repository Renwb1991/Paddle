/* Copyright (c) 2016 PaddlePaddle Authors. All Rights Reserve.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#include "ROIPoolLayer.h"

namespace paddle {

REGISTER_LAYER(roi_pool, ROIPoolLayer);

bool ROIPoolLayer::init(const LayerMap& layerMap,
                        const ParameterMap& parameterMap) {
  Layer::init(layerMap, parameterMap);

  const ROIPoolConfig& layerConf = config_.inputs(0).roi_pool_conf();
  pooledWidth_ = layerConf.pooled_width();
  pooledHeight_ = layerConf.pooled_height();
  spatialScale_ = layerConf.spatial_scale();

  return true;
}

void ROIPoolLayer::forward(PassType passType) {
  Layer::forward(passType);

  const ROIPoolConfig& layerConf = config_.inputs(0).roi_pool_conf();
  height_ = getInput(0).getFrameHeight();
  if (!height_) height_ = layerConf.height();
  width_ = getInput(0).getFrameWidth();
  if (!width_) width_ = layerConf.width();
  channels_ = getInputValue(0)->getWidth() / width_ / height_;

  size_t batchSize = getInput(0).getBatchSize();
  size_t numROIs = getInput(1).getBatchSize();

  real* bottomData = getInputValue(0)->getData();
  size_t batchOffset = getInputValue(0)->getWidth();
  size_t channelOffset = height_ * width_;
  real* bottomROIs = getInputValue(1)->getData();
  size_t roiOffset = getInputValue(1)->getWidth();
  size_t poolChannelOffset = pooledHeight_ * pooledWidth_;

  resetOutput(numROIs, channels_ * pooledHeight_ * pooledWidth_);
  real* outputData = getOutputValue()->getData();
  Matrix::resizeOrCreate(maxIdxs_,
                         numROIs,
                         channels_ * pooledHeight_ * pooledWidth_,
                         false,
                         false);
  real* argmaxData = maxIdxs_->getData();

  size_t uZero = 0;
  size_t uOne = 1;

  for (size_t n = 0; n < numROIs; ++n) {
    size_t roiBatchIdx = bottomROIs[0];
    size_t roiStartW = std::round(bottomROIs[1] * spatialScale_);
    size_t roiStartH = std::round(bottomROIs[2] * spatialScale_);
    size_t roiEndW = std::round(bottomROIs[3] * spatialScale_);
    size_t roiEndH = std::round(bottomROIs[4] * spatialScale_);
    CHECK_GE(roiBatchIdx, 0);
    CHECK_LT(roiBatchIdx, batchSize);
    size_t roiHeight = std::max(roiEndH - roiStartH + 1, uOne);
    size_t roiWidth = std::max(roiEndW - roiStartW + 1, uOne);
    real binSizeH =
        static_cast<real>(roiHeight) / static_cast<real>(pooledHeight_);
    real binSizeW =
        static_cast<real>(roiWidth) / static_cast<real>(pooledWidth_);
    real* batchData = bottomData + batchOffset * roiBatchIdx;
    for (size_t c = 0; c < channels_; ++c) {
      for (size_t ph = 0; ph < pooledHeight_; ++ph) {
        for (size_t pw = 0; pw < pooledWidth_; ++pw) {
          size_t hstart = static_cast<size_t>(std::floor(ph * binSizeH));
          size_t wstart = static_cast<size_t>(std::floor(pw * binSizeW));
          size_t hend = static_cast<size_t>(std::ceil((ph + 1) * binSizeH));
          size_t wend = static_cast<size_t>(std::ceil((pw + 1) * binSizeW));
          hstart = std::min(std::max(hstart + roiStartH, uZero), height_);
          wstart = std::min(std::max(wstart + roiStartW, uZero), width_);
          hend = std::min(std::max(hend + roiStartH, uZero), height_);
          wend = std::min(std::max(wend + roiStartW, uZero), width_);

          bool isEmpty = (hend <= hstart) || (wend <= wstart);
          size_t poolIndex = ph * pooledWidth_ + pw;
          if (isEmpty) {
            outputData[poolIndex] = 0;
            argmaxData[poolIndex] = -1;
          }

          for (size_t h = hstart; h < hend; ++h) {
            for (size_t w = wstart; w < wend; ++w) {
              size_t index = h * width_ + w;
              if (batchData[index] > outputData[poolIndex]) {
                outputData[poolIndex] = batchData[index];
                argmaxData[poolIndex] = index;
              }
            }
          }
        }
      }
      batchData += channelOffset;
      outputData += poolChannelOffset;
      argmaxData += poolChannelOffset;
    }
    bottomROIs += roiOffset;
  }
}

void ROIPoolLayer::backward(const UpdateCallback& callback) {
  real* bottomROIs = getInputValue(1)->getData();
  size_t numROIs = getInput(1).getBatchSize();
  size_t roiOffset = getInputValue(1)->getWidth();

  MatrixPtr inGrad = getInputGrad(0);
  real* inDiffData = inGrad->getData();
  size_t batchOffset = getInputValue(0)->getWidth();
  size_t channelOffset = height_ * width_;

  MatrixPtr outGrad = getOutputGrad();
  real* outDiffData = outGrad->getData();
  size_t poolChannelOffset = pooledHeight_ * pooledWidth_;
  real* argmaxData = maxIdxs_->getData();

  for (size_t n = 0; n < numROIs; ++n) {
    size_t roiBatchIdx = bottomROIs[0];
    real* batchDiffData = inDiffData + batchOffset * roiBatchIdx;
    for (size_t c = 0; c < channels_; ++c) {
      for (size_t ph = 0; ph < pooledHeight_; ++ph) {
        for (size_t pw = 0; pw < pooledWidth_; ++pw) {
          size_t poolIndex = ph * pooledWidth_ + pw;
          if (argmaxData[poolIndex] > 0) {
            size_t index = static_cast<size_t>(argmaxData[poolIndex]);
            batchDiffData[index] += outDiffData[poolIndex];
          }
        }
      }
      batchDiffData += channelOffset;
      outDiffData += poolChannelOffset;
      argmaxData += poolChannelOffset;
    }
    bottomROIs += roiOffset;
  }
}

}  // namespace paddle
