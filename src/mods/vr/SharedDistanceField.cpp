#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <optional>
#include <set>
#include <vector>

#include <bddisasm.h>
#include <spdlog/spdlog.h>
#include <utility/Logging.hpp>
#include <utility/Module.hpp>
#include <utility/Scan.hpp>

#include <sdk/FSceneViewFamily.hpp>

#include "SharedDistanceField.hpp"

namespace {
// Offset of FSceneView::State, learned from a constructed view.
std::atomic<int32_t> g_view_state_offset{-1};

// Each eye's view state, as its viewport view was constructed. Only the viewport's views pass through
// there, so scene captures and reflections with states of their own never pair up.
std::atomic<uintptr_t> g_eye_states[2]{};

// Offset of FSceneViewState::GlobalDistanceFieldData, a TRefCountPtr<FPersistentGlobalDistanceFieldData>.
// Render thread only from here down.
std::optional<uint32_t> g_field_offset{};

// When each eye's state was last seen in a family being rendered. A state in a family being rendered is
// alive; both seen within a few calls means both can be touched.
constexpr uint64_t max_calls_apart = 4;
uint64_t g_calls{};

struct Seen {
    uintptr_t state{};
    uint64_t call{};
} g_seen[2]{};
uintptr_t g_scanned_vtable{};
std::set<uint32_t> g_field_offset_candidates{};

// The right eye's view state we repointed, the field it owned (we hold its reference so it can be
// given back), and the left eye's field we put in its place (the slot holds a reference we added).
struct Tracked {
    uintptr_t right_state{};
    uintptr_t original{};
    uintptr_t shared{};
} g_tracked{};

// FThreadSafeRefCountedObject: vtable, then NumRefs. Release deletes through the virtual destructor.
bool is_ref_counted_object(uintptr_t obj) {
    if (obj == 0 || IsBadReadPtr((void*)obj, 16)) {
        return false;
    }

    const auto vtable = *(uintptr_t*)obj;
    const auto refs = *(int32_t*)(obj + 8);

    return vtable != 0 && utility::get_module_within(vtable).has_value() && refs >= 1 && refs < 0x10000;
}

void add_ref(uintptr_t obj) {
    _InterlockedIncrement((volatile long*)(obj + 8));
}

void release(uintptr_t obj) {
    if (_InterlockedDecrement((volatile long*)(obj + 8)) == 0) {
        const auto scalar_deleting_destructor = (*(void (***)(void*, uint32_t))obj)[0];
        scalar_deleting_destructor((void*)obj, 1);
    }
}

// [base + disp] with a displacement past the object's header, off a register that is not the stack.
bool is_member(const ND_OPERAND& op) {
    return op.Type == ND_OP_MEM && op.Info.Memory.HasBase && !op.Info.Memory.HasIndex && !op.Info.Memory.IsRipRel &&
           op.Info.Memory.HasDisp && op.Info.Memory.Disp >= 0x100 && op.Info.Memory.Base != NDR_RSP &&
           op.Info.Memory.Base != NDR_RBP;
}

bool is_member_access(const ND_OPERAND& op) {
    return is_member(op) && op.Size == 8;
}

bool is_gpr64(const ND_OPERAND& op) {
    return op.Type == ND_OP_REG && op.Size == 8 && op.Info.Register.Type == ND_REG_GPR;
}

// The FSceneViewState constructor is the code that writes the object's vtable. Its ShareOrigin branch
// copies the field from the target: a load from one base at the field's displacement, then either a store to
// another base at the same displacement with the copied register AddRef'd (lock inc [reg+8]; inlined, as in
// Stalker 2's 5.5), or the address of the same displacement off another base taken for a call to
// TRefCountPtr::operator= (as in The Outer Worlds 2's 5.4). Every such displacement is a candidate.
std::set<uint32_t> scan_field_offset_candidates(uintptr_t vtable) {
    std::set<uint32_t> candidates{};

    const auto module = utility::get_module_within(vtable);

    if (!module) {
        return candidates;
    }

    struct Access {
        uint32_t disp;
        uint32_t base;
        uint32_t reg;
    };

    for (const auto ref : utility::scan_displacement_references(*module, vtable)) {
        const auto fn = utility::find_function_start(ref);

        if (!fn) {
            continue;
        }

        std::vector<Access> loads{};
        std::vector<Access> stores{};
        std::vector<Access> addresses{};
        std::set<uint32_t> add_ref_regs{};

        utility::exhaustive_decode((uint8_t*)*fn, 1000, [&](utility::ExhaustionContext& ctx) {
            const auto& ix = ctx.instrux;

            if (ix.Category == ND_CAT_CALL) {
                return utility::ExhaustionResult::STEP_OVER;
            }

            if (ix.Instruction == ND_INS_MOV && ix.OperandsCount >= 2) {
                const auto& dst = ix.Operands[0];
                const auto& src = ix.Operands[1];

                if (is_gpr64(dst) && is_member_access(src)) {
                    loads.push_back({(uint32_t)src.Info.Memory.Disp, src.Info.Memory.Base, dst.Info.Register.Reg});
                } else if (is_member_access(dst) && is_gpr64(src)) {
                    stores.push_back({(uint32_t)dst.Info.Memory.Disp, dst.Info.Memory.Base, src.Info.Register.Reg});
                }
            } else if (ix.Instruction == ND_INS_LEA && ix.OperandsCount >= 2 && is_gpr64(ix.Operands[0]) &&
                       is_member(ix.Operands[1])) {
                const auto& src = ix.Operands[1];
                addresses.push_back({(uint32_t)src.Info.Memory.Disp, src.Info.Memory.Base, ix.Operands[0].Info.Register.Reg});
            } else if (ix.HasLock && (ix.Instruction == ND_INS_INC || ix.Instruction == ND_INS_XADD)) {
                const auto& op = ix.Operands[0];

                if (op.Type == ND_OP_MEM && op.Info.Memory.HasBase && !op.Info.Memory.HasIndex && op.Info.Memory.HasDisp &&
                    op.Info.Memory.Disp == 8) {
                    add_ref_regs.insert(op.Info.Memory.Base);
                }
            }

            return utility::ExhaustionResult::CONTINUE;
        });

        for (const auto& load : loads) {
            for (const auto& store : stores) {
                if (load.disp == store.disp && load.base != store.base && load.reg == store.reg &&
                    add_ref_regs.contains(store.reg)) {
                    candidates.insert(store.disp);
                }
            }

            for (const auto& address : addresses) {
                if (load.disp == address.disp && load.base != address.base) {
                    candidates.insert(address.disp);
                }
            }
        }
    }

    return candidates;
}

std::optional<uint32_t> field_offset(uintptr_t left, uintptr_t right) {
    if (g_field_offset) {
        return g_field_offset;
    }

    const auto vtable = *(uintptr_t*)left;

    // The code does not change, so one scan per class.
    if (vtable != g_scanned_vtable) {
        g_scanned_vtable = vtable;
        g_field_offset_candidates = scan_field_offset_candidates(vtable);

        SPDLOG_INFO("[SharedDistanceField] FSceneViewState vtable {:x}: {} candidate field offset(s)", vtable,
                    g_field_offset_candidates.size());
    }

    // Both eyes' states must hold a live field of the same class there. Refuse rather than guess if
    // that does not single one out: a wrong offset would repoint something else.
    std::vector<uint32_t> valid{};

    for (const auto candidate : g_field_offset_candidates) {
        const auto l = *(uintptr_t*)(left + candidate);
        const auto r = *(uintptr_t*)(right + candidate);

        if (is_ref_counted_object(l) && is_ref_counted_object(r) && *(uintptr_t*)l == *(uintptr_t*)r) {
            valid.push_back(candidate);
        }
    }

    if (valid.size() != 1) {
        SPDLOG_ERROR_ONCE("[SharedDistanceField] {} candidate(s) hold a field in both eyes' states, expected 1", valid.size());
        return std::nullopt;
    }

    g_field_offset = valid[0];
    SPDLOG_INFO("[SharedDistanceField] FSceneViewState::GlobalDistanceFieldData at 0x{:x}", *g_field_offset);

    return g_field_offset;
}

void share(uintptr_t left, uintptr_t right, uint32_t offset) {
    auto& left_slot = *(uintptr_t*)(left + offset);
    auto& right_slot = *(uintptr_t*)(right + offset);

    const auto left_field = left_slot;
    const auto right_field = right_slot;

    if (left_field == right_field) {
        return;
    }

    if (!is_ref_counted_object(left_field) || !is_ref_counted_object(right_field) ||
        *(uintptr_t*)left_field != *(uintptr_t*)right_field) {
        SPDLOG_ERROR_ONCE("[SharedDistanceField] The eyes' distance fields do not look alike ({:x}, {:x}), not sharing", left_field,
                          right_field);
        return;
    }

    // A new view state for the right eye: the old one's own field is referenced by us alone.
    if (g_tracked.right_state != right) {
        if (g_tracked.original != 0) {
            release(g_tracked.original);
        }

        g_tracked = {right, 0, 0};
    }

    if (right_field == g_tracked.shared) {
        // The left eye's previous field, put here by us. Drop the reference we added.
        release(right_field);
    } else {
        if (g_tracked.original != 0) {
            release(g_tracked.original);
        }

        // Keep the reference the slot held, so the field can be given back.
        g_tracked.original = right_field;
    }

    add_ref(left_field);
    right_slot = left_field;
    g_tracked.shared = left_field;

    SPDLOG_INFO("[SharedDistanceField] Right eye's view state {:x} now uses the left eye's distance field {:x} (own field {:x} kept)",
                right, left_field, g_tracked.original);
}

void give_back(uintptr_t right) {
    if (g_tracked.original == 0) {
        return;
    }

    if (g_tracked.right_state == right && g_field_offset) {
        auto& right_slot = *(uintptr_t*)(right + *g_field_offset);

        if (right_slot == g_tracked.shared) {
            right_slot = g_tracked.original;
            release(g_tracked.shared);

            SPDLOG_INFO("[SharedDistanceField] Right eye's view state {:x} has its own distance field {:x} again", right,
                        g_tracked.original);

            g_tracked = {};
            return;
        }
    }

    release(g_tracked.original);
    g_tracked = {};
}
} // namespace

void SharedDistanceField::on_view_constructed(void* view, void* view_state, uint32_t eye) {
    if (view == nullptr || view_state == nullptr) {
        return;
    }

    if (eye < 2) {
        g_eye_states[eye] = (uintptr_t)view_state;
    }

    if (g_view_state_offset.load() >= 0) {
        return;
    }

    // The constructor copies the pointer into State, and into EyeAdaptationViewState when post processing
    // is on. State is declared first, so it is the lowest match.
    constexpr size_t max_scan = 0x3000;

    for (size_t i = 0; i < max_scan; i += sizeof(void*)) {
        const auto addr = (uintptr_t)view + i;

        if ((i == 0 || (addr & 0xFFF) == 0) && IsBadReadPtr((void*)addr, sizeof(void*))) {
            break;
        }

        if (*(void**)addr == view_state) {
            g_view_state_offset = (int32_t)i;
            SPDLOG_INFO("[SharedDistanceField] FSceneView::State at 0x{:x}", i);
            return;
        }
    }

    SPDLOG_ERROR_ONCE("[SharedDistanceField] The view state was not found in a constructed FSceneView");
}

void SharedDistanceField::on_pre_render(sdk::FSceneViewFamily& view_family, bool wanted) {
    const auto state_offset = g_view_state_offset.load();

    if (state_offset < 0) {
        return;
    }

    const auto left = g_eye_states[0].load();
    const auto right = g_eye_states[1].load();

    if (left == 0 || right == 0 || left == right) {
        return;
    }

    const auto views = view_family.get_views();

    if (views == nullptr || views->data == nullptr || views->count <= 0 || views->count > 8) {
        return;
    }

    // The eyes may come in one family or, as in Stalker 2, one family each, one after the other. The
    // scene's list of modified primitives is reset after every family, so with a field per eye only the
    // eye rendered first ever hears of a change.
    ++g_calls;

    bool has_eye = false;

    for (int32_t i = 0; i < views->count; ++i) {
        const auto state = *(uintptr_t*)((uintptr_t)views->data[i] + state_offset);

        for (uint32_t eye = 0; eye < 2; ++eye) {
            if (state == (eye == 0 ? left : right)) {
                g_seen[eye] = {state, g_calls};
                has_eye = true;
            }
        }
    }

    const auto recently_seen = [&](uint32_t eye, uintptr_t state) {
        return g_seen[eye].state == state && g_calls - g_seen[eye].call <= max_calls_apart;
    };

    if (!has_eye || !recently_seen(0, left) || !recently_seen(1, right)) {
        return;
    }

    if (!wanted) {
        give_back(right);
        return;
    }

    if (const auto offset = field_offset(left, right); offset) {
        share(left, right, *offset);
    }
}
