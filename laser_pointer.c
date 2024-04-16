// SPDX-License-Identifier: GPL-2.0-or-later
/***********************************************************************
 *
 * Project: Eowyn
 *
 * Description of file:
 *    FLIR Application Driver (FAD) IO functions.
 *
 * Last check-in changelist:
 *
 *  FADDEV Copyright : FLIR Systems AB
 ***********************************************************************/

#include "flir_kernel_os.h"
#include "faddev.h"
#include "i2cdev.h"
#include "fad_internal.h"
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/leds.h>
#include "flir-kernel-version.h"
#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/of_regulator.h>
#endif
#include <linux/leds.h>
#include <linux/platform_device.h>

#define ENOLASERIRQ 1

void setLaserPointerStatus(PFAD_HW_INDEP_INFO gpDev, BOOL on)
{
#ifdef CONFIG_OF
	if (gpDev->laser_switch_gpio)
		gpio_set_value_cansleep(gpDev->laser_switch_gpio, on);
#endif
}

void updateLaserPointerOutput(PFAD_HW_INDEP_INFO gpDev)
{
	if (gpDev->bHasSoftwareControlledLaser) {
		FADDEVIOCTLLASER laserStatus = { 0 };
		getLaserPointerStatus(gpDev, &laserStatus);

		if (laserStatus.bLaserIsOn && laserStatus.bLaserPowerEnabled) {
			setLaserPointerStatus(gpDev, 1);
		} else {
			setLaserPointerStatus(gpDev, 0);
		}
	}
}

void getLaserPointerStatus(PFAD_HW_INDEP_INFO gpDev, PFADDEVIOCTLLASER pLaserStatus)
{
	int value = 0;
#ifdef CONFIG_OF
	if (gpDev->laser_on_gpio)
		value = (gpio_get_value_cansleep(gpDev->laser_on_gpio) == 0);
#endif
	pLaserStatus->bLaserIsOn = value;

#ifdef CONFIG_OF
	if (gpDev->laser_switch_gpio)
		value = gpio_get_value_cansleep(gpDev->laser_switch_gpio == 0);
#endif
	pLaserStatus->bLaserPowerEnabled = value;
}


void SetLaserPointerActive(PFAD_HW_INDEP_INFO gpDev, BOOL on)
{
#ifdef CONFIG_OF
	if (gpDev->laser_soft_gpio) {
		gpio_set_value_cansleep(gpDev->laser_soft_gpio, on);
	}
#endif
}

BOOL GetLaserPointerActive(PFAD_HW_INDEP_INFO gpDev)
{
	int value = 0;
#ifdef CONFIG_OF
	if (gpDev->laser_soft_gpio) {
		value = gpio_get_value_cansleep(gpDev->laser_soft_gpio);
	}
#endif
	return value;
}

int SetupLaserPointer(PFAD_HW_INDEP_INFO gpDev)
{
	int retval = 0;

	gpDev->pSetLaserStatus = setLaserPointerStatus;
	gpDev->pGetLaserStatus = getLaserPointerStatus;
	gpDev->pUpdateLaserOutput = updateLaserPointerOutput;
	gpDev->pSetLaserActive = SetLaserPointerActive;
	gpDev->pGetLaserActive = GetLaserPointerActive;
	struct faddata *data = container_of(gpDev, struct faddata, pDev);
	struct device *dev = data->dev;

#ifdef CONFIG_OF

	/* Configure devices (bools) from DT */
	of_property_read_u32_index(dev->of_node, "hasLaser", 0,
				   &gpDev->bHasLaser);
	of_property_read_u32_index(dev->of_node, "HasSoftwareControlledLaser", 0,
				   &gpDev->bHasSoftwareControlledLaser);

	if (gpDev->bHasLaser) {
		int pin;
		pin =
		    of_get_named_gpio_flags(dev->of_node, "laser_on-gpios", 0,
					    NULL);
		if (gpio_is_valid(pin) == 0) {
			pr_err("flirdrv-fad: LaserON can not be used\n");
		} else {
			gpDev->laser_on_gpio = pin;
			gpio_request(pin, "LaserON");
			gpio_direction_input(pin);
			retval = InitLaserIrq(gpDev);
			if (retval) {
				pr_err
				    ("flirdrv-fad: Failed to request Laser IRQ\n");
				retval = -ENOLASERIRQ;
				goto EXIT_NO_LASERIRQ;
			}
		}

		pin =
		    of_get_named_gpio_flags(dev->of_node, "laser_soft-gpios", 0,
					    NULL);
		if (gpio_is_valid(pin) == 0) {
			pr_err("flirdrv-fad: Laser Soft On can not be used\n");
		} else {
			gpDev->laser_soft_gpio = pin;
			retval = gpio_request(pin, "LaserSoftOn");
			if (retval) {
				pr_err("Fail registering lasersofton\n");
			}
			retval = gpio_direction_output(pin, 0);
			if (retval) {
				pr_err("Fail setting direction lasersofton\n");
			}
		}

		pin =
		    of_get_named_gpio_flags(dev->of_node, "laser_switch-gpios",
					    0, NULL);
		if (gpio_is_valid(pin) == 0) {
			pr_err("flirdrv-fad: Laser Switch can not be used\n");
		} else {
			gpDev->laser_switch_gpio = pin;
			retval = gpio_request(pin, "LaserSwitchOn");
			if (retval) {
				pr_err("Fail registering laserswitchon\n");
			}
			retval = gpio_direction_output(pin, 0);
			if (retval) {
				pr_err("Fail setting direction laserswitcon\n");
			}
		}
	}
	
	goto EXIT;

EXIT_NO_LASERIRQ:
	if (gpDev->bHasLaser) {
		FreeLaserIrq(gpDev);
	}

EXIT:
#endif
	return retval;
}


void InvSetupLaserPointer(PFAD_HW_INDEP_INFO gpDev)
{
	if (gpDev->bHasLaser) {
		FreeLaserIrq(gpDev);
	}
#ifdef CONFIG_OF
	if (gpDev->laser_on_gpio) {
		gpio_free(gpDev->laser_on_gpio);
	}
	if (gpDev->laser_soft_gpio) {
		gpio_free(gpDev->laser_soft_gpio);
	}
	if (gpDev->laser_soft_gpio) {
		gpio_free(gpDev->laser_switch_gpio);
	}
#endif
}
