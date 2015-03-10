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
static irqreturn_t fadDigIN1IST(int irq, void *dev_id);
static void ApplicationEvent(PFAD_HW_INDEP_INFO pInfo, FAD_EVENT_E event);

// Code

/** 
 * InitLaserIrq
 * 
 * Initialize laser irq if system hase laser
 *
 * @param pInfo 
 * 
 * @return retval
 */
int InitLaserIrq(PFAD_HW_INDEP_INFO pInfo)
{
	int ret = 0;
	int pin;
#ifdef CONFIG_OF
	pin = of_get_named_gpio_flags(pInfo->node, "laser_on-gpios", 0, NULL);
#else
	pin = LASER_ON;
#endif
	if(pInfo->bHasLaser){
		ret = request_irq(gpio_to_irq(pin), fadLaserIST,
				  IRQF_TRIGGER_HIGH | IRQF_ONESHOT, 
				  "LaserON", pInfo);
	}
	return ret;
}

void FreeLaserIrq(PFAD_HW_INDEP_INFO pInfo)
{
	int pin;
#ifdef CONFIG_OF
	if (pInfo->bHasLaser){
		pin = of_get_named_gpio_flags(pInfo->node, "laser_on-gpios", 0, NULL);
	}
#else
	pin = LASER_ON;
#endif
	if (pInfo->bHasLaser){
		free_irq(gpio_to_irq(pin), pInfo);
	}
}


/** 
 * InitDigitalIOIrq
 * 
 * Initialize digital io irq if system requires it
 *
 * @param pInfo 
 * 
 * @return retval
 */
int InitDigitalIOIrq(PFAD_HW_INDEP_INFO pInfo)
{
	int ret = 0;
	if (pInfo->bHasDigitalIO) {
		if(system_is_roco()){
		} else {
			ret = request_irq(gpio_to_irq(DIGIN_1), fadDigIN1IST,
				  IRQF_TRIGGER_HIGH | IRQF_ONESHOT, "Digin1", pInfo);
		}
	}
	return ret;
}

void FreeDigitalIOIrq(PFAD_HW_INDEP_INFO pInfo)
{
	if (pInfo->bHasLaser){
		if(system_is_roco()) {

		} else {
			free_irq(gpio_to_irq(DIGIN_1), pInfo);
		}
	}
}

void ApplicationEvent(PFAD_HW_INDEP_INFO pInfo, FAD_EVENT_E event)
{
	pInfo->eEvent = event;
	wake_up_interruptible(&pInfo->wq);
}

irqreturn_t fadDigIN1IST(int irq, void *dev_id)
{
	PFAD_HW_INDEP_INFO pInfo = (PFAD_HW_INDEP_INFO) dev_id;
	static BOOL bWaitForNeg = FALSE;

	ApplicationEvent(pInfo, FAD_DIGIN_EVENT);
	if (bWaitForNeg) {
		irq_set_irq_type(gpio_to_irq(DIGIN_1),
				 IRQF_TRIGGER_HIGH | IRQF_ONESHOT);
		bWaitForNeg = FALSE;
	} else {
		irq_set_irq_type(gpio_to_irq(DIGIN_1),
				 IRQF_TRIGGER_LOW | IRQF_ONESHOT);
		bWaitForNeg = TRUE;
	}

	pr_err("flirdrv-fad: fadDigIN1IST\n");

	return IRQ_HANDLED;
}

irqreturn_t fadLaserIST(int irq, void *dev_id)
{
	PFAD_HW_INDEP_INFO pInfo = (PFAD_HW_INDEP_INFO) dev_id;
	static BOOL bWaitForNeg;

	ApplicationEvent(pInfo, FAD_LASER_EVENT);
	if (bWaitForNeg) {
		irq_set_irq_type(gpio_to_irq(LASER_ON),
				 IRQF_TRIGGER_HIGH | IRQF_ONESHOT);
		bWaitForNeg = FALSE;
	} else {
		irq_set_irq_type(gpio_to_irq(LASER_ON),
				 IRQF_TRIGGER_LOW | IRQF_ONESHOT);
		bWaitForNeg = TRUE;
	}

	pr_err("flirdrv-fad: fadLaserIST\n");

	return IRQ_HANDLED;
}
