// Static RTOS kernel variant. Only compiled when V3_KERNEL_RTOS is defined.
//
// STATUS: implementation pending research (RtosVariant agent, issue #54).
// The env firmware-rtos has no lib_deps pin yet; this file is excluded from
// every currently buildable env (coop/hybrid/native). It will map the kernel
// API to a static FreeRTOS task set (safety / control+sensing / transport /
// observability), with bounded IPC via static queues and
// configSUPPORT_DYNAMIC_ALLOCATION=0.
//
// The kernel API contract it must implement:
//   slice::kernel::init / run / on_tick / schedule / now_ms / max_step_duration_ms
//   / max_scheduler_gap_ms / idle_ticks
#ifdef V3_KERNEL_RTOS

#error "V3_KERNEL_RTOS is not implemented yet: blocked on RtosVariant research (issue #54)"

#else
// Not built for this kernel variant.
#endif
