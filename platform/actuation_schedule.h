// Composition-root glue for the Actuator emission step (design
// docs/safety-authority-design-v3.md sections 3.4, 5.1; ticket #71).
// Host-buildable (no Arduino): schedules the ActuatorController emission step
// via kernel::schedule; armed by the safety slot when an intent is active,
// self-re-arms from a FRESH monotonic read after each step (pattern #63 §6),
// and disarms (silence on the bus) when the intent becomes inactive (Q7.1 A).
#pragma once

#include <cstdint>

#include "domain/actuator.h"

namespace v3
{
namespace safety
{

// Composition root binds the ActuatorController before scheduling.
void actuation_bind(ActuatorController* ac);

// Вооружён ли эмиссионный шаг (для glue / safety slot coordination).
bool actuation_armed();

// Glue: вооружить шаг (schedule now+1, в окне kernel [now, now+StepBudgetMs]).
void actuation_arm(std::uint64_t now);

// kernel::schedule target (bounded step). Emits via the ActuatorController
// when intent is active, re-arms itself; otherwise disarms (silence).
void actuation_step(void* ctx);

} // namespace safety
} // namespace v3
