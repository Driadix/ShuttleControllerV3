// Static RTOS kernel variant (issue 10: static RTOS candidate). Compiled only
// when V3_KERNEL_RTOS is defined.
//
// Kernel API mapping (the shared contract from execution_core.h):
// - init(): FreeRTOS setup + static task pool (xTaskCreateStatic, zero dynamic
//   allocation - configSUPPORT_DYNAMIC_ALLOCATION=0).
// - schedule(fn, ctx, deadline): bounded static work queue drained by the
//   control task; steps keep the bounded run-to-completion semantics.
// - on_tick(): called from vApplicationTickHook (SysTick ISR context) - light
//   accounting only, NO step execution and NO watchdog reload from ISR (a
//   reload in the tick hook would mask task starvation, breaking the F5 test).
// - watchdog reload: task step boundaries + vApplicationIdleHook
//   (INV-WATCHDOG-ARMED; a spinning task never reaches a step boundary).
// - Force-stop (obligation #3/#13): the bumper ISR notifies the safety task
//   (xTaskNotifyFromISR); emission happens in the safety task at the highest
//   priority - RTOS semantics (deferred to task context with preemption by
//   priority), distinct from the hybrid variant's ISR-context emission. The
//   comparison captures this difference.
// - Tick source: the STM32duino core owns SysTick (HAL tick); our override of
//   the weak osSystickHandler() (SrcWrapper/clock.c) forwards to
//   xPortSysTickHandler(). The port defines no SysTick_Handler, so there is
//   no collision.
#pragma once

#include <cstdint>

#include "platform/execution_core.h"

namespace slice
{
namespace kernel
{
namespace rtos
{

// Registers a static task. `priority` is the FreeRTOS priority (1..configMAX_PRIORITIES-1;
// higher = more urgent); `stack_words` is the static stack size in 32-bit words.
// Returns false when the static task pool is exhausted.
bool add_task(const char* name, StepFn fn, void* ctx, std::uint32_t priority, std::uint32_t stack_words);

// Bumper ISR entry: notifies the safety task (deferred to task context).
void bumper_notify_from_isr();

// Starts the scheduler (vTaskStartScheduler; never returns on success).
void start();

// Handle for the safety task (used by the harness to wire notifications).
void* safety_task_handle();

// Set by the harness after registering the safety task body.
void set_safety_task_handle(void* handle);

} // namespace rtos
} // namespace kernel
} // namespace slice
