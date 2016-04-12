/***********************************************************************
* Project: Balthazar
* $Date$
* $Author$
*
* $Id$
*
* Description of file:
*    FLIR Application Driver (FAD) IO functions.
*
* Last check-in changelist:
* $Change$
*
*  FADDEV Copyright : FLIR Systems AB
***********************************************************************/

#include "flir_kernel_os.h"
#include "faddev.h"
#include "fad_internal.h"
#include <linux/errno.h>
#include <linux/leds.h>
#include "flir-kernel-version.h"
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/of_regulator.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/ca111.h>
#include <linux/input.h>

// Definitions
#define ENOLASERIRQ 1
#define ENODIGIOIRQ 2
// Local variables

// Function prototypes

static void setLaserStatus(PFAD_HW_INDEP_INFO gpDev, BOOL on);
static void getLaserStatus(PFAD_HW_INDEP_INFO gpDev,
			   PFADDEVIOCTLLASER pLaserStatus);

// Code
void startmeasure(void);

int SetupMX6Platform(PFAD_HW_INDEP_INFO gpDev)
{
	int retval = -1;
//	u32 tmp;
//	extern struct list_head leds_list;
//	extern struct rw_semaphore leds_list_lock;
//	struct led_classdev *led_cdev;
	struct device *dev = &gpDev->pLinuxDevice->dev;
	gpDev->node = of_find_compatible_node(NULL, NULL, "flir,fad");
	gpDev->pSetLaserStatus = setLaserStatus;
	gpDev->pGetLaserStatus = getLaserStatus;
	startmeasure();
	/* Configure devices (bools) from DT */
	//Do not care about return value of function
	//If property is missing, assume device doesnt exist!
	//Better to wrap this in separate function... (int -> bool etc...)
	of_property_read_u32_index(gpDev->node,
				   "hasLaser", 0, &gpDev->bHasLaser);

	if (gpDev->bHasLaser) {
		int pin;
		pin = of_get_named_gpio_flags(gpDev->node,
					      "laser_on-gpios", 0, NULL);
		if (gpio_is_valid(pin) == 0){
			pr_err("flirdrv-fad: LaserON can not be used\n");
		} else {
			pr_debug("%s: laser_on_gpio %i\n", __func__, pin);
			gpDev->laser_on_gpio = pin;
			gpio_request(pin, "LaserON");
			gpio_direction_input(pin);
			retval = InitLaserIrq(gpDev);
			if (retval) {
				pr_err("flirdrv-fad: Failed to request Laser IRQ\n");
				retval = -ENOLASERIRQ;
				goto EXIT_NO_LASERIRQ;
			}
		}
	}

	gpDev->reg_optics_power = devm_regulator_get(dev, "optics_power");
	if(IS_ERR(gpDev->reg_optics_power))
		dev_err(dev,"can't get regulator optics_power");
	else
		retval = regulator_enable(gpDev->reg_optics_power);

	return retval;

EXIT_NO_LASERIRQ:
	if(gpDev->bHasLaser){
		FreeLaserIrq(gpDev);
	}
	return retval;
}

/**
 * Inverse setup done in SetupMX6Q...
 *
 * @param gpDev
 */
void InvSetupMX6Platform(PFAD_HW_INDEP_INFO gpDev)
{

	if (gpDev->bHasLaser) {
		FreeLaserIrq(gpDev);
	}
	if(gpDev->laser_on_gpio){
		gpio_free(gpDev->laser_on_gpio);
	}
}

void setLaserStatus(PFAD_HW_INDEP_INFO gpDev, BOOL on)
{
/*	SetI2CIoport(pInfo, LASER_SWITCH_ON, LaserStatus); */
/*	if(gpDev->laser_switch_gpio) */
/*		gpio_set_value_cansleep(gpDev->laser_switch_gpio, on); */
}

void getLaserStatus(PFAD_HW_INDEP_INFO gpDev, PFADDEVIOCTLLASER pLaserStatus)
{
	int value=0;
	if(gpDev->laser_on_gpio)
		value = (gpio_get_value_cansleep(gpDev->laser_on_gpio) == 0);

	pLaserStatus->bLaserIsOn = value;
	pr_debug("%s: bLaserIson is %i\n", __func__, value);
	if(gpDev->laser_switch_gpio){
		value = gpio_get_value_cansleep(gpDev->laser_switch_gpio == 0);
	}
	pr_debug("%s: laser_power_enabled is %i\n", __func__, value);
	pLaserStatus->bLaserPowerEnabled = value;
}


void stopmeasure(void)
{
	struct input_dev *button_dev = ca111_get_input_dev();
	pr_err("%s: input dev name is\n", __func__);
	input_event(button_dev, EV_MSC, MSC_RAW, 0);
	return 0;
}
void startmeasure(void)
{
	struct input_dev *button_dev = ca111_get_input_dev();
	pr_err("%s: input dev name is\n", __func__);
	input_event(button_dev, EV_MSC, MSC_RAW, 1);
	return 0;
}
