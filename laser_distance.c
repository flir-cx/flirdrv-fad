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

#define ENOLASERIRQ 1

extern struct input_dev *ca111_get_input_dev(void);
extern int ca111_get_laserstatus(void);

void startlaser(PFAD_HW_INDEP_INFO gpDev);
void stoplaser(void);
void startmeasure(int key, int value);
void stopmeasure(void);
void startmeasure_hq_continous(void);
void startmeasure_hq_single(void);
void startmeasure_lq_continous(void);
void startmeasure_lq_single(void);

void setLaserDistanceStatus(PFAD_HW_INDEP_INFO gpDev, BOOL on)
{
    if (on) {
		gpDev->bLaserEnable = true;
	} else {
		gpDev->bLaserEnable = false;
		stoplaser();
	}
}

void getLaserDistanceStatus(PFAD_HW_INDEP_INFO gpDev, PFADDEVIOCTLLASER pLaserStatus)
{
#if defined(CONFIG_CA111)
	int state;

	msleep(100);
	state = ca111_get_laserstatus();
	pLaserStatus->bLaserIsOn = state;	//if laser is on
	pLaserStatus->bLaserPowerEnabled = true;	// if switch is pressed...
#else
	pr_err("%s: CA111 Module not loaded, no Laser Distance Meter\n",
	       __func__);
#endif
}

void SetLaserDistanceActive(PFAD_HW_INDEP_INFO gpDev, BOOL on)
{
    if (gpDev->bLaserEnable) {
		if (on) {
			pr_debug("%s: Turning laser on", __func__);
			startlaser(gpDev);
		} else {
			pr_debug("%s: Turning laser off", __func__);
			stoplaser();
		}
	} else {
		pr_debug("%s: Turning laser off", __func__);
		stoplaser();
	}
}

BOOL GetLaserDistanceActive(PFAD_HW_INDEP_INFO gpDev)
{
    BOOL value = true;

	pr_err("%s return value true\n", __func__);
	return value;
}

void startlaser(PFAD_HW_INDEP_INFO gpDev)
{
#ifdef CONFIG_OF
	switch (gpDev->laserMode) {
	case LASERMODE_POINTER:
		startmeasure(MSC_RAW, 1);
		break;
	case LASERMODE_DISTANCE:
		switch (gpDev->ldmAccuracy) {
		case LASERMODE_DISTANCE_LOW_ACCURACY:
			if (gpDev->ldmContinous) {
				startmeasure_lq_continous();
			} else {
				startmeasure_lq_single();
			}
			break;
		case LASERMODE_DISTANCE_HIGH_ACCURACY:
			if (gpDev->ldmContinous) {
				startmeasure_hq_continous();
			} else {
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
#if defined(CONFIG_CA111)
	struct input_dev *button_dev = ca111_get_input_dev();

	if (button_dev) {
		input_event(button_dev, EV_MSC, key, value);
	} else {
		pr_err("fad %s: ca111 input_dev is NULL\n", __func__);
	}
#else
	pr_err("%s: CA111 Module not loaded, no Laser Distance Meter\n",
	       __func__);
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

void setLaserDistanceMode(PFAD_HW_INDEP_INFO gpDev, PFADDEVIOCTLLASERMODE pLaserMode)
{
#ifdef CONFIG_OF
	gpDev->laserMode = pLaserMode->mode;
	gpDev->ldmAccuracy = pLaserMode->accuracy;
	gpDev->ldmContinous = pLaserMode->continousMeasurment;
#endif
}

int SetupLaserDistance(PFAD_HW_INDEP_INFO gpDev)
{
    int retval = 0;

    gpDev->pSetLaserStatus = setLaserDistanceStatus;
	gpDev->pGetLaserStatus = getLaserDistanceStatus;
	gpDev->pSetLaserActive = SetLaserDistanceActive;
	gpDev->pGetLaserActive = GetLaserDistanceActive;
	gpDev->pSetLaserMode = setLaserDistanceMode;

    return retval;
}
