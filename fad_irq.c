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
#include "flir-kernel-version.h"
#include "linux/of_gpio.h"

// Internal function prototypes
static irqreturn_t fadLaserIST(int irq, void *dev_id);

// Code

/** 
 * InitLaserIrq
 * 
 * Initialize laser irq if system hase laser
 *
 * @param gpDev 
 * 
 * @return retval
 */
int InitLaserIrq(PFAD_HW_INDEP_INFO gpDev)
{
	int ret = 0;
	int pin;
#ifdef CONFIG_OF
	pin = gpDev->laser_on_gpio;
#else
	pin = LASER_ON;
#endif
	if (gpDev->bHasLaser) {
		ret = request_irq(gpio_to_irq(pin), fadLaserIST,
				  IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
				  "LaserON", gpDev);
	}
	if (ret) {
		pr_err
		    ("flridrv-fad: Failed to register interrupt for laser...\n");
	} else {
		pr_debug("flirdrv-fad: Registered interrupt %i for laser\n",
			 gpio_to_irq(pin));
	}
	return ret;
}

void FreeLaserIrq(PFAD_HW_INDEP_INFO gpDev)
{
	int pin;
#ifdef CONFIG_OF
	pin = gpDev->laser_on_gpio;
#else
	pin = LASER_ON;
#endif
	if (gpDev->bHasLaser) {
		free_irq(gpio_to_irq(pin), gpDev);
	}
}

void ApplicationEvent(PFAD_HW_INDEP_INFO gpDev, FAD_EVENT_E event)
{
	gpDev->eEvent = event;
	wake_up_interruptible(&gpDev->wq);
}

irqreturn_t fadLaserIST(int irq, void *dev_id)
{
	PFAD_HW_INDEP_INFO gpDev = (PFAD_HW_INDEP_INFO) dev_id;
	/* static BOOL bWaitForNeg; */
/* 	int pin; */
/* #ifdef CONFIG_OF */
/* 	pin = gpDev->laser_on_gpio; */
/* #else */
/* 	pin = LASER_ON */
/* #endif */
	ApplicationEvent(gpDev, FAD_LASER_EVENT);
	/* if (bWaitForNeg) { */
	/*      irq_set_irq_type(gpio_to_irq(pin), */
	/*                       IRQF_TRIGGER_LOW | IRQF_ONESHOT); */
	/*      bWaitForNeg = FALSE; */
	/* } else { */
	/*      irq_set_irq_type(gpio_to_irq(LASER_ON), */
	/*                       IRQF_TRIGGER_HIGH | IRQF_ONESHOT); */
	/*      bWaitForNeg = TRUE; */
	/* } */

	return IRQ_HANDLED;
}

irqreturn_t fadTriggerIST(int irq, void *dev_id)
{
	PFAD_HW_INDEP_INFO gpDev = (PFAD_HW_INDEP_INFO) dev_id;
	sysfs_notify(&gpDev->pLinuxDevice->dev.kobj, NULL, "trigger_poll");
	return IRQ_HANDLED;
}
