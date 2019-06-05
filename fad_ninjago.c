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

extern struct input_dev* ca111_get_input_dev(void);
extern int ca111_get_laserstatus(void);

// Definitions
#define ENOLASERIRQ 1
#define ENODIGIOIRQ 2
// Local variables

// Function prototypes

static DWORD setKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED * pLED);
static DWORD getKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED * pLED);
static void getDigitalStatus(PFAD_HW_INDEP_INFO gpDev, PFADDEVIOCTLDIGIO pDigioStatus);

static void setLaserStatus(PFAD_HW_INDEP_INFO gpDev, BOOL on);
static void getLaserStatus(PFAD_HW_INDEP_INFO gpDev,
			   PFADDEVIOCTLLASER pLaserStatus);
static void SetLaserActive(PFAD_HW_INDEP_INFO gpDev, BOOL on);
static BOOL GetLaserActive(PFAD_HW_INDEP_INFO gpDev);
void setLaserMode(PFAD_HW_INDEP_INFO gpDev, PFADDEVIOCTLLASERMODE pLaserMode);

void startlaser(PFAD_HW_INDEP_INFO gpDev);
void stoplaser(void);
void startmeasure(int key, int value);
void stopmeasure(void);
void startmeasure_hq_continous(void);
void startmeasure_hq_single(void);
void startmeasure_lq_continous(void);
void startmeasure_lq_single(void);
static BOOL setGPSEnable(BOOL on);
static BOOL getGPSEnable(BOOL *on);

static int suspend(PFAD_HW_INDEP_INFO gpDev);
static int resume(PFAD_HW_INDEP_INFO gpDev);
int SetChargerSuspend(PFAD_HW_INDEP_INFO gpDev, BOOL suspend);
int SetMotorSleepRegulator(PFAD_HW_INDEP_INFO gpDev, BOOL suspend);

// Code
int SetupMX6Platform(PFAD_HW_INDEP_INFO gpDev)
{
	int retval = -1;
	extern struct list_head leds_list;
	extern struct rw_semaphore leds_list_lock;
	struct led_classdev *led_cdev;

#ifdef CONFIG_OF
	struct device *dev = &gpDev->pLinuxDevice->dev;
	gpDev->node = of_find_compatible_node(NULL, NULL, "flir,fad");
#endif

	gpDev->pGetKAKALedState = getKAKALedState;
	gpDev->pSetKAKALedState = setKAKALedState;
	gpDev->pGetDigitalStatus = getDigitalStatus;
	gpDev->pSetLaserStatus = setLaserStatus;
	gpDev->pGetLaserStatus = getLaserStatus;
	gpDev->pSetLaserActive = SetLaserActive;
	gpDev->pGetLaserActive = GetLaserActive;
	gpDev->pSetLaserMode = setLaserMode;
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
	of_property_read_u32_index(gpDev->node,
				   "hasLaser", 0, &gpDev->bHasLaser);
	of_property_read_u32_index(gpDev->node,
				   "hasGPS", 0, &gpDev->bHasGPS);
	of_property_read_u32_index(gpDev->node, "HasDigitalIO", 0, &gpDev->bHasDigitalIO);
	of_property_read_u32_index(gpDev->node, "hasTrigger", 0, &gpDev->bHasTrigger);

	gpDev->reg_optics_power = devm_regulator_get(dev, "optics_power");
	if(IS_ERR(gpDev->reg_optics_power))
		dev_err(dev,"can't get regulator optics_power");
	else
		retval = regulator_enable(gpDev->reg_optics_power);

	gpDev->reg_position_sensor = devm_regulator_get(dev, "position_sensor");
	if(IS_ERR(gpDev->reg_position_sensor))
		dev_err(dev,"can't get regulator position_sensor");
	else
		retval = regulator_enable(gpDev->reg_position_sensor);

	gpDev->reg_ring_sensor = devm_regulator_get(dev, "ring_sensor");
	if(IS_ERR(gpDev->reg_ring_sensor))
		dev_err(dev,"can't get regulator ring_sensor");
	else
		retval = regulator_enable(gpDev->reg_ring_sensor);

	gpDev->reg_motor_sleep = devm_regulator_get(dev, "motor_sleep");
	if(IS_ERR(gpDev->reg_motor_sleep))
		dev_err(dev,"can't get regulator motor_sleep");
	else
		retval = SetMotorSleepRegulator(gpDev, true);

	of_property_read_u32(gpDev->node, "standbyMinutes", &gpDev->standbyMinutes);

	gpDev->backlight = of_find_backlight_by_node(of_parse_phandle(gpDev->node, "backlight", 0));

	// Find LEDs
	down_read(&leds_list_lock);
	list_for_each_entry(led_cdev, &leds_list, node) {
		if (strcmp(led_cdev->name, "KAKA_LED2") == 0)
			gpDev->red_led_cdev = led_cdev;
		else if (strcmp(led_cdev->name, "KAKA_LED1") == 0)
			gpDev->blue_led_cdev = led_cdev;
	}
	up_read(&leds_list_lock);

	if (gpDev->bHasDigitalIO) {
		int pin;
		pin = of_get_named_gpio_flags(gpDev->node, "digin0-gpios", 0, NULL);
		if (gpio_is_valid(pin) == 0){
			pr_err("flirdrv-fad: DigIN0 can not be used\n");
		} else {
			gpDev->digin0_gpio = pin;
			gpio_request(pin, "DigIN0");
			gpio_direction_input(pin);
		}
		pin = of_get_named_gpio_flags(gpDev->node, "digin1-gpios", 0, NULL);
		if (gpio_is_valid(pin) == 0){
			pr_err("flirdrv-fad: DigIN1 can not be used\n");
		} else {
			gpDev->digin1_gpio = pin;
			gpio_request(pin, "DigIN1");
			gpio_direction_input(pin);
		}
	}

	if (gpDev->bHasTrigger) {
		int pin;
		pin = of_get_named_gpio_flags(gpDev->node, "trigger-gpio", 0, NULL);
		if (gpio_is_valid(pin) == 0){
			pr_err("flirdrv-fad: Trigger can not be used\n");
		} else {
			gpDev->trigger_gpio = pin;
			gpio_request(pin, "Trigger");
			gpio_direction_input(pin);
			if (request_irq(gpio_to_irq(pin), fadTriggerIST,
					IRQF_TRIGGER_FALLING, "TriggerGPIO", gpDev))
				pr_err("flridrv-fad: Failed to register interrupt for trigger...\n");
			else
				pr_info("flirdrv-fad: Registered interrupt %i for trigger\n", gpio_to_irq(pin));
		}
	}

	return retval;

	//EXIT_NO_LASERIRQ:
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

}

DWORD getKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED * pLED)
{
	BOOL redLed = FALSE;
	BOOL blueLed = FALSE;

	if (gpDev->red_led_cdev && gpDev->red_led_cdev->brightness)
		redLed = TRUE;
	if (gpDev->blue_led_cdev && gpDev->blue_led_cdev->brightness)
		blueLed = TRUE;

	if ((blueLed == FALSE) && (redLed == FALSE)) {
		pLED->eState = LED_STATE_OFF;
		pLED->eColor = LED_COLOR_GREEN;
	} else {
		pLED->eState = LED_STATE_ON;
		if (blueLed && redLed)
			pLED->eColor = LED_COLOR_YELLOW;
		else if (redLed)
			pLED->eColor = LED_COLOR_RED;
		else
			pLED->eColor = LED_COLOR_GREEN;
	}

	return ERROR_SUCCESS;
}

DWORD setKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED * pLED)
{
	int redLed = 0;
	int blueLed = 0;

	if (pLED->eState == LED_STATE_ON) {
		if (pLED->eColor == LED_COLOR_YELLOW) {
			redLed = 1;
			blueLed = 1;
		} else if (pLED->eColor == LED_COLOR_GREEN) {
			blueLed = 1;
		} else if (pLED->eColor == LED_COLOR_RED) {
			redLed = 1;
		}
	}
	if (gpDev->red_led_cdev) {
		gpDev->red_led_cdev->brightness = redLed;
		gpDev->red_led_cdev->brightness_set(gpDev->red_led_cdev,
						    redLed);
	}

	if (gpDev->blue_led_cdev) {
		gpDev->blue_led_cdev->brightness = blueLed;
		gpDev->blue_led_cdev->brightness_set(gpDev->blue_led_cdev,
						     blueLed);
	}

	return ERROR_SUCCESS;
}

void getDigitalStatus(PFAD_HW_INDEP_INFO gpDev, PFADDEVIOCTLDIGIO pDigioStatus)
{
	int digin0_value=0;
	int digin1_value=0;
#ifdef CONFIG_OF
	if(gpDev->digin0_gpio)
		digin0_value = gpio_get_value_cansleep(gpDev->digin0_gpio);
	if(gpDev->digin1_gpio)
		digin1_value = gpio_get_value_cansleep(gpDev->digin1_gpio);
#endif
	pDigioStatus->usInputState |= digin0_value ? 0x01 : 0x00;
	pDigioStatus->usInputState |= digin1_value ? 0x02 : 0x00;
}

/** 
 * setLaserStatus tells if *laser* is allowed to be turned on, but will not 
 * turn on the laser
 *
 * Laser is allowed if no lens is covering the laseroptics, and the correct
 * attribute in appcore is set.
 * 
 * 
 * @param gpDev 
 * @param on if set, laser is allowed, if false, turn off laser!!
 */
void setLaserStatus(PFAD_HW_INDEP_INFO gpDev, BOOL on)
{
    if(on){
        gpDev->bLaserEnable=true;
    } else {
        gpDev->bLaserEnable=false;
        stoplaser();
    }
}

void getLaserStatus(PFAD_HW_INDEP_INFO gpDev, PFADDEVIOCTLLASER pLaserStatus)
{
#if defined (CONFIG_CA111)
	int state;
	msleep(100);
	state = ca111_get_laserstatus();
	pLaserStatus->bLaserIsOn = state;  //if laser is on
	pLaserStatus->bLaserPowerEnabled = true; // if switch is pressed...
#else
	pr_err("%s: CA111 Module not loaded, no Laser Distance Meter\n", __func__);
#endif
}

void SetLaserActive(PFAD_HW_INDEP_INFO gpDev, BOOL on)
{
        if (gpDev->bLaserEnable){
                if(on){
                        pr_debug("%s: Turning laser on", __func__);
                        startlaser(gpDev);
                } else{
                        pr_debug("%s: Turning laser off", __func__);
                        stoplaser();
                }
        } else {
                pr_debug("%s: Turning laser off", __func__);
                stoplaser();
        }
}

BOOL GetLaserActive(PFAD_HW_INDEP_INFO gpDev)
{
    BOOL value = true;
    pr_err("%s return value true\n", __func__);
	return value;
}

void startlaser(PFAD_HW_INDEP_INFO gpDev)
{
#ifdef CONFIG_OF
        switch (gpDev->laserMode){
        case LASERMODE_POINTER: 
                startmeasure(MSC_RAW, 1);
                break;
        case LASERMODE_DISTANCE:
                switch (gpDev->ldmAccuracy){
                case LASERMODE_DISTANCE_LOW_ACCURACY:
                        if(gpDev->ldmContinous){
                                startmeasure_lq_continous();
                        } else{
                                startmeasure_lq_single();
                        }
                        break;
                case LASERMODE_DISTANCE_HIGH_ACCURACY:
                        if(gpDev->ldmContinous){
                                startmeasure_hq_continous();
                        } else{
                                startmeasure_hq_single();
                        }
                        break;
                        
                default:
                        pr_err("%s: Unknown ldm accuracy mode (%i)...\n", __func__, gpDev->ldmAccuracy);
                        break;
                }
                break;
        default: 
                pr_err("%s: Unknown lasermode...\n", __func__);
                break;
        }
#endif
}


void stoplaser(void)
{
    stopmeasure();
}

void stopmeasure(void)
{
        startmeasure(MSC_RAW, 0);
}

void startmeasure(int key, int value)
{
#if defined (CONFIG_CA111)
	struct input_dev *button_dev = ca111_get_input_dev();
	if(button_dev){
		input_event(button_dev, EV_MSC, key, value);
	} else {
		pr_err("fad %s: ca111 input_dev is NULL\n", __func__);
	}
#else
    pr_err("%s: CA111 Module not loaded, no Laser Distance Meter\n", __func__);
#endif
}

void startmeasure_hq_single(void)
{
	startmeasure(MSC_PULSELED, 1);
}

void startmeasure_hq_continous(void)
{
	startmeasure(MSC_PULSELED, 2);
}

void startmeasure_lq_single(void)
{
	startmeasure(MSC_GESTURE, 1);
}

void startmeasure_lq_continous(void)
{
	startmeasure(MSC_GESTURE, 2);
}


/** 
 * setLaserMode
 *
 * 
 * @param gpDev 
 * @param on if set, laser is allowed, if false, turn off laser!!
 */
void setLaserMode(PFAD_HW_INDEP_INFO gpDev, PFADDEVIOCTLLASERMODE pLaserMode)
{
#ifdef CONFIG_OF
        gpDev->laserMode = pLaserMode->mode;
        gpDev->ldmAccuracy = pLaserMode->accuracy;
        gpDev->ldmContinous = pLaserMode->continousMeasurment;
#endif
}


BOOL setGPSEnable(BOOL on)
{
	//setting GPS enabled /disabled is handled through linux device PM system
	// opening/closing the tty device is enough for userspace...
	return TRUE;
}

BOOL getGPSEnable(BOOL * on)
{
	*on = TRUE;
	return TRUE;
}

int suspend(PFAD_HW_INDEP_INFO gpDev)
{
	int res = 0;

#ifdef CONFIG_OF
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
#endif
	return 0;
}


int resume(PFAD_HW_INDEP_INFO gpDev)
{
	int res = 0;

#ifdef CONFIG_OF
	res = regulator_enable(gpDev->reg_optics_power);
	res |= regulator_enable(gpDev->reg_position_sensor);
	res |= regulator_enable(gpDev->reg_ring_sensor);
	res |= SetMotorSleepRegulator(gpDev, true);
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
int SetChargerSuspend(PFAD_HW_INDEP_INFO gpDev, BOOL suspend)
{
	int res;
#ifdef CONFIG_OF
	res = SetMotorSleepRegulator(gpDev, suspend);
#endif
	return res;
}

int SetMotorSleepRegulator(PFAD_HW_INDEP_INFO gpDev, BOOL on)
{
	int res;
	static int enabled = false;
#ifdef CONFIG_OF
	if(on){
		if (! enabled){
			res = regulator_enable(gpDev->reg_motor_sleep);
			enabled = true;
		} else {
			//If already enabled, silently exit...
			res = 0;
		}
	} else {
		if(enabled){
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
