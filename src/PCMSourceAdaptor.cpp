#include "PCMSourceAdaptor.h"

namespace fluid {

PCMSourceAdaptor::PCMSourceAdaptor(std::vector<float>& data, index numChannels,
                  index numFrames, double sampleRate)
    : mData(data.data(), 0, numFrames, numChannels),
      mNumFrames(numFrames),
      mNumChannels(numChannels),
      mSampleRate(sampleRate),
      mAcquired(false) 
{
}

bool PCMSourceAdaptor::acquire() const { 
    return !mAcquired && (mAcquired = true); 
}

void PCMSourceAdaptor::release() const { 
    mAcquired = false; 
}

bool PCMSourceAdaptor::valid() const { 
    return numFrames() > 0; 
}

bool PCMSourceAdaptor::exists() const { 
    return true; 
}

const client::Result PCMSourceAdaptor::resize(index frames, index channels,
                  double sampleRate) {
    return client::Result{client::Result::Status::kError, "Resize not supported"};
}

std::string PCMSourceAdaptor::asString() const { 
    return "PCMSourceAdaptor"; 
}

FluidTensorView<float, 2> PCMSourceAdaptor::allFrames() { 
    return mData; 
}

FluidTensorView<const float, 2> PCMSourceAdaptor::allFrames() const { 
    return mData; 
}

FluidTensorView<float, 1> PCMSourceAdaptor::samps(index channel) {
    return mData.col(channel);
}

FluidTensorView<float, 1> PCMSourceAdaptor::samps(index offset, index nframes,
                              index chanoffset) {
    return mData(Slice(offset, nframes), Slice(chanoffset, 1)).col(0);
}

FluidTensorView<const float, 1> PCMSourceAdaptor::samps(index channel) const {
    return mData.col(channel);
}

FluidTensorView<const float, 1> PCMSourceAdaptor::samps(index offset, index nframes,
                                    index chanoffset) const {
    return mData(Slice(offset, nframes), Slice(chanoffset, 1)).col(0);
}

index PCMSourceAdaptor::numFrames() const { 
    return mNumFrames; 
}

index PCMSourceAdaptor::numChans() const { 
    return mNumChannels; 
}

double PCMSourceAdaptor::sampleRate() const { 
    return mSampleRate; 
}

}  // namespace fluid