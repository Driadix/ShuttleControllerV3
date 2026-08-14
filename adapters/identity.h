// Identity adapter (design docs/observability-design-v3.md section 4.2;
// ticket #72). Read-only STM32 96-bit UID: the snapshot identity field uses
// the high 32 bits. FW version / build id / serial are placeholders until the
// release infra and provisioning (#76) - those snapshot fields stay 0.
#pragma once

#include <cstdint>

#include "domain/ports.h"

namespace v3
{

class Stm32Identity : public IdentitySource
{
  public:
    std::uint32_t hardware_id() const override;
};

} // namespace v3
