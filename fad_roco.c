// SPDX-License-Identifier: GPL-2.0-or-later
/***********************************************************************
 *
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

// Definitions
#define ENOLASERIRQ 1
#define ENODIGIOIRQ 2
// Local variables

// Function prototypes
static DWORD setKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED *pLED);
static DWORD getKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED *pLED);
static void getDigitalStatus(PFAD_HW_INDEP_INFO gpDev,
			     PFADDEVIOCTLDIGIO pDigioStatus);
static void SetBuzzerFrequency(USHORT usFreq, UCHAR ucPWM);
static DWORD SetKeypadSubjBacklight(PFAD_HW_INDEP_INFO gpDev,
				    PFADDEVIOCTLSUBJBACKLIGHT pBacklight);
static DWORD GetKeypadSubjBacklight(PFAD_HW_INDEP_INFO gpDev,
				    PFADDEVIOCTLSUBJBACKLIGHT pBacklight);
static BOOL setGPSEnable(BOOL on);
static BOOL getGPSEnable(BOOL *on);
static void WdogInit(PFAD_HW_INDEP_INFO gpDev, UINT32 Timeout);
static BOOL WdogService(PFAD_HW_INDEP_INFO gpDev);
#ifdef CONFIG_OF
static void BspGetSubjBackLightLevel(UINT8 * pLow, UINT8 * pMedium,
				     UINT8 * pHigh);
#endif
static void CleanupHW(PFAD_HW_INDEP_INFO gpDev);

// Code

int SetupMX6Q(PFAD_HW_INDEP_INFO gpDev)
{
	int retval = 0;
#ifdef CONFIG_OF
	u32 tmp;
	extern struct list_head leds_list;
	extern struct rw_semaphore leds_list_lock;
	struct led_classdev *led_cdev;
#endif

	gpDev->pGetKAKALedState = getKAKALedState;
	gpDev->pSetKAKALedState = setKAKALedState;
	gpDev->pGetDigitalStatus = getDigitalStatus;
	gpDev->pSetBuzzerFrequency = SetBuzzerFrequency;
	gpDev->pSetKeypadBacklight = NULL;
	gpDev->pGetKeypadBacklight = NULL;
	gpDev->pSetKeypadSubjBacklight = SetKeypadSubjBacklight;
	gpDev->pGetKeypadSubjBacklight = GetKeypadSubjBacklight;
	gpDev->pSetGPSEnable = setGPSEnable;
	gpDev->pGetGPSEnable = getGPSEnable;
	gpDev->pSetChargerSuspend = NULL;
	gpDev->pWdogInit = WdogInit;
	gpDev->pWdogService = WdogService;
	gpDev->pCleanupHW = CleanupHW;

#ifdef CONFIG_OF

	retval = SetupLaserPointer(gpDev);
	if (retval) {
		pr_err("Could not initilize laser pointer!\n");
		goto EXIT;
	}

//      pr_err("Searching for LEDs\n");
	down_read(&leds_list_lock);
	list_for_each_entry(led_cdev, &leds_list, node) {
		if (strcmp(led_cdev->name, "pikeled") == 0) {
			gpDev->pike_cdev = led_cdev;

		} else if (strcmp(led_cdev->name, "pijkled") == 0) {
			gpDev->pijk_cdev = led_cdev;
		}
	}
	up_read(&leds_list_lock);

/* Configure I2C from Devicetree */
	retval = of_property_read_u32_index(gpDev->node, "hI2C1", 0, &tmp);
	if (retval) {
		pr_err("flirdrv-fad: couldn't read hI2C1 from DT!\n");
		goto EXIT_I2C1;
	}

	gpDev->hI2C1 = i2c_get_adapter(tmp);

	retval = of_property_read_u32_index(gpDev->node, "hI2C2", 0, &tmp);
	if (retval) {
		pr_err("flirdrv-fad: couldn't read hI2C2 from DT!\n");
		goto EXIT_I2C2;
	}

	gpDev->hI2C2 = i2c_get_adapter(tmp);

	pr_debug("flirdrv-fad: I2C drivers %p and %p\n", gpDev->hI2C1,
		 gpDev->hI2C2);

	/* Configure devices (bools) from DT */
	//Do not care about return value of function
	//If property is missing, assume device doesnt exist!
	//Better to wrap this in separate function... (int -> bool etc...)
	of_property_read_u32_index(gpDev->node, "HasGPS", 0, &gpDev->bHasGPS);
	of_property_read_u32_index(gpDev->node, "Has7173", 0, &gpDev->bHas7173);
	of_property_read_u32_index(gpDev->node, "Has5VEnable", 0,
				   &gpDev->bHas5VEnable);
	of_property_read_u32_index(gpDev->node, "HasDigitalIO", 0,
				   &gpDev->bHasDigitalIO);
	of_property_read_u32_index(gpDev->node, "HasKAKALed", 0,
				   &gpDev->bHasKAKALed);
	of_property_read_u32_index(gpDev->node, "HasBuzzer", 0,
				   &gpDev->bHasBuzzer);
	of_property_read_u32_index(gpDev->node, "HasKpBacklight", 0,
				   &gpDev->bHasKpBacklight);
	BspGetSubjBackLightLevel(&gpDev->Keypad_bl_low,
				 &gpDev->Keypad_bl_medium,
				 &gpDev->Keypad_bl_high);

	gpDev->reg_opt3v6 =
	    regulator_get(&gpDev->pLinuxDevice->dev, "rori_opt_3v6");
	if (IS_ERR(gpDev->reg_opt3v6)) {
		pr_err("Error on rori_opt_3v6 get\n");
	} else {
		retval = regulator_enable(gpDev->reg_opt3v6);
		if (retval) {
			pr_err
			    ("flirdrv-fad: Could not enable rori_opt_3v6 regulators\n");
		}
	}

	goto EXIT;

EXIT_I2C2:
	i2c_put_adapter(gpDev->hI2C1);
EXIT_I2C1:
EXIT:
#endif
	return retval;
}

/**
 * Inverse setup done in SetupMX6Q...
 *
 * @param gpDev
 */
void InvSetupMX6Q(PFAD_HW_INDEP_INFO gpDev)
{
	InvSetupLaserPointer(gpDev);

#ifdef CONFIG_OF
	if (IS_ERR(gpDev->reg_opt3v6)) {
		pr_err("Error on rori_opt_5v0 get\n");
	} else {
		regulator_disable(gpDev->reg_opt3v6);
		regulator_put(gpDev->reg_opt3v6);
	};
#endif

#ifdef CONFIG_OF
	gpDev->pijk_cdev->brightness = 0;
	gpDev->pijk_cdev->brightness_set(gpDev->pijk_cdev,
					 gpDev->pijk_cdev->brightness);
	gpDev->pike_cdev->brightness = 0;
	gpDev->pike_cdev->brightness_set(gpDev->pike_cdev,
					 gpDev->pike_cdev->brightness);
#endif

	i2c_put_adapter(gpDev->hI2C1);
	i2c_put_adapter(gpDev->hI2C2);

}

void CleanupHW(PFAD_HW_INDEP_INFO gpDev)
{
	pr_warn("%s handled by unloading the FAD kernel module...", __func__);
}

DWORD setKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED *pLED)
{
	return 0;
}

DWORD getKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED *pLED)
{
	return 0;
}

void getDigitalStatus(PFAD_HW_INDEP_INFO gpDev, PFADDEVIOCTLDIGIO pDigioStatus)
{
}

BOOL setGPSEnable(BOOL on)
{
	return TRUE;
}

BOOL getGPSEnable(BOOL *on)
{
	// GPS does not seem to receive correct signals when switching
	// on and off, I2C problems? Temporary fallback solution is to
	// Keep GPS switched on all the time.
	*on = TRUE;
	return TRUE;
}

void WdogInit(PFAD_HW_INDEP_INFO gpDev, UINT32 Timeout)
{
	pr_debug("flirdrv-fad: Watchdog init deprecated\n");
}

BOOL WdogService(PFAD_HW_INDEP_INFO gpDev)
{
	pr_debug("flirdrv-fad: WatchdogService deprecated\n");
	return TRUE;
}


void SetBuzzerFrequency(USHORT usFreq, UCHAR ucPWM)
{
	pr_debug("%s not implemented\n", __func__);
}

DWORD SetKeypadSubjBacklight(PFAD_HW_INDEP_INFO gpDev,
			     PFADDEVIOCTLSUBJBACKLIGHT pBacklight)
{
	int brightness;
//      pr_err("flirdrv-fad: SetKeypadSubjBacklight!\n");
	switch (pBacklight->subjectiveBacklight) {
	case KP_SUBJ_LOW:
		brightness = gpDev->Keypad_bl_low;
		break;
	case KP_SUBJ_MEDIUM:
		brightness = gpDev->Keypad_bl_medium;
		break;
	case KP_SUBJ_HIGH:
		brightness = gpDev->Keypad_bl_high;
		break;
	case KP_SUBJ_OFF:
		brightness = 0;
		break;
	default:
		brightness = 0;
		break;
	}

#ifdef CONFIG_OF
	gpDev->pijk_cdev->brightness = brightness;
	gpDev->pijk_cdev->brightness_set(gpDev->pijk_cdev,
					 gpDev->pijk_cdev->brightness);

	gpDev->pike_cdev->brightness = brightness;
	gpDev->pike_cdev->brightness_set(gpDev->pike_cdev,
					 gpDev->pike_cdev->brightness);
#endif

	return ERROR_SUCCESS;
}

/**
 *
 *  Function:  GetKeypadSubjBacklight
 *
 *    This method returns the keypad backlight in subjective levels.
 *    Since this function works in parallel with the older functions setting
 *    percentage values, it copes with values that differs from the defined
 *    subjective levels.
 *
 * @param gpDev
 * @param pBacklight
 *
 * @return
 */
DWORD GetKeypadSubjBacklight(PFAD_HW_INDEP_INFO gpDev,
			     PFADDEVIOCTLSUBJBACKLIGHT pBacklight)
{
#ifdef CONFIG_OF
	int brightness;

	UINT8 bl_value;

	brightness = gpDev->pike_cdev->brightness;

	bl_value = (UINT8) brightness;

	if (brightness == 0)
		pBacklight->subjectiveBacklight = KP_SUBJ_LOW;
	else if (brightness <=
		 gpDev->Keypad_bl_low + (gpDev->Keypad_bl_medium -
					 gpDev->Keypad_bl_low) / 2)
		pBacklight->subjectiveBacklight = KP_SUBJ_LOW;
	else if (brightness <=
		 gpDev->Keypad_bl_medium + (gpDev->Keypad_bl_high -
					    gpDev->Keypad_bl_medium) / 2)
		pBacklight->subjectiveBacklight = KP_SUBJ_MEDIUM;
	else
		pBacklight->subjectiveBacklight = KP_SUBJ_HIGH;
#endif

	// RETAILMSG(1, (_T("GetKeypadSubjBacklight %x\r\n"),pBacklight->subjectiveBacklight));
	return ERROR_SUCCESS;
}

#ifdef CONFIG_OF
void BspGetSubjBackLightLevel(UINT8 *pLow, UINT8 *pMedium, UINT8 *pHigh)
{
	*pLow = 10;
	*pMedium = 40;
	*pHigh = 75;
}
#endif
