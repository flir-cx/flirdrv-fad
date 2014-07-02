/***********************************************************************
*                                                                     
* Project: Balthazar
* $Date$
* $Author$
*
* $Id$
*
* Description of file:
*    FLIR Application Driver (FAD) handling of input interrupt pins.
*
* Last check-in changelist:
* $Change$
* 
*
*  FADDEV Copyright : FLIR Systems AB
***********************************************************************/

#include "flir_kernel_os.h"
#include "faddev.h"
#include "i2cdev.h"
#include "fad_internal.h"
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/irq.h>

// Internal function prototypes
static irqreturn_t fadLaserIST(int irq, void *dev_id);
static irqreturn_t fadDigIN1IST(int irq, void *dev_id);
static DWORD ApplicationEvent(PFAD_HW_INDEP_INFO pInfo, FAD_EVENT_E event);

// Code

BOOL InitLaserIrq(PFAD_HW_INDEP_INFO pInfo)
{
	DWORD ret;

	ret = request_irq(gpio_to_irq(LASER_ON), fadLaserIST,
					  IRQF_TRIGGER_HIGH | IRQF_ONESHOT, "LaserON", pInfo);

	if (ret != 0)
        pr_err("Failed to request Laser IRQ (%ld)\n", ret);
	else
        pr_err("Successfully requested Laser IRQ\n");

    return TRUE;
}

BOOL InitDigitalIOIrq(PFAD_HW_INDEP_INFO pInfo)
{
    DWORD ret;

    ret = request_irq(gpio_to_irq(DIGIN_1), fadDigIN1IST,
                      IRQF_TRIGGER_HIGH | IRQF_ONESHOT, "Digin1", pInfo);

    if (ret != 0)
        pr_err("Failed to request DIGIN_1 IRQ (%ld)\n", ret);
    else
        pr_err("Successfully requested DIGIN_1 IRQ\n");

	return TRUE;
}

DWORD ApplicationEvent(PFAD_HW_INDEP_INFO pInfo, FAD_EVENT_E event)
{
    pInfo->eEvent = event;
    wake_up_interruptible(&pInfo->wq);

	return ERROR_SUCCESS;
}

irqreturn_t fadDigIN1IST(int irq, void *dev_id)
{
    PFAD_HW_INDEP_INFO	pInfo = (PFAD_HW_INDEP_INFO)dev_id;
    static BOOL         bWaitForNeg = FALSE;

    ApplicationEvent(pInfo, FAD_DIGIN_EVENT);
    if (bWaitForNeg)
    {
        irq_set_irq_type(gpio_to_irq(DIGIN_1), IRQF_TRIGGER_HIGH | IRQF_ONESHOT);
        bWaitForNeg = FALSE;
    }
    else
    {
        irq_set_irq_type(gpio_to_irq(DIGIN_1), IRQF_TRIGGER_LOW | IRQF_ONESHOT);
        bWaitForNeg = TRUE;
    }

    pr_err("fadDigIN1IST\n");

	return IRQ_HANDLED;
}

irqreturn_t fadLaserIST(int irq, void *dev_id)
{
    PFAD_HW_INDEP_INFO	pInfo = (PFAD_HW_INDEP_INFO)dev_id;
    static BOOL         bWaitForNeg;

    ApplicationEvent(pInfo, FAD_LASER_EVENT);
	if (bWaitForNeg)
	{
		irq_set_irq_type(gpio_to_irq(LASER_ON), IRQF_TRIGGER_HIGH | IRQF_ONESHOT);
		bWaitForNeg = FALSE;
	}
	else
	{
		irq_set_irq_type(gpio_to_irq(LASER_ON), IRQF_TRIGGER_LOW | IRQF_ONESHOT);
		bWaitForNeg = TRUE;
	}

	pr_err("fadLaserIST\n");

    return IRQ_HANDLED;
}

