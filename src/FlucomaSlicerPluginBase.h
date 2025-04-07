#pragma once
#include "FlucomaPluginBase.h"
#include <clients/common/BufferAdaptor.hpp>
#include <clients/common/MemoryBufferAdaptor.hpp>
#include <clients/common/ParameterTypes.hpp>

/**
 * FlucomaSlicerPluginBase
 * 
 * A specialized base class for Flucoma plugins that produce slice indices as output.
 * Implements common functionality for slice-based algorithms, including marker creation.
 */
template<typename ClientType, typename PluginType>
class FlucomaSlicerPluginBase : public FlucomaPluginBase<ClientType, PluginType> {
protected:
    // Use the base class's constructor
    FlucomaSlicerPluginBase(const char* pluginName)
        : FlucomaPluginBase<ClientType, PluginType>(pluginName),
          m_sliceBufferIndex(5),  // Default index for slice buffer in most Flucoma slice algorithms
          m_sliceMarkerLabel("slice")  // Default marker label
    {
    }
    
    // Implement the createMarkersFromResults method for all slicer plugins
    bool createMarkersFromResults() override {
        MediaItem* item = GetSelectedMediaItem(0, 0);
        if (!item) return false;
        
        MediaItem_Take* take = GetActiveTake(item);
        if (!take) return false;
        
        PCM_source* source = GetMediaItemTake_Source(take);
        if (!source) return false;
        
        // Access the slice buffer from the parameter set
        // Instead of using template parameters directly, delegate to the derived class
        fluid::client::BufferAdaptor* sliceBuffer = getSliceBuffer();
        if (!sliceBuffer) {
            strcpy(this->m_status, "Invalid slice buffer");
            return false;
        }
        
        fluid::client::BufferAdaptor::ReadAccess readAccess(sliceBuffer);
        if (!readAccess.valid()) {
            strcpy(this->m_status, "Invalid output buffer");
            return false;
        }
        
        // Get slice points data
        auto slicesView = readAccess.samps(0);
        
        double sampleRate = GetMediaSourceSampleRate(source);
        double playRate = GetMediaItemTakeInfo_Value(take, "D_PLAYRATE");
        
        Undo_BeginBlock2(0);
        
        // Delete all existing take markers first
        int markerCount = GetNumTakeMarkers(take);
        for (int i = markerCount - 1; i >= 0; i--) {
            DeleteTakeMarker(take, i);
        }
        
        // Add new markers at slice points
        int numSlices = 0;
        for (fluid::index i = 0; i < slicesView.size(); i++) {
            if (slicesView(i) > 0) {
                double sliceTime = slicesView(i) / sampleRate / playRate;
                SetTakeMarker(take, -1, m_sliceMarkerLabel, &sliceTime, nullptr);
                numSlices++;
            }
        }
        
        UpdateTimeline();
        Undo_EndBlock2(0, getUndoLabel(), -1);
        
        // Success
        char successMsg[256];
        snprintf(successMsg, sizeof(successMsg), 
                 "%s: Added %d %s markers", this->m_pluginName, numSlices, m_sliceMarkerLabel);
        strcpy(this->m_status, successMsg);
        
        return true;
    }
    
    // Virtual method that derived classes must implement to provide the slice buffer
    virtual fluid::client::BufferAdaptor* getSliceBuffer() = 0;
    
    // Virtual method to get custom undo label
    virtual const char* getUndoLabel() const {
        return "Flucoma: Add Slice Markers";
    }
    
    // Configuration methods to be called by derived classes if needed
    void setSliceBufferIndex(int index) {
        m_sliceBufferIndex = index;
    }
    
    void setSliceMarkerLabel(const char* label) {
        m_sliceMarkerLabel = label;
    }
    
protected:
    // The parameter index where the slice buffer is stored
    int m_sliceBufferIndex;
    
    // The label to use for slice markers
    const char* m_sliceMarkerLabel;
};