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
#include <linux/workqueue.h>
#include <linux/jiffies.h>

#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/of_regulator.h>
#endif
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/input.h>

extern struct input_dev* ca111_get_input_dev(void);
extern int ca111_get_laserstatus(void);

struct fad_ninjago {
	struct delayed_work laser_wq;
	PFAD_HW_INDEP_INFO gpDev;
	unsigned int time;
	int laser_on;
};

static struct fad_ninjago fadninjago;

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
void fad_laser_wq (unsigned long unused);
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
#ifdef CONFIG_OF
	struct device *dev = &gpDev->pLinuxDevice->dev;
	gpDev->node = of_find_compatible_node(NULL, NULL, "flir,fad");
#endif
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

	of_property_read_u32(gpDev->node, "standbyMinutes",
			     &gpDev->standbyMinutes);
	INIT_DELAYED_WORK(&fadninjago.laser_wq,
			  (void (*)(struct work_struct *)) fad_laser_wq);

	fadninjago.time = 0;
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
	f(on){
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
	pr_err("%s: CA111 Module not loaded, no Laser Distance Meter\n",
	       __func__);
#endif
}

void fad_laser_wq (unsigned long unused){
	if(! fadninjago.laser_on){
		if(time_after(jiffies, fadninjago.time + 3*HZ)){
			stoplaser();
		} else {
			schedule_delayed_work(&fadninjago.laser_wq, 5*HZ/10);
		}
	}
}


void SetLaserActive(PFAD_HW_INDEP_INFO gpDev, BOOL on)
{
	if (gpDev->bLaserEnable){
		fadninjago.laser_on=on;
		fadninjago.gpDev = gpDev;
		if(on){
			startlaser(gpDev);
			if (! cancel_delayed_work(&fadninjago.laser_wq))
				fadninjago.time = jiffies;
		} else{
			schedule_delayed_work(&fadninjago.laser_wq, HZ/10);
		}
	} else {
		pr_debug("%s: Turning laser off", __func__);
		stoplaser();
	}
}

BOOL GetLaserActive(PFAD_HW_INDEP_INFO gpDev)
{
    BOOL value = true;
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
			pr_err("%s: Unknown ldm accuracy mode (%i)...\n",
			       __func__, gpDev->ldmAccuracy);
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
	//setting GPS enabled /disabled is handled through linux
	//device PM system
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
	res = SetMotorSleepRegulator(gpDev,false);
	res |= regulator_disable(gpDev->reg_ring_sensor);
	res |= regulator_disable(gpDev->reg_position_sensor);
	res |= regulator_disable(gpDev->reg_optics_power);
#endif
	return res;
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
