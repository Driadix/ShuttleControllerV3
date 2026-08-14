// Operation Runtime tests (design docs/operation-runtime-design-v3.md section
// 7.3 T27-T34; #13): operationId allocation, ownership tree, lifecycle,
// stop propagation + deferred parent terminal, fault cascade, bounded advance,
// suboperation outcome propagation, exclusive slot.
#include <cstdint>

#include <gtest/gtest.h>

#include "domain/runtime.h"
#include "tests/common/semantic_fakes.h"

namespace
{

using v3::runtime::CreateRequest;
using CreateResult = v3::runtime::Runtime::CreateResult;
using v3::runtime::DriverFn;
using v3::runtime::OpState;
using v3::runtime::Runtime;

struct RtEnv
{
    test::FakeEpoch epoch;
    test::RecordingEvents events;
    v3::slot::ExclusiveSlot slot;
    Runtime rt;

    RtEnv() { rt.init(&epoch, &events, &slot); }

    CreateRequest make(std::uint16_t type, std::uint32_t parent, DriverFn fn,
                       v3::slot::Activity act = v3::slot::Activity::Idle, void* ctx = nullptr)
    {
        CreateRequest r;
        r.type_id = type;
        r.authority_id = 5;
        r.parent_op_id = parent;
        r.fn = fn;
        r.ctx = ctx;
        r.exclusive_class = act;
        return r;
    }
};

TEST(Runtime, OperationIdsUniqueAndNonZero)
{
    RtEnv env;
    std::uint32_t seen[Runtime::MaxActiveInstances] = {};
    for (std::uint32_t i = 0; i < Runtime::MaxActiveInstances; ++i)
    {
        std::uint32_t op = 0;
        EXPECT_EQ(env.rt.create_root(env.make(1, 0, test::driver_yield), op),
                  CreateResult::Accepted);
        EXPECT_NE(op, 0u);
        for (std::uint32_t k = 0; k < i; ++k)
        {
            EXPECT_NE(seen[k], op);
        }
        seen[i] = op;
    }
    // Capacity: 9th root -> InstancesFull (bounded storage, design section 8).
    std::uint32_t op = 0;
    EXPECT_EQ(env.rt.create_root(env.make(1, 0, test::driver_yield), op),
              CreateResult::InstancesFull);
}

TEST(Runtime, OwnershipTreeAndGuards)
{
    RtEnv env;
    std::uint32_t root = 0;
    ASSERT_EQ(env.rt.create_root(env.make(1, 0, test::driver_yield), root), CreateResult::Accepted);

    // Child of an active parent: ok.
    std::uint32_t child = 0;
    EXPECT_EQ(env.rt.create_child(env.make(2, root, test::driver_yield), child),
              CreateResult::Accepted);

    // Child of a MISSING parent: ParentMissing (delegation unavailable, #13).
    std::uint32_t op = 0;
    EXPECT_EQ(env.rt.create_child(env.make(2, 0xFFFFu, test::driver_yield), op),
              CreateResult::ParentMissing);

    // Invalid shapes: child with parent 0 via create_child, root with parent != 0.
    EXPECT_EQ(env.rt.create_child(env.make(2, 0, test::driver_yield), op), CreateResult::EdgeDenied);
    CreateRequest bad_root = env.make(1, 5, test::driver_yield);
    EXPECT_EQ(env.rt.create_root(bad_root, op), CreateResult::EdgeDenied);
    // No driver: EdgeDenied (type registry must supply it).
    CreateRequest no_drv = env.make(1, 0, nullptr);
    EXPECT_EQ(env.rt.create_root(no_drv, op), CreateResult::EdgeDenied);
}

TEST(Runtime, ChildOfTerminalParentRejected)
{
    RtEnv env;
    std::uint32_t done_root = 0;
    ASSERT_EQ(env.rt.create_root(env.make(3, 0, test::driver_complete), done_root),
              CreateResult::Accepted);
    env.rt.advance(1000); // done_root -> Succeeded (no children: immediate)
    EXPECT_FALSE(env.rt.is_active(done_root));
    std::uint32_t op = 0;
    EXPECT_EQ(env.rt.create_child(env.make(2, done_root, test::driver_yield), op),
              CreateResult::ParentMissing);
}

TEST(Runtime, LifecycleTransitions)
{
    // Each transition in a fresh runtime: advance processes the FIRST active
    // instance (FIFO), so mixing instances would serialize the wrong one.
    {
        RtEnv env; // Accepted -> Running (Continue driver stays active)
        std::uint32_t op = 0;
        ASSERT_EQ(env.rt.create_root(env.make(1, 0, test::driver_continue), op), CreateResult::Accepted);
        env.rt.advance(1000);
        ASSERT_TRUE(env.rt.is_active(op));
    }
    {
        RtEnv env; // Accepted -> Running -> Succeeded (immediate complete)
        std::uint32_t op2 = 0;
        ASSERT_EQ(env.rt.create_root(env.make(1, 0, test::driver_complete), op2), CreateResult::Accepted);
        env.rt.advance(1001);
        EXPECT_FALSE(env.rt.is_active(op2));
        EXPECT_EQ(env.events.terminal_count, 1u);
        EXPECT_EQ(env.events.terminal[0].op, op2);
        EXPECT_EQ(env.events.terminal[0].code, 100u);
    }
    {
        RtEnv env; // Running -> Failed
        std::uint32_t op3 = 0;
        ASSERT_EQ(env.rt.create_root(env.make(1, 0, test::driver_fail), op3), CreateResult::Accepted);
        env.rt.advance(1002);
        EXPECT_FALSE(env.rt.is_active(op3));
        EXPECT_EQ(env.events.terminal_count, 1u);
        EXPECT_EQ(env.events.terminal[0].code, 2u);
    }
}

TEST(Runtime, ExclusiveSlotClaimAndRelease)
{
    RtEnv env;
    EXPECT_FALSE(env.rt.slot_held());
    std::uint32_t op = 0;
    ASSERT_EQ(env.rt.create_root(env.make(1, 0, test::driver_cancel_on_stop, v3::slot::Activity::Motion), op),
              CreateResult::Accepted);
    EXPECT_TRUE(env.rt.slot_held());
    // A second exclusive root: ExclusiveBusy (I-LC-4).
    std::uint32_t op2 = 0;
    EXPECT_EQ(env.rt.create_root(env.make(1, 0, test::driver_cancel_on_stop, v3::slot::Activity::Motion), op2),
              CreateResult::ExclusiveBusy);
    // Release on terminal (Cancelled).
    env.rt.stop(op);
    env.rt.advance(2000);
    EXPECT_FALSE(env.rt.is_active(op));
    EXPECT_FALSE(env.rt.slot_held());
}

TEST(Runtime, StopCascadesAndParentWaitsForChildren)
{
    RtEnv env;
    std::uint32_t root = 0;
    ASSERT_EQ(env.rt.create_root(env.make(1, 0, test::driver_cancel_on_stop), root),
              CreateResult::Accepted);
    std::uint32_t child = 0;
    ASSERT_EQ(env.rt.create_child(env.make(2, root, test::driver_cancel_on_stop), child),
              CreateResult::Accepted);

    // stop(root): both -> Stopping (cascade, #13).
    EXPECT_EQ(env.rt.stop(root), Runtime::StopResult::Accepted);
    EXPECT_EQ(env.rt.stop(0xFFFFu), Runtime::StopResult::Unknown); // unknown id
    EXPECT_EQ(env.rt.stop(root), Runtime::StopResult::Accepted);   // idempotent

    env.rt.advance(3000); // first active = root: Cancel -> deferred (child active)
    EXPECT_TRUE(env.rt.is_active(root)); // parent stays Stopping until child terminal
    env.rt.advance(3001); // child: Cancel -> Cancelled; resolves the parent
    EXPECT_FALSE(env.rt.is_active(child));
    EXPECT_FALSE(env.rt.is_active(root));
    EXPECT_EQ(env.events.terminal_count, 2u);
    EXPECT_EQ(env.events.terminal[0].op, child); // child terminal first
    EXPECT_EQ(env.events.terminal[1].op, root);
    EXPECT_EQ(env.events.terminal[1].code, 1u); // Cancelled
}

TEST(Runtime, StoppingToFailedWhenSafeStopCannotComplete)
{
    RtEnv env;
    std::uint32_t op = 0;
    ASSERT_EQ(env.rt.create_root(env.make(1, 0, test::driver_fail_on_stop), op),
              CreateResult::Accepted);
    env.rt.advance(1000);
    EXPECT_TRUE(env.rt.is_active(op));
    env.rt.stop(op);
    env.rt.advance(1001);
    EXPECT_FALSE(env.rt.is_active(op));
    EXPECT_EQ(env.events.terminal[0].code, 3u); // Failed (safe stop could not complete)
}

TEST(Runtime, FaultCascadeMovesActiveInstancesToStopping)
{
    RtEnv env;
    std::uint32_t root = 0;
    ASSERT_EQ(env.rt.create_root(env.make(1, 0, test::driver_cancel_on_stop), root),
              CreateResult::Accepted);
    std::uint32_t child = 0;
    ASSERT_EQ(env.rt.create_child(env.make(2, root, test::driver_cancel_on_stop), child),
              CreateResult::Accepted);
    env.rt.advance(1000); // root Running
    env.rt.advance(1001); // child Running

    env.rt.fault_cascade(); // latched fault (#46 section 10)
    env.rt.advance(2000);   // root: Cancel -> deferred
    env.rt.advance(2001);   // child: Cancel -> Cancelled; root resolved
    EXPECT_FALSE(env.rt.is_active(root));
    EXPECT_FALSE(env.rt.is_active(child));
    EXPECT_EQ(env.events.terminal_count, 2u);
}

TEST(Runtime, AtMostOneInstancePerAdvance)
{
    RtEnv env;
    std::uint32_t counters[3] = {0, 0, 0};
    std::uint32_t op = 0;
    ASSERT_EQ(env.rt.create_root(env.make(1, 0, test::driver_count, v3::slot::Activity::Idle, &counters[0]), op),
              CreateResult::Accepted);
    ASSERT_EQ(env.rt.create_root(env.make(1, 0, test::driver_count, v3::slot::Activity::Idle, &counters[1]), op),
              CreateResult::Accepted);
    ASSERT_EQ(env.rt.create_root(env.make(1, 0, test::driver_count, v3::slot::Activity::Idle, &counters[2]), op),
              CreateResult::Accepted);

    env.rt.advance(5000); // exactly ONE due instance per call (dispatch contract)
    EXPECT_EQ(counters[0], 1u);
    EXPECT_EQ(counters[1], 0u);
    EXPECT_EQ(counters[2], 0u);
}

TEST(Runtime, SuboperationOutcomePropagatesToParent)
{
    RtEnv env;
    test::SpawnCtx ctx;
    std::uint32_t root = 0;
    ASSERT_EQ(env.rt.create_root(env.make(1, 0, test::driver_spawn_once,
                                          v3::slot::Activity::Idle, &ctx),
                                 root),
              CreateResult::Accepted);

    env.rt.advance(6000); // root spawns the child
    EXPECT_TRUE(env.rt.is_active(root));
    env.rt.advance(6001); // root: Yield -> parked (child not yet terminal)
    env.rt.advance(6002); // child: Complete -> notifies the root (wake)
    EXPECT_TRUE(env.rt.is_active(root));
    env.rt.advance(6003); // root: child_terminal -> Complete (Succeeded, code 7)
    EXPECT_FALSE(env.rt.is_active(root));
    EXPECT_TRUE(ctx.saw_child_terminal);
    EXPECT_EQ(ctx.child_code, 100u); // child outcome (driver_complete) propagated
    EXPECT_EQ(env.events.terminal_count, 2u);
    EXPECT_EQ(env.events.terminal[0].code, 100u); // child terminal
    EXPECT_EQ(env.events.terminal[1].code, 7u);   // parent terminal
}

TEST(Runtime, DeferredParentNeverRedrivenWithTwoChildren)
{
    // #13: parent stays Stopping until ALL descendants are terminal; the
    // deferred parent's driver is NEVER re-invoked (review MAJOR-1 fix).
    RtEnv env;
    test::SpawnTwoCtx ctx;
    std::uint32_t root = 0;
    ASSERT_EQ(env.rt.create_root(env.make(1, 0, test::driver_spawn_two,
                                          v3::slot::Activity::Idle, &ctx),
                                 root),
              CreateResult::Accepted);
    env.rt.advance(1000); // root -> Spawn C1
    env.rt.advance(1001); // root -> Spawn C2
    env.rt.advance(1002); // root: Yield -> parked (no child outcome yet)
    env.rt.advance(1003); // C1: Complete -> notify root (wake: root not pending yet)
    env.rt.advance(1004); // root: child_terminal -> Complete -> DEFERRED (C2 active, parked)
    EXPECT_TRUE(env.rt.is_active(root)); // stays Stopping while C2 is active
    env.rt.advance(1005); // C2: Complete -> notify -> resolve deferred root
    EXPECT_FALSE(env.rt.is_active(root));
    EXPECT_FALSE(env.rt.is_active(0u)); // no instances left
    EXPECT_EQ(env.events.terminal_count, 3u);
    // Root terminates Succeeded (code 7), NOT Cancelled: the deferred driver
    // was never re-run with stop_requested (review MAJOR-1 scenario).
    EXPECT_EQ(env.events.terminal[2].code, 7u);
}

} // namespace
