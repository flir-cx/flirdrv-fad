/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FLIRKERNELVERSION
#define FLIRKERNELVERSION

#if KERNEL_VERSION(3, 10, 0) <= LINUX_VERSION_CODE
#include "../arch/arm/mach-imx/hardware.h"
#else
#include "mach/mx6.h"
#endif
#endif
