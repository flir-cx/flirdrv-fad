// SPDX-License-Identifier: GPL-2.0-or-later
/***********************************************************************
 * Description of file:
 *    FLIR Application Driver (FAD) IO functions for Leia
 *
 *  FADDEV Copyright : FLIR Systems AB
 ***********************************************************************/

#include "flir_kernel_os.h"
#include "faddev.h"
#include "fad_internal.h"
#include <linux/errno.h>
#include "flir-kernel-version.h"
#ifdef CONFIG_OF
#include <linux/of.h>
#endif

// Function prototypes
static BOOL set_gps_enable(BOOL on);
static BOOL get_gps_enable(BOOL *on);

static int suspend(PFAD_HW_INDEP_INFO gpDev);
static int resume(PFAD_HW_INDEP_INFO gpDev);

// Code
int Setup_ec702(PFAD_HW_INDEP_INFO gpDev)
{
#ifdef CONFIG_OF
	struct faddata *data = container_of(gpDev, struct faddata, pDev);
	struct device *dev = data->dev;
#endif

	gpDev->pSetGPSEnable = set_gps_enable;
	gpDev->pGetGPSEnable = get_gps_enable;
	gpDev->suspend = suspend;
	gpDev->resume = resume;

#ifdef CONFIG_OF
	/* Configure devices (bools) from DT */
	of_property_read_u32_index(dev->of_node, "hasGPS", 0, &gpDev->bHasGPS);
	dev_info(dev, "FAD setup ec702, hasGPS=%d\n", (int)gpDev->bHasGPS);
#endif

	return 0;
}

/**
 * Inverse setup done in Setup_ec702
 *
 * @param gpDev
 */
void InvSetup_ec702(PFAD_HW_INDEP_INFO gpDev)
{
	(void)gpDev;
}

BOOL set_gps_enable(BOOL on)
{
	// setting GPS enabled /disabled is handled through linux device PM system
	// opening/closing the tty device is enough for userspace...
	return TRUE;
}

BOOL get_gps_enable(BOOL *on)
{
	*on = TRUE;
	return TRUE;
}

int suspend(PFAD_HW_INDEP_INFO gpDev)
{
	return 0;
}

int resume(PFAD_HW_INDEP_INFO gpDev)
{
	return 0;
}
