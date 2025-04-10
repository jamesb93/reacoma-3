#pragma once
#include "ParameterWrapper.h"
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

namespace flucoma {

// Parameter manager to handle collections of parameters
class ParameterManager {
public:
    // Add a new parameter and return a reference to it
    template <typename T>
    ParameterWrapper<T>& addParameter(const char* name, T defaultValue, T min, T max, const char* format = nullptr, const char* tooltip = nullptr) {
        auto param = std::make_unique<ParameterImpl<T>>(name, defaultValue, min, max, format, tooltip);
        ParameterWrapper<T>& wrapper = *param;
        m_parameters.push_back(std::move(param));
        return wrapper;
    }
    
    // Draw all parameters and return true if any changed
    bool drawAll(ImGui_Context* ctx) {
        bool anyChanged = false;
        m_anyActive = false;
        
        for (auto& param : m_parameters) {
            anyChanged |= param->draw(ctx);
            m_anyActive |= param->isActive();
        }
        
        return anyChanged;
    }
    
    // Check if any parameter has changed since last save
    bool anyParameterChanged() const {
        for (const auto& param : m_parameters) {
            if (param->hasChanged()) {
                return true;
            }
        }
        return false;
    }
    
    // Save all current parameter values
    void saveAllValues() {
        for (auto& param : m_parameters) {
            param->saveValue();
        }
    }
    
    // Check if any control is currently active
    bool isAnyControlActive() const {
        return m_anyActive;
    }
    
private:
    // Base parameter interface for type erasure
    class BaseParameter {
    public:
        virtual ~BaseParameter() = default;
        virtual bool draw(ImGui_Context* ctx) = 0;
        virtual bool hasChanged() const = 0;
        virtual void saveValue() = 0;
        virtual bool isActive() const = 0;
    };
    
    // Type-specific implementation that inherits from both BaseParameter and ParameterWrapper
    template <typename T>
    class ParameterImpl : public BaseParameter, public ParameterWrapper<T> {
    public:
        using ParameterWrapper<T>::ParameterWrapper;
        
        bool draw(ImGui_Context* ctx) override { 
            return ParameterWrapper<T>::draw(ctx);
        }
        
        bool hasChanged() const override {
            return ParameterWrapper<T>::hasChanged();
        }
        
        void saveValue() override {
            ParameterWrapper<T>::saveValue();
        }
        
        bool isActive() const override {
            return ParameterWrapper<T>::isActive();
        }
    };
    
    std::vector<std::unique_ptr<BaseParameter>> m_parameters;
    bool m_anyActive = false;
};

} // namespace flucoma