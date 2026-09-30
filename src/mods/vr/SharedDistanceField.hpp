#pragma once

#include <cstdint>

namespace sdk {
class FSceneViewFamily;
}

// Keeps both eyes of the native stereo fix on one global distance field.
//
// ULocalPlayer allocates a view state per eye without ShareOrigin, so each eye keeps its own
// FPersistentGlobalDistanceFieldData and updates it incrementally from the scene's list of modified
// primitives. That list is reset after every view family, so when the eyes render as separate families
// the second eye's field never hears of a change, and Lumen's far lighting differs between the eyes
// until a full rebuild. A view state allocated with ShareOrigin holds the other's field, and the
// renderer updates a shared field once per frame and hands it to both views. The states already exist
// by the time we're injected, so instead the right eye's reference is repointed at the left eye's
// field, on the render thread before a family renders.
class SharedDistanceField {
public:
    // Game thread, right after the FSceneView constructor for eye 0 or 1. Learns where FSceneView keeps
    // its view state, and which state belongs to which eye.
    static void on_view_constructed(void* view, void* view_state, uint32_t eye);

    // Render thread, before the family renders.
    static void on_pre_render(sdk::FSceneViewFamily& view_family, bool wanted);
};
