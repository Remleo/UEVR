#pragma once

#include <string>
#include <optional>
#include <cstdint>
#include <array>
#include <functional>
#include <mutex>

#include "Mod.hpp"

#include "imgui.h"

namespace vrmod{
class D3D11Component;
class D3D12Component;

class OverlayComponent : public ModComponent {
public:
    // UI PLANES, READ BY INDEX. 0 is the interface the game draws; from 1 on are the planes scripts ask for, each with
    // its own swapchain, made when it is asked for (VR::create_ui_plane_sized_like_game_ui).
    //
    // FOUR FOR SCRIPTS, because each one is a swapchain and a composition layer of the runtime's: OpenXR promises
    // sixteen layers a frame and no more, and SteamVR allows sixteen swapchains a session, a frame generation layer
    // taking some of them for itself.
    //
    // Public because the count is not this class's private business: VR sizes its source arrays by it and
    // the D3D12 path loops over it. One place to change, and nothing to keep in step by hand.
    static constexpr size_t SCRIPT_PLANE_COUNT = 4;
    static constexpr size_t PLANE_COUNT = 1 + SCRIPT_PLANE_COUNT;
    static constexpr size_t PLANE_GAME = 0;
    static constexpr size_t PLANE_SCRIPT = 1;

    void on_reset();
    std::optional<std::string> on_initialize_openvr();

    void on_pre_imgui_frame() override;
    void on_post_compositor_submit();

    void on_config_save(utility::Config& cfg) override;
    void on_config_load(const utility::Config& cfg, bool set_defaults)  override;
    void on_draw_ui() override;

    auto& get_openxr() {
        return m_openxr;
    }

    struct IntersectState {
        bool intersecting{false};
        glm::vec2 quad_intersection_point{};
        glm::vec2 swapchain_intersection_point{};
    };

    const auto& get_intersect_state() const {
        return m_intersect_state;
    }

    const auto& get_framework_intersect_state() const {
        return m_intersect_state;
    }

    bool should_invert_ui_alpha() const {
        return m_ui_invert_alpha->value();
    }

    // WHERE A PLANE WAS LAST PUT, kept so a script can be told. `space` is what makes the pose readable at all:
    // 0 nothing was placed this frame, 1 the pose is against the head, 2 against the standing origin. The
    // reasoning for handing this out rather than letting a script rebuild it is in API.h, next to
    // `get_ui_quad_pose`.
    //
    // WRITTEN BY THE QUAD PATH ONLY, and `space` staying 0 is how the cylinder path and OpenVR say "not me"
    // instead of leaving last frame's pose to be read as current.
    struct QuadPose {
        unsigned int space{0};
        glm::quat rotation{glm::identity<glm::quat>()};
        glm::vec3 position{};
    };

    const QuadPose& get_last_quad_pose(size_t plane) const {
        return m_last_quad_pose[plane < PLANE_COUNT ? plane : 0];
    }

    // A HELD plane stands in the stage exactly where it hung on the headset when the hold began: the head's pose at
    // that moment is kept, and the plane is placed from it as the head locked one is placed from the head -- same
    // distance, size and offsets, and every turn and tilt, roll included. A hold asked for while one is on keeps the
    // pose it took. OpenXR's quad path only; elsewhere the request is ignored.
    void set_plane_held(size_t plane, bool held);
    std::optional<glm::mat4> get_held_head(size_t plane) const;

    // A script plane's placement, the same values the game's plane takes from its config keys. A new plane starts
    // from the game plane's (copy_placement), so it covers the interface exactly until a script moves it.
    void copy_placement(size_t from, size_t to);
    void set_plane_distance(size_t plane, float meters);
    void set_plane_size(size_t plane, float meters);
    void set_plane_offset(size_t plane, float x, float y);
    void set_plane_follows_view(size_t plane, bool follows);

private:
    std::array<QuadPose, PLANE_COUNT> m_last_quad_pose{};

    // Written by a script's thread, read where the layers are made.
    mutable std::mutex m_held_mtx{};
    std::array<std::optional<glm::mat4>, PLANE_COUNT> m_held_head{};

    // Cached data for imgui VR overlay so we know when we need to update it
    // instead of doing it constantly every frame
    struct {
        uint32_t last_render_target_width{};
        uint32_t last_render_target_height{};
        float last_width{};
        float last_height{};
        float last_x{};
        float last_y{};
    } m_overlay_data;

    // initial input state for imgui on the left eye frame
    struct {
        ImVec2      MousePos{};
        bool        MouseDown[5]{};
        float       MouseWheel{};
        float       MouseWheelH{};
        bool        KeyCtrl{};
        bool        KeyShift{};
        bool        KeyAlt{};
        bool        KeySuper{};
        bool        KeysDown[512]{};
    } m_initial_imgui_input_state;

    // overlay handle
    vr::VROverlayHandle_t m_overlay_handle{};
    vr::VROverlayHandle_t m_thumbnail_handle{};
    vr::VROverlayHandle_t m_slate_overlay_handle{};

    bool m_closed_ui{false};
    bool m_just_closed_ui{false};
    bool m_just_opened_ui{false};
    bool m_forced_aim{false};
    
    glm::vec2 m_last_mouse_pos{};
    std::chrono::steady_clock::time_point m_last_mouse_move_time{};

    IntersectState m_intersect_state{};
    IntersectState m_framework_intersect_state{};

    enum OverlayType {
        DEFAULT = 0,
        QUAD = 0,
        CYLINDER = 1,
        MAX
    };

    static const inline std::vector<std::string> s_overlay_type_names{
        "Quad",
        "Cylinder"
    };

    const ModCombo::Ptr m_slate_overlay_type{ ModCombo::create("UI_OverlayType", s_overlay_type_names) };
    const ModSlider::Ptr m_slate_cylinder_angle{ ModSlider::create("UI_Cylinder_Angle", 0.0f, 360.0f, 90.0f) };
    const ModToggle::Ptr m_ui_invert_alpha{ ModToggle::create("UI_InvertAlpha", false) };

    // ONE VALUE PER PLANE. Planes 0 and 1 keep the keys configs and profiles in the wild already carry: a rename would
    // silently reset everyone's interface placement, and plane 1 is the one set_secondary_ui_source feeds. The planes
    // after them are asked for by scripts each session and saved nowhere -- they are not among m_options -- so their
    // keys only name them.
    template <typename T>
    static std::array<std::unique_ptr<T>, PLANE_COUNT> per_plane(std::unique_ptr<T> game, std::unique_ptr<T> first_script,
        const std::function<std::unique_ptr<T>(const std::string& prefix)>& later_script)
    {
        std::array<std::unique_ptr<T>, PLANE_COUNT> out{};
        out[PLANE_GAME] = std::move(game);
        out[PLANE_SCRIPT] = std::move(first_script);

        for (size_t plane = PLANE_SCRIPT + 1; plane < PLANE_COUNT; ++plane) {
            out[plane] = later_script("UI_Plane" + std::to_string(plane));
        }

        return out;
    }

    // PLACEMENT VALUES, ONE ARRAY PER SETTING. The planes from 1 on are fed by scripts, so a piece of the interface
    // can be placed differently from the rest -- a reticle that belongs with the shot rather than with the head, which
    // is what this was built for.
    //
    // Everything the placement reads is an array by plane, because the placement itself is one piece of code walked
    // once per plane. The alternative -- a second copy of it with its own anchoring -- was written first and thrown
    // away: it repeated the pose maths and got a different answer than the plane beside it, which is the one thing
    // that must not differ.
    const std::array<ModSlider::Ptr, PLANE_COUNT> m_slate_distance{per_plane<ModSlider>(
        ModSlider::create("UI_Distance", 0.5f, 10.0f, 2.0f),
        ModSlider::create("UI_Secondary_Distance", 0.05f, 10.0f, 1.0f),
        [](const std::string& prefix) { return ModSlider::create(prefix + "_Distance", 0.05f, 10.0f, 1.0f); })};

    const std::array<ModSlider::Ptr, PLANE_COUNT> m_slate_size{per_plane<ModSlider>(
        ModSlider::create("UI_Size", 0.5f, 10.0f, 2.0f),
        ModSlider::create("UI_Secondary_Size", 0.01f, 10.0f, 0.25f),
        [](const std::string& prefix) { return ModSlider::create(prefix + "_Size", 0.01f, 10.0f, 0.25f); })};

    const std::array<ModSlider::Ptr, PLANE_COUNT> m_slate_x_offset{per_plane<ModSlider>(
        ModSlider::create("UI_X_Offset", -10.0f, 10.0f, 0.0f),
        ModSlider::create("UI_Secondary_X_Offset", -10.0f, 10.0f, 0.0f),
        [](const std::string& prefix) { return ModSlider::create(prefix + "_X_Offset", -10.0f, 10.0f, 0.0f); })};

    const std::array<ModSlider::Ptr, PLANE_COUNT> m_slate_y_offset{per_plane<ModSlider>(
        ModSlider::create("UI_Y_Offset", -10.0f, 10.0f, 0.0f),
        ModSlider::create("UI_Secondary_Y_Offset", -10.0f, 10.0f, 0.0f),
        [](const std::string& prefix) { return ModSlider::create(prefix + "_Y_Offset", -10.0f, 10.0f, 0.0f); })};

    // The binding, and the only value that has to differ between the game's plane and a reticle's for the reticle to
    // work: off means the plane hangs in the stage through the rotation offset, which the aim path writes, so the
    // plane faces where the game shoots.
    const std::array<ModToggle::Ptr, PLANE_COUNT> m_ui_follows_view{per_plane<ModToggle>(
        ModToggle::create("UI_FollowView", false),
        ModToggle::create("UI_Secondary_FollowView", false),
        [](const std::string& prefix) { return ModToggle::create(prefix + "_FollowView", false); })};

    const ModSlider::Ptr m_framework_distance{ ModSlider::create("UI_Framework_Distance", 0.5f, 10.0f, 1.75f) };
    const ModSlider::Ptr m_framework_size{ ModSlider::create("UI_Framework_Size", 0.5f, 10.0f, 2.0f) };
    const ModToggle::Ptr m_framework_ui_follows_view{ ModToggle::create("UI_Framework_FollowView", false) };
    const ModToggle::Ptr m_framework_wrist_ui{ ModToggle::create("UI_Framework_WristUI", false) };
    const ModToggle::Ptr m_framework_mouse_emulation{ ModToggle::create("UI_Framework_MouseEmulation", true) };

public:
    OverlayComponent() 
        : m_openxr{this}
    {
        m_options = { 
            *m_slate_overlay_type,
            *m_slate_cylinder_angle,
            *m_ui_invert_alpha,
            // The game's plane and the first script plane, in index order -- the two whose keys configs carry. Written
            // out rather than looped because m_options is a list of references built once, and spelling it out keeps
            // the save order stable in existing configs.
            *m_slate_x_offset[PLANE_GAME],
            *m_slate_y_offset[PLANE_GAME],
            *m_slate_distance[PLANE_GAME],
            *m_slate_size[PLANE_GAME],
            *m_ui_follows_view[PLANE_GAME],
            *m_slate_x_offset[PLANE_SCRIPT],
            *m_slate_y_offset[PLANE_SCRIPT],
            *m_slate_distance[PLANE_SCRIPT],
            *m_slate_size[PLANE_SCRIPT],
            *m_ui_follows_view[PLANE_SCRIPT],
            *m_framework_distance,
            *m_framework_size,
            *m_framework_ui_follows_view,
            *m_framework_wrist_ui,
            *m_framework_mouse_emulation
        };
    }

    // OpenXR
private:
    class OpenXR {
    public:
        OpenXR(OverlayComponent* parent)
            : m_parent{parent}
        {

        }

        // The plane index is the last argument and defaults to the game's plane, so every existing call site
        // -- including the D3D11 path, which none of this touches -- keeps working unchanged.
        std::optional<std::reference_wrapper<XrCompositionLayerQuad>> generate_slate_quad(
            runtimes::OpenXR::SwapchainIndex swapchain = runtimes::OpenXR::SwapchainIndex::UI, 
            XrEyeVisibility eye = XR_EYE_VISIBILITY_BOTH,
            size_t plane = PLANE_GAME
        );
        std::optional<std::reference_wrapper<XrCompositionLayerCylinderKHR>> generate_slate_cylinder(
            runtimes::OpenXR::SwapchainIndex swapchain = runtimes::OpenXR::SwapchainIndex::UI, 
            XrEyeVisibility eye = XR_EYE_VISIBILITY_BOTH
        );
        std::optional<std::reference_wrapper<XrCompositionLayerBaseHeader>> generate_slate_layer(
            runtimes::OpenXR::SwapchainIndex swapchain = runtimes::OpenXR::SwapchainIndex::UI, 
            XrEyeVisibility eye = XR_EYE_VISIBILITY_BOTH,
            size_t plane = PLANE_GAME
        );
        std::optional<std::reference_wrapper<XrCompositionLayerQuad>> generate_framework_ui_quad();
        
    private:
        std::array<XrCompositionLayerQuad, PLANE_COUNT> m_slate_layer{};
        std::array<XrCompositionLayerQuad, PLANE_COUNT> m_slate_layer_right{};
        XrCompositionLayerCylinderKHR m_slate_layer_cylinder{};
        XrCompositionLayerCylinderKHR m_slate_layer_cylinder_right{};
        XrCompositionLayerQuad m_framework_ui_layer{};

        // Where the cursor's quad was put last with the menu closed, said on a change (generate_framework_ui_quad).
        int m_cursor_placement{-1};

        OverlayComponent* m_parent{ nullptr };
        
        friend class OverlayComponent;
    } m_openxr;

private:
    void update_input_openvr();
    void update_input_mouse_emulation();
    void update_overlay_openvr();
    bool update_wrist_overlay_openvr();
    void update_slate_openvr();
};}