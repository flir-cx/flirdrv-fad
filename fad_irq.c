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


// Internal function prototypes
static irqreturn_t fadLaserIST(int irq, void *dev_id);
static irqreturn_t fadDigIN1IST(int irq, void *dev_id);
static DWORD ApplicationEvent(PFAD_HW_INDEP_INFO pInfo, FAD_EVENT_E event);

// Code

/** 
 * InitLaserIrq
 * 
 * Initialize laser irq if system hase laser
 *
 * @param gpDev 
 * 
 * @return TRUE if init ok, or if system does not have a laser
 */
BOOL InitLaserIrq(PFAD_HW_INDEP_INFO gpDev)
{
	int ret = 0;
	if (gpDev->bHasLaser){
		if(! system_is_roco()) {
			ret = request_irq(gpio_to_irq(LASER_ON), fadLaserIST,
					  IRQF_TRIGGER_HIGH | IRQF_ONESHOT, "LaserON", gpDev);
		}
	}
	return ret;
}

/** 
 * InitDigitalIOIrq
 * 
 * Initialize digital io irq if system requires it
 *
 * @param gpDev 
 * 
 * @return  TRUE if init ok, or if system does not have/use digital io for this
 */
BOOL InitDigitalIOIrq(PFAD_HW_INDEP_INFO gpDev)
{
	int ret = 0;
	if (gpDev->bHasDigitalIO) {
		if(! system_is_roco()){
			ret = request_irq(gpio_to_irq(DIGIN_1), fadDigIN1IST,
				  IRQF_TRIGGER_HIGH | IRQF_ONESHOT, "Digin1", gpDev);
		}
	}
	return ret;
}

DWORD ApplicationEvent(PFAD_HW_INDEP_INFO pInfo, FAD_EVENT_E event)
{
	pInfo->eEvent = event;
	wake_up_interruptible(&pInfo->wq);

	return ERROR_SUCCESS;
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

	pr_err("fadDigIN1IST\n");

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

	pr_err("fadLaserIST\n");

	return IRQ_HANDLED;
}
