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

// Code
int SetupMX6Platform(PFAD_HW_INDEP_INFO gpDev)
{
	int retval = -1;
	struct device *dev = &gpDev->pLinuxDevice->dev;
	gpDev->node = of_find_compatible_node(NULL, NULL, "flir,fad");
	gpDev->pSetLaserStatus = setLaserStatus;
	gpDev->pGetLaserStatus = getLaserStatus;
	gpDev->pSetLaserActive = SetLaserActive;
	gpDev->pGetLaserActive = GetLaserActive;
	gpDev->pSetLaserMode = setLaserMode;

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
    pr_err("%s: Returning falses...\n", __func__);
	pLaserStatus->bLaserIsOn = false;  //if laser is on
	pLaserStatus->bLaserPowerEnabled = false; // if switch is pressed...
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
        switch (gpDev->laserMode){
        case LASERMODE_POINTER: 
                startmeasure(MSC_RAW, 1);
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
                        pr_err("%s: Unknown ldm accuracy mode...\n", __func__);
                        break;
                }
        default: 
                pr_err("%s: Unknown lasermode...\n", __func__);
                break;
        }
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
	struct input_dev *button_dev = ca111_get_input_dev();
	input_event(button_dev, EV_MSC, key, value);
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
        gpDev->laserMode = pLaserMode->mode;
        gpDev->ldmAccuracy = pLaserMode->accuracy;
        gpDev->ldmContinous = pLaserMode->continousMeasurment;
}


