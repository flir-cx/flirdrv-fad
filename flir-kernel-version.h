/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FLIRKERNELVERSION
#define FLIRKERNELVERSION

#if KERNEL_VERSION(3, 10, 0) <= LINUX_VERSION_CODE
#include "../arch/arm/mach-imx/hardware.h"
#else
#include "mach/mx6.h"
#endif

/* #if KERNEL_VERSION(4, 9, 0) <= LINUX_VERSION_CODE */
/* #define cpu_is_imx6s()  false */
/* #else */
/* #define cpu_is_imx6s   cpu_is_imx6dl */
/* #endif */

#endif
