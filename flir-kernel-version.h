#ifndef FLIRKERNELVERSION
#define FLIRKERNELVERSION

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
#include "../arch/arm/mach-imx/hardware.h"
#ifndef __devexit
#define __devexit
#define cpu_is_imx6s   cpu_is_imx6dl
#define system_is_roco cpu_is_imx6q
#define system_is_neco cpu_is_imx6s
#endif
#else				// LINUX_VERSION_CODE
#include "mach/mx6.h"
#define cpu_is_imx6s   cpu_is_mx6dl
#define cpu_is_imx6q   cpu_is_mx6q
#define system_is_roco cpu_is_imx6q
#define system_is_neco cpu_is_imx6s
#endif

#endif
