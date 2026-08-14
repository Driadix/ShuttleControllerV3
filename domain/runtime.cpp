// Operation Runtime implementation (design
// docs/operation-runtime-design-v3.md sections 3.3-3.4; #13; ticket #74).
#include "domain/runtime.h"

#include "domain/codec.h" // OutcomeCode registry only (shared semantic constants)

namespace v3
{
namespace runtime
{

void Runtime::init(EpochSource* epoch, RuntimeEvents* events, slot::ExclusiveSlot* slot)
{
    m_epoch_src = epoch;
    m_events = events;
    m_slot = slot;
    m_count = 0;
    m_next_op_id = 0;
    m_epoch = epoch != nullptr ? epoch->epoch() : 0;
}

std::int32_t Runtime::find(std::uint32_t op_id) const
{
    for (std::uint32_t i = 0; i < m_count; ++i)
    {
        if (m_instances[i].op_id == op_id)
        {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

std::uint32_t Runtime::depth_of(std::uint32_t op_id) const
{
    // Number of ancestors of op_id (root = 0). Bounded walk (<= MaxTreeDepth).
    std::uint32_t depth = 0;
    std::int32_t idx = find(op_id);
    while (idx >= 0)
    {
        const Instance& inst = m_instances[static_cast<std::uint32_t>(idx)];
        if (inst.parent_op_id == 0)
        {
            return depth;
        }
        idx = find(inst.parent_op_id);
        ++depth;
        if (depth >= MaxTreeDepth)
        {
            return depth; // walk guard; depth overflow is rejected at create
        }
    }
    return depth;
}

std::uint32_t Runtime::next_op_id()
{
    // Monotonic within the epoch, wrap-safe, 0 never issued, never colliding
    // with a live instance (defensive; 2^32 ids per epoch without reboot).
    for (;;)
    {
        ++m_next_op_id;
        if (m_next_op_id == 0)
        {
            ++m_next_op_id;
        }
        if (find(m_next_op_id) < 0)
        {
            return m_next_op_id;
        }
    }
}

Runtime::CreateResult Runtime::create_root(const CreateRequest& r, std::uint32_t& op_id_out)
{
    if (r.parent_op_id != 0 || r.fn == nullptr)
    {
        return CreateResult::EdgeDenied; // root shape + type registry must supply a driver
    }
    if (m_count >= MaxActiveInstances)
    {
        return CreateResult::InstancesFull;
    }
    if (r.exclusive_class != slot::Activity::Idle && m_slot != nullptr &&
        !m_slot->try_claim(r.exclusive_class))
    {
        return CreateResult::ExclusiveBusy; // authoritative claim (single-threaded, no race)
    }

    Instance inst;
    inst.op_id = next_op_id();
    inst.parent_op_id = 0;
    inst.type_id = r.type_id;
    inst.authority_id = r.authority_id;
    inst.state = OpState::Accepted;
    inst.activity = r.exclusive_class;
    inst.fn = r.fn;
    inst.ctx = r.ctx;
    m_instances[m_count++] = inst;
    op_id_out = inst.op_id;
    if (m_events != nullptr)
    {
        m_events->operation_started(inst.op_id, inst.type_id);
    }
    return CreateResult::Accepted;
}

Runtime::CreateResult Runtime::create_child(const CreateRequest& r, std::uint32_t& op_id_out)
{
    if (r.parent_op_id == 0 || r.fn == nullptr)
    {
        return CreateResult::EdgeDenied;
    }
    const std::int32_t parent = find(r.parent_op_id);
    if (parent < 0)
    {
        return CreateResult::ParentMissing;
    }
    const OpState ps = m_instances[static_cast<std::uint32_t>(parent)].state;
    if (ps == OpState::Succeeded || ps == OpState::Cancelled || ps == OpState::Failed)
    {
        return CreateResult::ParentMissing; // delegation unavailable
    }
    if (m_count >= MaxActiveInstances)
    {
        return CreateResult::InstancesFull;
    }
    if (depth_of(r.parent_op_id) + 1 > MaxTreeDepth)
    {
        return CreateResult::DepthExceeded;
    }
    // Cycle guard (#13: parentOperationId must not form a direct/indirect
    // cycle): a fresh op_id has no descendants and is not an ancestor of any
    // live instance, so a cycle is unreachable by construction; the depth
    // bound + parent-active check keep the ownership tree finite and acyclic.

    Instance inst;
    inst.op_id = next_op_id();
    inst.parent_op_id = r.parent_op_id;
    inst.type_id = r.type_id;
    inst.authority_id = r.authority_id;
    inst.state = OpState::Accepted;
    inst.activity = slot::Activity::Idle;
    inst.fn = r.fn;
    inst.ctx = r.ctx;
    m_instances[m_count++] = inst;
    op_id_out = inst.op_id;
    if (m_events != nullptr)
    {
        m_events->operation_started(inst.op_id, inst.type_id);
    }
    return CreateResult::Accepted;
}

void Runtime::advance(std::uint64_t now)
{
    // At most one due instance per call (dispatch contract #70 section 2.1);
    // FIFO by insertion (array order preserved on removal).
    for (std::uint32_t i = 0; i < m_count; ++i)
    {
        Instance& inst = m_instances[i];
        if (inst.state != OpState::Accepted && inst.state != OpState::Running &&
            inst.state != OpState::Stopping)
        {
            continue;
        }
        if (inst.fn == nullptr)
        {
            continue; // defensive: type registry invariant
        }
        if (inst.parked && !inst.child_terminal && inst.state != OpState::Stopping)
        {
            continue; // Yield: parked until woken (child completion / stop / fault)
        }

        OperationEnv env;
        env.now = now;
        env.op_id = inst.op_id;
        env.parent_op_id = inst.parent_op_id;
        env.authority_id = inst.authority_id;
        env.stop_requested = inst.state == OpState::Stopping;
        env.child_terminal = inst.child_terminal;
        env.child_outcome = inst.child_outcome;
        inst.child_terminal = false; // consumed by this step

        DriverEvent ev = inst.fn(inst.ctx, env);
        apply(ev, i);
        return;
    }
}

void Runtime::apply(const DriverEvent& ev, std::uint32_t idx)
{
    Instance& inst = m_instances[idx];
    switch (ev.kind)
    {
    case DriverEventKind::Continue:
        if (inst.state == OpState::Accepted)
        {
            inst.state = OpState::Running;
        }
        inst.parked = false;
        break;

    case DriverEventKind::Yield:
        if (inst.state == OpState::Accepted)
        {
            inst.state = OpState::Running;
        }
        inst.parked = true;
        break;

    case DriverEventKind::Spawn:
    {
        if (inst.state == OpState::Stopping)
        {
            break; // no new children while stopping (#13)
        }
        if (inst.state == OpState::Accepted)
        {
            inst.state = OpState::Running;
        }
        CreateRequest cr;
        cr.type_id = ev.spawn_type;
        cr.authority_id = inst.authority_id;
        cr.parent_op_id = inst.op_id;
        const std::uint8_t plen = ev.spawn_params_len <= 64 ? ev.spawn_params_len : static_cast<std::uint8_t>(64);
        cr.params_len = plen;
        for (std::uint8_t k = 0; k < plen; ++k)
        {
            cr.params[k] = ev.spawn_params[k];
        }
        // NOTE: Phase 3 supplies the child driver from the type registry; the
        // shell inherits the parent driver as a placeholder.
        cr.fn = inst.fn;
        cr.ctx = inst.ctx;
        const CreateResult res = create_child(cr);
        if (res != CreateResult::Accepted)
        {
            // Failed spawn -> the parent sees a failed child outcome on its
            // next step (Phase 3 refines the failure contract).
            inst.child_terminal = true;
            inst.child_outcome.code = static_cast<std::uint16_t>(codec::OutcomeCode::FailedGeneric);
            inst.child_outcome.context = static_cast<std::uint32_t>(res);
        }
        break;
    }

    case DriverEventKind::Complete:
        terminate(idx, OpState::Succeeded, ev.outcome);
        break;
    case DriverEventKind::Fail:
        terminate(idx, OpState::Failed, ev.outcome);
        break;
    case DriverEventKind::Cancel:
        if (inst.state == OpState::Stopping)
        {
            terminate(idx, OpState::Cancelled, ev.outcome);
        }
        // Cancel outside Stopping is a type-contract violation: ignored
        // (lifecycle transitions are guarded, I-LC-2).
        break;
    }
}

void Runtime::terminate(std::uint32_t idx, OpState final_state, const Outcome& o)
{
    Instance& inst = m_instances[idx];
    inst.state = final_state;
    inst.outcome = o;
    inst.parked = false;
    const std::uint32_t op_id = inst.op_id;
    const std::uint16_t type_id = inst.type_id;
    const std::uint32_t parent_op_id = inst.parent_op_id;
    release_slot(inst);
    for (std::uint32_t k = idx; k + 1 < m_count; ++k)
    {
        m_instances[k] = m_instances[k + 1]; // shift keeps FIFO insertion order
    }
    --m_count;
    if (m_events != nullptr)
    {
        m_events->operation_terminal(op_id, type_id, o.code);
    }
    notify_parent(parent_op_id, o);
}

void Runtime::release_slot(const Instance& inst)
{
    if (inst.activity != slot::Activity::Idle && m_slot != nullptr)
    {
        m_slot->release(inst.activity);
    }
}

void Runtime::notify_parent(std::uint32_t parent_op_id, const Outcome& o)
{
    if (parent_op_id == 0)
    {
        return;
    }
    const std::int32_t p = find(parent_op_id);
    if (p < 0)
    {
        return; // parent already terminal (it stays Stopping until us, #13)
    }
    Instance& pinst = m_instances[static_cast<std::uint32_t>(p)];
    pinst.child_terminal = true;
    pinst.child_outcome = o;
    pinst.parked = false; // wake
}

Runtime::StopResult Runtime::stop(std::uint32_t op_id)
{
    const std::int32_t idx = find(op_id);
    if (idx < 0)
    {
        return StopResult::Unknown;
    }
    Instance& inst = m_instances[static_cast<std::uint32_t>(idx)];
    if (inst.state == OpState::Accepted || inst.state == OpState::Running)
    {
        inst.state = OpState::Stopping;
        inst.parked = false; // Stopping must advance to deliver Cancel/Fail
        mark_stopping_recursive(op_id);
    }
    // Stopping/terminal: idempotent no-op (repeat stop is idempotent, #13).
    return StopResult::Accepted;
}

void Runtime::mark_stopping_recursive(std::uint32_t op_id)
{
    for (std::uint32_t i = 0; i < m_count; ++i)
    {
        Instance& inst = m_instances[i];
        if (inst.state != OpState::Accepted && inst.state != OpState::Running)
        {
            continue;
        }
        std::uint32_t a = inst.parent_op_id;
        while (a != 0)
        {
            if (a == op_id)
            {
                inst.state = OpState::Stopping;
                inst.parked = false;
                break;
            }
            const std::int32_t p = find(a);
            if (p < 0)
            {
                break;
            }
            a = m_instances[static_cast<std::uint32_t>(p)].parent_op_id;
        }
    }
}

void Runtime::fault_cascade()
{
    // Latched fault: all active instances -> Stopping (#46 section 10).
    for (std::uint32_t i = 0; i < m_count; ++i)
    {
        Instance& inst = m_instances[i];
        if (inst.state == OpState::Accepted || inst.state == OpState::Running)
        {
            inst.state = OpState::Stopping;
            inst.parked = false;
        }
    }
}

bool Runtime::is_active(std::uint32_t op_id) const
{
    const std::int32_t idx = find(op_id);
    if (idx < 0)
    {
        return false;
    }
    const OpState s = m_instances[static_cast<std::uint32_t>(idx)].state;
    return s == OpState::Accepted || s == OpState::Running || s == OpState::Stopping;
}

} // namespace runtime
} // namespace v3
