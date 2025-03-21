// SPDX-License-Identifier: GPL-2.0-or-later
/***********************************************************************
 * Project: Balthazar
 *
 * Description of file:
 *    FLIR Application Driver (FAD) IO functions.
 *
 *
 *  FADDEV Copyright : FLIR Systems AB
 ***********************************************************************/

#include "flir_kernel_os.h"
#include "faddev.h"
#include "fad_internal.h"
#include <linux/errno.h>
#include <linux/leds.h>
#include "flir-kernel-version.h"
#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/of_regulator.h>
#include <linux/backlight.h>
#endif
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/input.h>



// Definitions
#define ENOLASERIRQ 1
#define ENODIGIOIRQ 2
// Local variables

// Function prototypes

static DWORD setKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED *pLED);
static DWORD getKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED *pLED);
static DWORD setLedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED *pLED);
static DWORD getLedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED *pLED);
static void getDigitalStatus(PFAD_HW_INDEP_INFO gpDev,
			     PFADDEVIOCTLDIGIO pDigioStatus);


static BOOL setGPSEnable(BOOL on);
static BOOL getGPSEnable(BOOL *on);

static int suspend(PFAD_HW_INDEP_INFO gpDev);
static int resume(PFAD_HW_INDEP_INFO gpDev);
static int SetChargerSuspend(PFAD_HW_INDEP_INFO gpDev, BOOL suspend);
static int SetMotorSleepRegulator(PFAD_HW_INDEP_INFO gpDev, BOOL suspend);

// Code
int SetupMX6Platform(PFAD_HW_INDEP_INFO gpDev)
{
	int retval = 0;

#ifdef CONFIG_OF
	extern struct list_head leds_list;
	extern struct rw_semaphore leds_list_lock;
	struct led_classdev *led_cdev;
	struct faddata *data = container_of(gpDev, struct faddata, pDev);
	struct device *dev = data->dev;
#endif

	gpDev->pGetLedState = getLedState;
	gpDev->pSetLedState = setLedState;
	gpDev->pGetKAKALedState = getKAKALedState;
	gpDev->pSetKAKALedState = setKAKALedState;
	gpDev->pGetDigitalStatus = getDigitalStatus;
	gpDev->pSetGPSEnable = setGPSEnable;
	gpDev->pGetGPSEnable = getGPSEnable;
	gpDev->pSetChargerSuspend = SetChargerSuspend;
	gpDev->suspend = suspend;
	gpDev->resume = resume;

#ifdef CONFIG_OF
	/* Configure devices (bools) from DT */
	//Do not care about return value of function
	//If property is missing, assume device doesnt exist!
	//Better to wrap this in separate function... (int -> bool etc...)
	of_property_read_u32_index(dev->of_node, "hasLaser", 0, &gpDev->bHasLaser);
	of_property_read_u32_index(dev->of_node, "hasGPS", 0, &gpDev->bHasGPS);
	of_property_read_u32_index(dev->of_node, "HasDigitalIO", 0, &gpDev->bHasDigitalIO);
	of_property_read_u32_index(dev->of_node, "hasTrigger", 0, &gpDev->bHasTrigger);
	of_property_read_u32_index(dev->of_node, "HasKAKALed", 0,&gpDev->bHasKAKALed);
	of_property_read_u32_index(dev->of_node, "hasFocusModule", 0, &gpDev->bHasFocusModule);

	// Determine what laser device to use
	if (gpDev->bHasLaser) {
		if (of_machine_is_compatible("fsl,imx6qp-eoco")){
			retval = SetupLaserPointer(gpDev);
		} else {
			retval = SetupLaserDistance(gpDev);
		}
	}

	// Find regulators related to focusing.
	if (gpDev->bHasFocusModule) {

		gpDev->reg_optics_power = devm_regulator_get(dev, "optics_power");
		if (IS_ERR(gpDev->reg_optics_power))
			dev_err(dev, "can't get regulator optics_power");
		else
			retval |= regulator_enable(gpDev->reg_optics_power);

		gpDev->reg_position_sensor = devm_regulator_get(dev, "position_sensor");
		if (IS_ERR(gpDev->reg_position_sensor))
			dev_err(dev, "can't get regulator position_sensor");
		else
			retval |= regulator_enable(gpDev->reg_position_sensor);

		gpDev->reg_ring_sensor = devm_regulator_get(dev, "ring_sensor");
		if (IS_ERR(gpDev->reg_ring_sensor))
			dev_err(dev, "can't get regulator ring_sensor");
		else
			retval |= regulator_enable(gpDev->reg_ring_sensor);

		gpDev->reg_motor_sleep = devm_regulator_get(dev, "motor_sleep");
		if (IS_ERR(gpDev->reg_motor_sleep))
			dev_err(dev, "can't get regulator motor_sleep");
		else
			retval |= SetMotorSleepRegulator(gpDev, true);
	}

	gpDev->backlight = of_find_backlight_by_node(of_parse_phandle(dev->of_node, "backlight", 0));

	// Find LEDs
	if (gpDev->bHasKAKALed) {
		down_read(&leds_list_lock);
		list_for_each_entry(led_cdev, &leds_list, node) {

			if (!led_cdev->dev) {
				dev_err(dev, "finding KAKA leds - dev is NULL");
				continue;
			}

			if (!led_cdev->dev->kobj.name) {
				dev_err(dev, "finding KAKA leds - listed led name is NULL");
				continue;
			}

			if (strcmp(led_cdev->dev->kobj.name, "KAKA_LED2") == 0)
				gpDev->red_led_cdev = led_cdev;
			else if (strcmp(led_cdev->dev->kobj.name, "KAKA_LED1") == 0)
				gpDev->blue_led_cdev = led_cdev;
		}
		up_read(&leds_list_lock);
	}

	if (gpDev->bHasDigitalIO) {
		int pin;

		pin = of_get_named_gpio_flags(dev->of_node, "digin0-gpios", 0, NULL);
		if (gpio_is_valid(pin) == 0) {
			pr_err("flirdrv-fad: DigIN0 can not be used\n");
		} else {
			gpDev->digin0_gpio = pin;
			gpio_request(pin, "DigIN0");
			gpio_direction_input(pin);
		}
		pin = of_get_named_gpio_flags(dev->of_node, "digin1-gpios", 0, NULL);
		if (gpio_is_valid(pin) == 0) {
			pr_err("flirdrv-fad: DigIN1 can not be used\n");
		} else {
			gpDev->digin1_gpio = pin;
			gpio_request(pin, "DigIN1");
			gpio_direction_input(pin);
		}
	}

	if (gpDev->bHasTrigger) {
		int pin;
		pin = of_get_named_gpio_flags(dev->of_node, "trigger-gpio", 0, NULL);
		if (gpio_is_valid(pin) == 0) {
			pr_err("flirdrv-fad: Trigger can not be used\n");
		} else {
			gpDev->trigger_gpio = pin;
			gpio_request(pin, "Trigger");
			gpio_direction_input(pin);
			if (request_irq(gpio_to_irq(pin), fadTriggerIST, IRQF_TRIGGER_FALLING, "TriggerGPIO", gpDev))
				pr_err("flridrv-fad: Failed to register interrupt for trigger...\n");
			else
				pr_debug("flirdrv-fad: Registered interrupt %i for trigger\n", gpio_to_irq(pin));
		}
	}

#endif
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
		if (of_machine_is_compatible("fsl,imx6qp-eoco")){
			InvSetupLaserPointer(gpDev);
		}
	}

	if (gpDev->trigger_gpio)
		free_irq(gpio_to_irq(gpDev->trigger_gpio), gpDev);

	if (gpDev->bHasFocusModule) {
		SetMotorSleepRegulator(gpDev, false);
		regulator_disable(gpDev->reg_ring_sensor);
		regulator_disable(gpDev->reg_position_sensor);
		regulator_disable(gpDev->reg_optics_power);
	}

	if (gpDev->digin0_gpio)
		gpio_free(gpDev->digin0_gpio);
	if (gpDev->digin1_gpio)
		gpio_free(gpDev->digin1_gpio);
}

DWORD getLedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED *pLED)
{
#ifdef CONFIG_OF
	BOOL redLed = FALSE;
	BOOL blueLed = FALSE;

	if (!gpDev->red_led_cdev || !gpDev->blue_led_cdev) {
		pLED->eState = LED_STATE_OFF;
		return ERROR_SUCCESS;
	}

	if (gpDev->red_led_cdev->brightness)
		redLed = TRUE;
	if (gpDev->blue_led_cdev->brightness)
		blueLed = TRUE;

	if (gpDev->red_led_cdev->blink_delay_on == 500 ||
	    gpDev->blue_led_cdev->blink_delay_on == 500)
		pLED->eState = LED_FLASH_SLOW;
	else if (gpDev->red_led_cdev->blink_delay_on == 100 ||
		 gpDev->blue_led_cdev->blink_delay_on == 100)
		pLED->eState = LED_FLASH_FAST;
	else if ((blueLed == FALSE) && (redLed == FALSE)) {
		pLED->eState = LED_STATE_OFF;
	} else {
		pLED->eState = LED_STATE_ON;
	}
#endif
	return ERROR_SUCCESS;
}

DWORD setLedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED *pLED)
{
#ifdef CONFIG_OF
	BOOL redLed = FALSE;
	BOOL greenLed = FALSE;
	unsigned long delay = 0;

	// On Bellatrix the KAKA LED consists of a green and a red LED
	// The blue_led_cdev actually controls a green LED
	if (gpDev->red_led_cdev
	    && (gpDev->red_led_cdev->brightness
		|| gpDev->red_led_cdev->blink_delay_on))
		redLed = TRUE;
	if (gpDev->blue_led_cdev
	    && (gpDev->blue_led_cdev->brightness
		|| gpDev->blue_led_cdev->blink_delay_on))
		greenLed = TRUE;

	if (pLED->eState == LED_FLASH_SLOW || pLED->eState == LED_FLASH_FAST) {
		if (pLED->eState == LED_FLASH_FAST)
			delay = 100;
		else
			delay = 500;

		if (redLed) {
			led_blink_set(gpDev->red_led_cdev, &delay, &delay);
		}

		if (greenLed) {
			led_blink_set(gpDev->blue_led_cdev, &delay, &delay);
		}
	} else if (pLED->eState == LED_STATE_ON) {
		if (gpDev->blue_led_cdev) {
			led_set_brightness(gpDev->blue_led_cdev, LED_FULL);
		}
		if (gpDev->red_led_cdev) {
			led_set_brightness(gpDev->red_led_cdev, LED_OFF);
		}
	} else if (pLED->eState == LED_STATE_OFF) {
		if (gpDev->blue_led_cdev) {
			led_set_brightness(gpDev->blue_led_cdev, LED_OFF);
		}
		if (gpDev->red_led_cdev) {
			led_set_brightness(gpDev->red_led_cdev, LED_OFF);
		}
	}
#endif
	return ERROR_SUCCESS;
}

DWORD getKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED *pLED)
{
	BOOL redLed = FALSE;
	BOOL greenLed = FALSE;

	// On Bellatrix the KAKA LED consists of a green and a red LED
	// The blue_led_cdev actually controls a green LED
	if (gpDev->red_led_cdev
	    && (gpDev->red_led_cdev->brightness
		|| gpDev->red_led_cdev->blink_delay_on))
		redLed = TRUE;
	if (gpDev->blue_led_cdev
	    && (gpDev->blue_led_cdev->brightness
		|| gpDev->blue_led_cdev->blink_delay_on))
		greenLed = TRUE;

	if ((greenLed == FALSE) && (redLed == FALSE)) {
		pLED->eState = LED_STATE_OFF;
		pLED->eColor = LED_COLOR_GREEN;
	} else {
		pLED->eState = LED_STATE_ON;
		if (greenLed && redLed)
			pLED->eColor = LED_COLOR_YELLOW;
		else if (redLed)
			pLED->eColor = LED_COLOR_RED;
		else
			pLED->eColor = LED_COLOR_GREEN;
	}

	return ERROR_SUCCESS;
}

DWORD setKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED *pLED)
{
#ifdef CONFIG_OF
	int redLed = 0;
	int greenLed = 0;

	// On Bellatrix the KAKA LED consists of a green and a red LED
	// The blue_led_cdev actually controls a green LED
	if (pLED->eState == LED_STATE_ON) {
		if (pLED->eColor == LED_COLOR_YELLOW) {
			redLed = 1;
			greenLed = 1;
		} else if (pLED->eColor == LED_COLOR_GREEN) {
			greenLed = 1;
		} else if (pLED->eColor == LED_COLOR_RED) {
			redLed = 1;
		}
	}

	if (gpDev->red_led_cdev) {
		led_set_brightness(gpDev->red_led_cdev,
				   redLed ? LED_FULL : LED_OFF);
	}

	if (gpDev->blue_led_cdev) {
		led_set_brightness(gpDev->blue_led_cdev,
				   greenLed ? LED_FULL : LED_OFF);
	}
#endif
	return ERROR_SUCCESS;
}

void getDigitalStatus(PFAD_HW_INDEP_INFO gpDev, PFADDEVIOCTLDIGIO pDigioStatus)
{
	int digin0_value = 0;
	int digin1_value = 0;
#ifdef CONFIG_OF
	if (gpDev->digin0_gpio)
		digin0_value = gpio_get_value_cansleep(gpDev->digin0_gpio);
	if (gpDev->digin1_gpio)
		digin1_value = gpio_get_value_cansleep(gpDev->digin1_gpio);
#endif
	pDigioStatus->usInputState |= digin0_value ? 0x01 : 0x00;
	pDigioStatus->usInputState |= digin1_value ? 0x02 : 0x00;
}


BOOL setGPSEnable(BOOL on)
{
	//setting GPS enabled /disabled is handled through linux device PM system
	// opening/closing the tty device is enough for userspace...
	return TRUE;
}

BOOL getGPSEnable(BOOL *on)
{
	*on = TRUE;
	return TRUE;
}

int suspend(PFAD_HW_INDEP_INFO gpDev)
{
#ifdef CONFIG_OF
	if (gpDev->bHasFocusModule) {
		int res = 0;

		pr_debug("Disbling motor regulator...\n");
		res = SetMotorSleepRegulator(gpDev, false);
		if (res)
			pr_err("Motor regulator disable failed..\n");
		pr_debug("Disbling ring sensor...\n");
		if (gpDev->reg_ring_sensor) {
			res = regulator_disable(gpDev->reg_ring_sensor);
			if (res)
				pr_err("Ring sensor disable failed..\n");
		}
		pr_debug("Disbling position sensor...\n");
		if (gpDev->reg_position_sensor) {
			res |= regulator_disable(gpDev->reg_position_sensor);
			if (res)
				pr_err("Position sensor disable failed..\n");
		}
		pr_debug("Disbling optics power...\n");
		if (gpDev->reg_optics_power) {
			res |= regulator_disable(gpDev->reg_optics_power);
			if (res)
				pr_err("Optics power disable failed..\n");
		}
	}
#endif
	return 0;
}

int resume(PFAD_HW_INDEP_INFO gpDev)
{
	int res = 0;

#ifdef CONFIG_OF
	if (gpDev->bHasFocusModule) {
		res = regulator_enable(gpDev->reg_optics_power);
		res |= regulator_enable(gpDev->reg_position_sensor);
		res |= regulator_enable(gpDev->reg_ring_sensor);
		res |= SetMotorSleepRegulator(gpDev, true);
	}
#endif
	return res;
}

/**
 * Mode for disabling misc regulators during suspend for charging
 *
 * @param suspend
 *
 * @return
 */
static int SetChargerSuspend(PFAD_HW_INDEP_INFO gpDev, BOOL suspend)
{
	int res = 0;
#ifdef CONFIG_OF
	if (gpDev->bHasFocusModule) {
		res = SetMotorSleepRegulator(gpDev, suspend);
	}
#endif
	return res;
}

static int SetMotorSleepRegulator(PFAD_HW_INDEP_INFO gpDev, BOOL on)
{
	int res = 0;
#ifdef CONFIG_OF
	static bool enabled = false;

	if (on) {
		if (!enabled) {
			res = regulator_enable(gpDev->reg_motor_sleep);
			enabled = true;
		} else {
			//If already enabled, silently exit...
			res = 0;
		}
	} else {
		if (enabled) {
			res = regulator_disable(gpDev->reg_motor_sleep);
			enabled = false;
		} else {
			//If already disabled, silently exit...
			res = 0;
		}
	}
#endif
	return res;
}
