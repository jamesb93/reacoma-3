#pragma once

#include <clients/common/BufferAdaptor.hpp>
#include <data/FluidTensor.hpp>
#include <vector>

namespace fluid {

class PCMSourceAdaptor : public client::BufferAdaptor {
public:
    PCMSourceAdaptor(std::vector<float>& data, index numChannels,
                      index numFrames, double sampleRate);

    bool acquire() const override;
    void release() const override;

    bool valid() const override;
    bool exists() const override;

    const client::Result resize(index frames, index channels,
                      double sampleRate) override;

    std::string asString() const override;

    FluidTensorView<float, 2> allFrames() override;
    FluidTensorView<const float, 2> allFrames() const override;

    FluidTensorView<float, 1> samps(index channel) override;
    FluidTensorView<float, 1> samps(index offset, index nframes,
                                  index chanoffset) override;

    FluidTensorView<const float, 1> samps(index channel) const override;
    FluidTensorView<const float, 1> samps(index offset, index nframes,
                                        index chanoffset) const override;

    index numFrames() const override;
    index numChans() const override;
    double sampleRate() const override;

private:
    FluidTensorView<float, 2> mData;
    index mNumFrames;
    index mNumChannels;
    double mSampleRate;
    mutable bool mAcquired;
};
}