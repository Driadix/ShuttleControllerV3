/*
 * FreeRTOSConfig.h for the proving slice (issue #54) on STM32F405RG,
 * STM32duino core 2.7.1 (frozen toolchain, issue #51).
 *
 * Static allocation only: configSUPPORT_DYNAMIC_ALLOCATION=0, all tasks,
 * queues and notifications from static memory (rule R1: zero heap after init;
 * issue #48 section 8).
 *
 * Tick integration: the STM32duino core owns SysTick (HAL tick + millis) and
 * calls the weak osSystickHandler() from SysTick_Handler (SrcWrapper/clock.c).
 * This config maps the FreeRTOS port functions onto the core's interrupt
 * names; the tick itself is forwarded by our osSystickHandler() override in
 * execution_rtos.cpp. The port does not define SysTick_Handler, so there is no
 * link collision.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

/* -------- basic -------- */
#define configUSE_PREEMPTION 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE 0
#define configUSE_TIME_SLICING 1
#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 1
#define configMAX_PRIORITIES 6 /* safety > control > actuator > observability > idle */
#define configMINIMAL_STACK_SIZE 128
#define configMAX_TASK_NAME_LEN 12
#define configTICK_RATE_HZ 1000
#define configCPU_CLOCK_HZ 168000000 /* genericSTM32F405RG, verified (research 2026-08-12) */
#define configSYSTICK_CLOCK_HZ 168000000
#define configSTACK_DEPTH_TYPE uint32_t
#define configMESSAGE_BUFFER_LENGTH_TYPE size_t
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0
#define configENABLE_BACKWARD_COMPATIBILITY 0
#define configUSE_NEWLIB_REENTRANT 0

/* -------- memory (static only) -------- */
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configTOTAL_HEAP_SIZE 1024 /* unused (dynamic alloc off); belt-and-braces */

/* -------- IPC / features -------- */
#define configUSE_MUTEXES 0
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configUSE_QUEUE_SETS 0
#define configUSE_TASK_NOTIFICATIONS 1
#define configQUEUE_REGISTRY_SIZE 0
#define configUSE_TIMERS 0 /* timers.c excluded by add_config.py */

/* -------- hooks -------- */
#define configUSE_IDLE_HOOK 1 /* watchdog reload in idle (INV-WATCHDOG-ARMED) */
#define configUSE_TICK_HOOK 1 /* kernel::on_tick() from the tick */
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK 0
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0
#define configUSE_APPLICATION_TASK_TAG 0

/* -------- Cortex-M4 NVIC (4 priority bits, PRIORITYGROUP_4 set by core premain) -------- */
#define configPRIO_BITS 4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* -------- port function mapping onto core interrupt names -------- */
/* The port defines vPortSVCHandler/xPortPendSVHandler; they must take the
 * vector-table names. The core's startup provides weak SVC_Handler and
 * PendSV_Handler, which our strong definitions override. */
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
/* xPortSysTickHandler is NOT mapped to SysTick_Handler: the core owns
 * SysTick_Handler and forwards via osSystickHandler() (execution_rtos.cpp). */

/* -------- diagnostics -------- */
/* Debug configASSERT; production hardening is a later concern (issue #51
 * section 6.1 R5: invariants -> assert in debug). */
extern void slice_config_assert_failed(const char* file, int line);
#define configASSERT(x)                                             \
    do                                                              \
    {                                                               \
        if (!(x))                                                   \
        {                                                           \
            slice_config_assert_failed(__FILE__, __LINE__);         \
        }                                                           \
    } while (0)

#define INCLUDE_vTaskPrioritySet 0
#define INCLUDE_uxTaskPriorityGet 0
#define INCLUDE_vTaskDelete 0
#define INCLUDE_vTaskSuspend 0
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1 /* obligation #10: stack watermark */

#endif /* FREERTOS_CONFIG_H */
