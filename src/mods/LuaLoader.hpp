#pragma once

#include <atomic>
#include <deque>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <shared_mutex>

#include "ScriptContext.hpp"
#include "ScriptState.hpp"

#include "Mod.hpp"

using namespace uevr;

class LuaLoader : public Mod {
public:
    static std::shared_ptr<LuaLoader>& get();

    std::string_view get_name() const override { return "LuaLoader"; }
    bool is_advanced_mod() const override { return true; }
    std::optional<std::string> on_initialize_d3d_thread() override;

    std::vector<SidebarEntryInfo> get_sidebar_entries() override {
        if (m_script_panels.empty()) {
            return {
                {"Main", true},
                {"Script UI", true}
            };
        }

        std::vector<SidebarEntryInfo> entries{
            {"Main", true},
            {"Script UI", true}
        };

        for (auto& entry : m_script_panels) {
            entries.emplace_back(entry.name, true);
        }

        return entries;
    }

    void on_draw_sidebar_entry(std::string_view in_entry);
    void on_frame() override;

    void on_config_load(const utility::Config& cfg, bool set_defaults) override;
    void on_config_save(utility::Config& cfg) override;


    const auto& get_state() {
        return m_main_state;
    }

    const auto& get_state(int index) { 
        return m_states[index];
    }

    std::scoped_lock<std::recursive_mutex> get_access_lock() {
        return std::scoped_lock<std::recursive_mutex>{m_access_mutex};
    }

    // Resets the ScriptState and runs autorun scripts again.
    void reset_scripts();

    // Ask for a reload from anywhere, including from inside a running script.
    //
    // NOT A RENAMED reset_scripts. That one destroys every Lua state, so calling it from a script would free the
    // stack the caller is still running on. This only raises a flag; on_frame does the reload where the keybind
    // already does it, before any state is walked.
    void request_reset();
    void state_post_init(std::shared_ptr<ScriptState>& state);
    void add_additional_bindings(sol::state_view& lua);
    void dispatch_event(std::string_view event_name, std::string_view event_data);

private:
    ScriptState::GarbageCollectionData make_gc_data() const {
        ScriptState::GarbageCollectionData data{};

        data.gc_handler = (decltype(ScriptState::GarbageCollectionData::gc_handler))m_gc_handler->value();
        data.gc_type = (decltype(ScriptState::GarbageCollectionData::gc_type))m_gc_type->value();
        data.gc_mode = (decltype(ScriptState::GarbageCollectionData::gc_mode))m_gc_mode->value();
        data.gc_budget = std::chrono::microseconds{(uint32_t)m_gc_budget->value()};
        data.gc_minor_multiplier = (uint32_t)m_gc_minor_multiplier->value();
        data.gc_major_multiplier = (uint32_t)m_gc_major_multiplier->value();

        return data;
    }

    std::shared_ptr<ScriptState> m_main_state{};
    std::vector<std::shared_ptr<ScriptState>> m_states{};
    std::recursive_mutex m_access_mutex{};

    // A list of Lua files that have been explicitly loaded either through the user manually loading the script, or
    // because the script was in the autorun directory.
    std::vector<std::string> m_loaded_scripts{};
    std::vector<std::string> m_known_scripts{};
    std::unordered_map<std::string, bool> m_loaded_scripts_map{};
    std::vector<lua_State*> m_states_to_delete{};
    struct PanelEntry {
        std::weak_ptr<uevr::ScriptState> state;
        std::string name;
        sol::protected_function fn;
    };
    std::vector<PanelEntry> m_script_panels{};

    bool m_console_spawned{false};
    bool m_needs_first_reset{true};

    // Raised by request_reset, cleared by on_frame. Atomic because the request can come from a script running on
    // the game thread while on_frame may still be on the DXGI thread before tick is hooked.
    std::atomic<bool> m_reset_requested{false};

    const ModToggle::Ptr m_log_to_disk{ ModToggle::create(generate_name("LogToDisk"), false) };

    // Resetting the scripts is the main loop when working on one, and the only way to do it was the
    // button in this panel -- which means taking the headset off or fighting the menu in VR.
    //
    // Numpad plus, and not numpad 0, because the keypad digits only send VK_NUMPAD* while Num Lock is on.
    // With it off the same key sends VK_INSERT, which is what toggles this menu (Framework.cpp), so the
    // binding would have opened the menu instead. VK_ADD is the same code either way.
    const ModKey::Ptr m_keybind_reset_scripts{ ModKey::create(generate_name("ResetScriptsKey"), VK_ADD) };

    const ModCombo::Ptr m_gc_handler { 
        ModCombo::create(generate_name("GarbageCollectionHandler"),
        {
            "Managed by UEVR",
            "Managed by Lua"
        }, (int)ScriptState::GarbageCollectionHandler::UEVR_MANAGED)
    };

    const ModCombo::Ptr m_gc_type {
        ModCombo::create(generate_name("GarbageCollectionType"),
        {
            "Step",
            "Full",
        }, (int)ScriptState::GarbageCollectionType::STEP)
    };

    const ModCombo::Ptr m_gc_mode {
        ModCombo::create(generate_name("GarbageCollectionMode"),
        {
            "Generational",
            "Incremental (Mark & Sweep)",
        }, (int)ScriptState::GarbageCollectionMode::GENERATIONAL)
    };

    // Garbage collection budget in microseconds.
    const ModSlider::Ptr m_gc_budget {
        ModSlider::create(generate_name("GarbageCollectionBudget"), 0.0f, 2000.0f, 1000.0f)
    };

    const ModSlider::Ptr m_gc_minor_multiplier {
        ModSlider::create(generate_name("GarbageCollectionMinorMultiplier"), 1.0f, 200.0f, 1.0f)
    };

    const ModSlider::Ptr m_gc_major_multiplier {
        ModSlider::create(generate_name("GarbageCollectionMajorMultiplier"), 1.0f, 1000.0f, 100.0f)
    };

    ValueList m_options{
        *m_log_to_disk,
        *m_keybind_reset_scripts,
        *m_gc_handler,
        *m_gc_type,
        *m_gc_mode,
        *m_gc_budget,
        *m_gc_minor_multiplier,
        *m_gc_major_multiplier
    };
};