#include <vector>
#include <cstring>
#include "reaper_plugin.h"

class VectorPCMSource : public PCM_source
{
private:
    std::vector<ReaSample> m_samples;
    int m_nch;
    double m_samplerate;
    char m_filename[512];
    bool m_available;

public:
    // Constructor with vector of samples
    VectorPCMSource(const std::vector<ReaSample>& samples, int channels, double samplerate, const char* name = "VectorSource")
    {
        m_samples = samples;
        m_nch = channels > 0 ? channels : 1;
        m_samplerate = samplerate > 0 ? samplerate : 44100.0;
        strncpy(m_filename, name, sizeof(m_filename) - 1);
        m_filename[sizeof(m_filename) - 1] = '\0';
        m_available = true;
    }

    // Required PCM_source implementations
    PCM_source* Duplicate() override
    {
        return new VectorPCMSource(m_samples, m_nch, m_samplerate, m_filename);
    }

    bool IsAvailable() override { return m_available; }
    void SetAvailable(bool avail) override { m_available = avail; }
    const char* GetType() override { return "VECTORPCM"; }
    const char* GetFileName() override { return m_filename; }
    bool SetFileName(const char* newfn) override
    {
        if (!newfn) return false;
        strncpy(m_filename, newfn, sizeof(m_filename) - 1);
        m_filename[sizeof(m_filename) - 1] = '\0';
        return true;
    }
    int GetNumChannels() override { return m_nch; }
    double GetSampleRate() override { return m_samplerate; }
    double GetLength() override
    {
        if (m_nch <= 0) return 0.0;
        return m_samples.size() / (double)(m_nch * m_samplerate);
    }
    
    int PropertiesWindow(HWND hwndParent) override { return 0; }
    
    // The core method that provides audio samples
    void GetSamples(PCM_source_transfer_t* block) override
    {
        if (!block || !block->samples || block->length <= 0 || m_nch <= 0 || m_samples.empty())
        {
            if (block) block->samples_out = 0;
            return;
        }

        // Calculate sample offset
        int start_sample = (int)(block->time_s * m_samplerate) * m_nch;
        
        // Clamp to available samples
        if (start_sample >= (int)m_samples.size())
        {
            block->samples_out = 0;
            return;
        }
        
        // Calculate samples to copy
        int available_samples = ((int)m_samples.size() - start_sample) / m_nch;
        int samples_to_copy = block->length > available_samples ? available_samples : block->length;
        
        // Copy samples (interleaved)
        for (int i = 0; i < samples_to_copy; i++)
        {
            for (int ch = 0; ch < m_nch && ch < block->nch; ch++)
            {
                int src_idx = start_sample + i * m_nch + ch;
                if (src_idx < (int)m_samples.size())
                    block->samples[i * block->nch + ch] = m_samples[src_idx];
                else
                    block->samples[i * block->nch + ch] = 0.0;
            }
            
            // Fill remaining channels with silence
            for (int ch = m_nch; ch < block->nch; ch++)
            {
                block->samples[i * block->nch + ch] = 0.0;
            }
        }
        
        block->samples_out = samples_to_copy;
    }

    // Provide peak data for waveform display
    void GetPeakInfo(PCM_source_peaktransfer_t* block) override
    {
        if (!block || !block->peaks || block->numpeak_points <= 0 || m_nch <= 0 || m_samples.empty())
        {
            if (block) block->peaks_out = 0;
            return;
        }
        
        double scale = (double)m_samples.size() / m_nch / block->numpeak_points;
        int channels_to_process = block->nchpeaks < m_nch ? block->nchpeaks : m_nch;
        
        for (int i = 0; i < block->numpeak_points; i++)
        {
            int start_sample = (int)(i * scale) * m_nch;
            int end_sample = (int)((i + 1) * scale) * m_nch;
            
            if (start_sample >= (int)m_samples.size()) break;
            if (end_sample > (int)m_samples.size()) end_sample = (int)m_samples.size();
            
            for (int ch = 0; ch < channels_to_process; ch++)
            {
                ReaSample min_val = 0.0;
                ReaSample max_val = 0.0;
                bool found_data = false;
                
                for (int s = start_sample + ch; s < end_sample; s += m_nch)
                {
                    if (s < (int)m_samples.size())
                    {
                        if (!found_data || m_samples[s] < min_val) min_val = m_samples[s];
                        if (!found_data || m_samples[s] > max_val) max_val = m_samples[s];
                        found_data = true;
                    }
                }
                
                block->peaks[i * block->nchpeaks + ch] = max_val;
                if (block->peaks_minvals)
                    block->peaks_minvals[i * block->nchpeaks + ch] = min_val;
            }
        }
        
        block->peaks_out = block->numpeak_points;
    }

    // Base implementation of state save/load
    void SaveState(ProjectStateContext* ctx) override
    {
        if (!ctx) return;
        ctx->AddLine("VECTORPCM %d %f %s", m_nch, m_samplerate, m_filename);
    }

    int LoadState(const char* firstline, ProjectStateContext* ctx) override
    {
        if (!firstline) return -1;
        
        int nch = 0;
        double srate = 44100.0;
        char filename[512] = {0};
        
        if (sscanf(firstline, "VECTORPCM %d %lf %511[^\n]", &nch, &srate, filename) >= 2)
        {
            m_nch = nch > 0 ? nch : 1;
            m_samplerate = srate > 0 ? srate : 44100.0;
            if (filename[0])
                strncpy(m_filename, filename, sizeof(m_filename) - 1);
            return 0;
        }
        
        return -1;
    }

    // Required but minimal implementations for in-memory sources
    void Peaks_Clear(bool deleteFile) override {}
    int PeaksBuild_Begin() override { return 0; }
    int PeaksBuild_Run() override { return 0; }
    void PeaksBuild_Finish() override {}
};