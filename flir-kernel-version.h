#ifndef FLIRKERNELVERSION
#define FLIRKERNELVERSION

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
#include "../arch/arm/mach-imx/hardware.h"
#else
#include "mach/mx6.h"
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
#define cpu_is_imx6s()  false
#else
#define cpu_is_imx6s   cpu_is_imx6dl
#endif

#endif
