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
static void ApplicationEvent(PFAD_HW_INDEP_INFO gpDev, FAD_EVENT_E event);

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
	if(gpDev->bHasLaser){
		ret = request_irq(gpio_to_irq(pin), fadLaserIST,
				  IRQF_TRIGGER_HIGH | IRQF_ONESHOT, 
				  "LaserON", gpDev);
	}
	if(ret){
		pr_err("flridrv-fad: Failed to register interrupt for laser...\n");
	} else{
		pr_info("flirdrv-fad: Registered interrupt %i for laser\n", gpio_to_irq(pin));
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
	if (gpDev->bHasLaser){
		free_irq(gpio_to_irq(pin), gpDev);
	}
}


/** 
 * InitDigitalIOIrq
 * 
 * Initialize digital io irq if system requires it
 *
 * @param gpDev 
 * 
 * @return retval
 */
int InitDigitalIOIrq(PFAD_HW_INDEP_INFO gpDev)
{
	int ret = 0;
	if (gpDev->bHasDigitalIO) {
		if(system_is_roco()){
		} else {
			ret = request_irq(gpio_to_irq(DIGIN_1), fadDigIN1IST,
				  IRQF_TRIGGER_HIGH | IRQF_ONESHOT, "Digin1", gpDev);
		}
	}
	return ret;
}

void FreeDigitalIOIrq(PFAD_HW_INDEP_INFO gpDev)
{
	if (gpDev->bHasLaser){
		if(system_is_roco()) {

		} else {
			free_irq(gpio_to_irq(DIGIN_1), gpDev);
		}
	}
}

void ApplicationEvent(PFAD_HW_INDEP_INFO gpDev, FAD_EVENT_E event)
{
	gpDev->eEvent = event;
	wake_up_interruptible(&gpDev->wq);
}

irqreturn_t fadDigIN1IST(int irq, void *dev_id)
{
	PFAD_HW_INDEP_INFO gpDev = (PFAD_HW_INDEP_INFO) dev_id;
	static BOOL bWaitForNeg = FALSE;

	ApplicationEvent(gpDev, FAD_DIGIN_EVENT);
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
	PFAD_HW_INDEP_INFO gpDev = (PFAD_HW_INDEP_INFO) dev_id;
	static BOOL bWaitForNeg;
	int pin;
#ifdef CONFIG_OF
	pin = gpDev->laser_on_gpio;
#else
	pin = LASER_ON
#endif

	ApplicationEvent(gpDev, FAD_LASER_EVENT);
	if (bWaitForNeg) {
		irq_set_irq_type(gpio_to_irq(pin),
				 IRQF_TRIGGER_HIGH | IRQF_ONESHOT);
		bWaitForNeg = FALSE;
	} else {
		irq_set_irq_type(gpio_to_irq(LASER_ON),
				 IRQF_TRIGGER_LOW | IRQF_ONESHOT);
		bWaitForNeg = TRUE;
	}

	return IRQ_HANDLED;
}
