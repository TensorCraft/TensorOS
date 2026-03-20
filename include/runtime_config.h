#pragma once

#if !defined(CONFIG_SCHEDULER_COOPERATIVE) || !defined(CONFIG_SCHEDULER_PREEMPTIVE)
#error "Scheduler configuration macros must be defined by the build."
#endif

#if !defined(CONFIG_RUNTIME_SINGLE_FOREGROUND) || !defined(CONFIG_RUNTIME_MULTIPROCESS)
#error "Runtime mode configuration macros must be defined by the build."
#endif

#if !defined(CONFIG_TARGET_PREEMPT_SAFE_POINT_CAPABLE)
#error "Target preemption capability macro must be defined by the build."
#endif

#if ((CONFIG_SCHEDULER_COOPERATIVE != 0) + (CONFIG_SCHEDULER_PREEMPTIVE != 0)) != 1
#error "Exactly one scheduler mode must be enabled."
#endif

#if ((CONFIG_RUNTIME_SINGLE_FOREGROUND != 0) + (CONFIG_RUNTIME_MULTIPROCESS != 0)) != 1
#error "Exactly one runtime mode must be enabled."
#endif
