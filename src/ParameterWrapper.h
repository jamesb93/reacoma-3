#pragma once
#include "reaper_imgui_functions.h"
#include <type_traits>
#include <string>
#include <functional>

namespace flucoma {

// Parameter wrapper class that handles GUI state and value tracking
template <typename T>
class ParameterWrapper {
public:
    ParameterWrapper(const char* name, T defaultValue, T min, T max, const char* format = nullptr, const char* tooltip = nullptr)
        : m_name(name), 
          m_value(defaultValue), 
          m_prevValue(defaultValue), 
          m_min(min), 
          m_max(max), 
          m_format(format ? format : ""), 
          m_tooltip(tooltip),
          m_active(false) {}
    
    bool draw(ImGui_Context* ctx) {
        bool changed = false;
        
        if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
            changed = ImGui::SliderDouble(ctx, m_name, &m_value, m_min, m_max, m_format);
        }
        else if constexpr (std::is_same_v<T, int>) {
            changed = ImGui::SliderInt(ctx, m_name, &m_value, static_cast<int>(m_min), static_cast<int>(m_max), m_format);
        }
        else if constexpr (std::is_same_v<T, bool>) {
            changed = ImGui::Checkbox(ctx, m_name, &m_value);
        }
        
        // Display tooltip if available
        if (m_tooltip && ImGui::IsItemHovered(ctx)) {
            bool tooltipOpen = ImGui::BeginTooltip(ctx);
            if (tooltipOpen) {
                // Modify this line to match your ImGui::Text function signature
                ImGui::Text(ctx, m_tooltip);  // Changed from ImGui::Text(ctx, "%s", m_tooltip)
                ImGui::EndTooltip(ctx);
            }
        }
        
        m_active = ImGui::IsItemActive(ctx);
        return changed;
    }
    
    // Add a callback for when the value changes
    void setChangeCallback(std::function<void(T)> callback) {
        m_changeCallback = callback;
    }
    
    bool isActive() const { return m_active; }
    bool hasChanged() const { return m_value != m_prevValue; }
    
    void saveValue() { 
        if (m_value != m_prevValue && m_changeCallback) {
            m_changeCallback(m_value);
        }
        m_prevValue = m_value; 
    }
    
    T getValue() const { return m_value; }
    void setValue(T newValue) { 
        m_value = newValue;
        if (m_value < m_min) m_value = m_min;
        if (m_value > m_max) m_value = m_max;
    }
    
    // For easier access to the parameter value
    operator T() const { return m_value; }
    
private:
    const char* m_name;
    T m_value;
    T m_prevValue;
    T m_min;
    T m_max;
    const char* m_format;
    const char* m_tooltip;
    bool m_active;
    std::function<void(T)> m_changeCallback;
};

} // namespace flucoma