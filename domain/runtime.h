// Operation Runtime (design docs/operation-runtime-design-v3.md section 2.5,
// 3.3-3.4; #13 lifecycle/ownership; ticket #74). Framework-free. Owns the
// active operation instances (bounded tree of ownership, no cycles), their
// lifecycle (Accepted -> Running -> Succeeded / Stopping -> Cancelled /
// Failed), typed terminal outcomes and the exclusive slot for exclusive
// roots (I-LC-4). Advances at most ONE due instance per call (dispatch
// contract #70 section 2.1), each driver bounded by T_step (#48 section 4).
//
// #74 ships the runtime SHELL: real operation types and their drivers arrive
// with the Phase-3 capability slices; the driver contract (OperationEnv /
// DriverEvent) is fixed here. The runtime executes NO safety policy - the
// Safety Authority remains the single arbitration funnel (#43 section 3.1);
// fault_cascade() only moves the lifecycle per #13/#46.
#pragma once

#include <cstdint>

#include "domain/ports.h"
#include "domain/slot.h"

namespace v3
{
namespace runtime
{

enum class OpState : std::uint8_t
{
    Accepted = 0,
    Running = 1,
    Stopping = 2,
    Succeeded = 3,
    Cancelled = 4,
    Failed = 5,
};

struct Outcome
{
    std::uint16_t code = 0;
    std::uint32_t context = 0; // bounded diagnostic context (#13)
};

// Bounded step of an operation algorithm (Phase 3 supplies the drivers).
enum class DriverEventKind : std::uint8_t
{
    Continue = 0, // needs another step: re-scheduled by the next advance
    Yield = 1,    // parks until woken (child completion / stop / fault)
    Spawn = 2,    // requests a suboperation (type must allow the child role)
    Complete = 3, // terminal success
    Fail = 4,     // terminal failure
    Cancel = 5,   // terminal cancel - honored only while Stopping (#13)
};

// Read-only environment for a driver step (bounded, single-writer #43 §4).
struct OperationEnv
{
    std::uint64_t now = 0;
    std::uint32_t op_id = 0;
    std::uint32_t parent_op_id = 0; // 0 = root
    std::uint16_t authority_id = 0;
    bool stop_requested = false;  // true while Stopping: driver must Cancel/Fail
    bool child_terminal = false;  // last suboperation completed: child_outcome valid
    Outcome child_outcome;
};

struct DriverEvent; // forward: DriverFn is a function pointer to it

using DriverFn = DriverEvent (*)(void* ctx, const OperationEnv& env);

struct DriverEvent
{
    DriverEventKind kind = DriverEventKind::Continue;
    std::uint16_t spawn_type = 0;
    std::uint8_t spawn_params_len = 0;
    std::uint8_t spawn_params[64] = {};
    DriverFn spawn_fn = nullptr; // Spawn: child driver (Phase 3: type registry supplies it)
    void* spawn_ctx = nullptr;
    Outcome outcome;
};

// Create request mapped by the Semantic Contract from codec::OperationRequest
// (runtime does not depend on the codec; dependency matrix, design 4.1).
struct CreateRequest
{
    std::uint16_t type_id = 0;
    std::uint16_t authority_id = 0;
    std::uint32_t parent_op_id = 0; // 0 = root
    std::uint8_t role = 0;
    std::uint8_t params_len = 0;
    std::uint8_t params[64] = {};
    DriverFn fn = nullptr;                     // type registry supplies it (Phase 3)
    void* ctx = nullptr;
    slot::Activity exclusive_class = slot::Activity::Idle; // root only; Idle = no slot
};

struct Instance
{
    std::uint32_t op_id = 0;
    std::uint32_t parent_op_id = 0;
    std::uint16_t type_id = 0;
    std::uint16_t authority_id = 0;
    OpState state = OpState::Accepted;
    slot::Activity activity = slot::Activity::Idle; // claimed exclusive activity (Idle = none)
    bool parked = false;          // Yield: skipped until woken
    bool child_terminal = false;  // pending child outcome for the driver
    Outcome child_outcome;
    DriverFn fn = nullptr;
    void* ctx = nullptr;
    Outcome outcome;
    // Deferred terminal (#13: parent stays Stopping until descendants are
    // terminal and delegated resources are released): set when terminate()
    // is called while the instance still has active descendants.
    bool pending_terminal = false;
    OpState pending_state = OpState::Accepted;
    Outcome pending_outcome;
};

class Runtime
{
  public:
    static constexpr std::uint32_t MaxActiveInstances = 8; // owner decision #74 §0.2
    static constexpr std::uint32_t MaxTreeDepth = 8;

    enum class CreateResult : std::uint8_t
    {
        Accepted = 0,
        InstancesFull = 1,  // bounded storage (design 8): capacity exhausted
        TreeCycle = 2,      // parent chain would cycle (defensive; unreachable by construction)
        DepthExceeded = 3,  // ownership depth would exceed MaxTreeDepth
        ParentMissing = 4,  // parent not active (delegation unavailable)
        EdgeDenied = 5,     // type graph forbids the child role / invalid driver
        ExclusiveBusy = 6,  // exclusive slot taken (authoritative claim failed)
    };
    enum class StopResult : std::uint8_t
    {
        Accepted = 0,
        Unknown = 1,
    };

    void init(EpochSource* epoch, RuntimeEvents* events, slot::ExclusiveSlot* slot);

    // Called by the Semantic Contract AFTER all admission gates (#13 step 7).
    CreateResult create_root(const CreateRequest& r, std::uint32_t& op_id_out);
    CreateResult create_child(const CreateRequest& r, std::uint32_t& op_id_out); // Spawn from a driver

    // At most one due instance per call (bounded step; each driver <= T_step,
    // overrun observable via events). Foreground only.
    void advance(std::uint64_t now);

    // Stop intent (control plane): idempotent; root -> Stopping + cascade to
    // all descendants; parent stays Stopping until children are terminal and
    // delegated resources are released (#13).
    StopResult stop(std::uint32_t op_id);

    // Latched fault (Safety Authority): all active instances -> Stopping
    // (#46 section 10). Runtime does not apply safety policy.
    void fault_cascade();

    bool slot_held() const { return m_slot != nullptr && m_slot->current() != slot::Activity::Idle; }
    bool is_active(std::uint32_t op_id) const; // parent-alive check for admission
    std::uint32_t active_count() const { return m_count; }

    // Read-only snapshot for the observability snapshot assembler (#72):
    // operation tree summary (#49 section 2.6). Valid until next mutation.
    const Instance* snapshot(std::uint32_t& count) const
    {
        count = m_count;
        return m_instances;
    }

  private:
    std::int32_t find(std::uint32_t op_id) const;
    std::uint32_t depth_of(std::uint32_t op_id) const; // bounded walk (<= MaxTreeDepth)
    std::uint32_t next_op_id();
    void apply(const DriverEvent& ev, std::uint32_t idx);
    void terminate(std::uint32_t idx, OpState final_state, const Outcome& o);
    void finalize(std::uint32_t idx, OpState final_state, const Outcome& o);
    bool has_active_descendants(std::uint32_t op_id) const;
    void release_slot(const Instance& inst);
    void notify_parent(std::uint32_t parent_op_id, const Outcome& o);
    void mark_stopping_recursive(std::uint32_t op_id);

    Instance m_instances[MaxActiveInstances];
    std::uint32_t m_count = 0;
    std::uint32_t m_next_op_id = 0;
    std::uint32_t m_epoch = 0;
    EpochSource* m_epoch_src = nullptr;
    RuntimeEvents* m_events = nullptr;
    slot::ExclusiveSlot* m_slot = nullptr;
};

} // namespace runtime
} // namespace v3
