/***********************************************************************
*                                                                     
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
#include <linux/of_gpio.h>

// Definitions
#define ENOLASERIRQ 1
#define ENODIGIOIRQ 2
// Local variables

// Function prototypes


static DWORD setKAKALedState(PFAD_HW_INDEP_INFO pInfo, FADDEVIOCTLLED * pLED);
static DWORD getKAKALedState(PFAD_HW_INDEP_INFO pInfo, FADDEVIOCTLLED * pLED);
static void getDigitalStatus(PFADDEVIOCTLDIGIO pDigioStatus);
static void setLaserStatus(PFAD_HW_INDEP_INFO pInfo, BOOL LaserStatus);
static void getLaserStatus(PFAD_HW_INDEP_INFO pInfo,
			   PFADDEVIOCTLLASER pLaserStatus);
static void updateLaserOutput(PFAD_HW_INDEP_INFO pInfo);
static void SetLaserActive(PFAD_HW_INDEP_INFO pInfo, BOOL bEnable);
static BOOL GetLaserActive(PFAD_HW_INDEP_INFO pInfo);
static void SetBuzzerFrequency(USHORT usFreq, UCHAR ucPWM);
static DWORD SetKeypadBacklight(PFADDEVIOCTLBACKLIGHT pBacklight);
static DWORD GetKeypadBacklight(PFADDEVIOCTLBACKLIGHT pBacklight);
static DWORD SetKeypadSubjBacklight(PFAD_HW_INDEP_INFO pInfo,
				    PFADDEVIOCTLSUBJBACKLIGHT pBacklight);
static DWORD GetKeypadSubjBacklight(PFAD_HW_INDEP_INFO pInfo,
				    PFADDEVIOCTLSUBJBACKLIGHT pBacklight);
static BOOL setGPSEnable(BOOL enabled);
static BOOL getGPSEnable(BOOL * enabled);
static void WdogInit(PFAD_HW_INDEP_INFO pInfo, UINT32 Timeout);
static BOOL WdogService(PFAD_HW_INDEP_INFO pInfo);
static void BspGetSubjBackLightLevel(UINT8 * pLow, UINT8 * pMedium,
				     UINT8 * pHigh);
static void CleanupHW(PFAD_HW_INDEP_INFO pInfo);

// Code

int SetupMX6Q(PFAD_HW_INDEP_INFO pInfo)
{
	extern struct list_head leds_list;
	extern struct rw_semaphore leds_list_lock;
	struct led_classdev *led_cdev;
	int retval;
	u32 tmp;


	pInfo->pGetKAKALedState = getKAKALedState;
	pInfo->pSetKAKALedState = setKAKALedState;
	pInfo->pGetDigitalStatus = getDigitalStatus;
	pInfo->pSetLaserStatus = setLaserStatus;
	pInfo->pGetLaserStatus = getLaserStatus;
	pInfo->pUpdateLaserOutput = updateLaserOutput;
	pInfo->pSetBuzzerFrequency = SetBuzzerFrequency;
	pInfo->pSetLaserActive = SetLaserActive;
	pInfo->pGetLaserActive = GetLaserActive;
	pInfo->pSetKeypadBacklight = SetKeypadBacklight;
	pInfo->pGetKeypadBacklight = GetKeypadBacklight;
	pInfo->pSetKeypadSubjBacklight = SetKeypadSubjBacklight;
	pInfo->pGetKeypadSubjBacklight = GetKeypadSubjBacklight;
	pInfo->pSetGPSEnable = setGPSEnable;
	pInfo->pGetGPSEnable = getGPSEnable;
	pInfo->pWdogInit = WdogInit;
	pInfo->pWdogService = WdogService;
	pInfo->pCleanupHW = CleanupHW;


/* Configure I2C from Devicetree */
	retval = of_property_read_u32_index(pInfo->node, "hI2C1", 0, &tmp);
	if (retval) {
		pr_err("flirdrv-fad: couldn't read hI2C1 from DT!\n");
		goto EXIT_I2C1;
	}

	pInfo->hI2C1 = i2c_get_adapter(tmp);

	retval = of_property_read_u32_index(pInfo->node, "hI2C2", 0, &tmp);
	if (retval) {
		pr_err("flirdrv-fad: couldn't read hI2C2 from DT!\n");
		goto EXIT_I2C2;
	}

	pInfo->hI2C2 = i2c_get_adapter(tmp);

	pr_info("flirdrv-fad: I2C drivers %p and %p\n", pInfo->hI2C1, pInfo->hI2C2);


	/* Configure devices (bools) from DT */
	//Do not care about return value of function
	//If property is missing, assume device doesnt exist!
	//Better to wrap this in separate function... (int -> bool etc...)
	of_property_read_u32_index(pInfo->node, "hasLaser", 0, &pInfo->bHasLaser);
	of_property_read_u32_index(pInfo->node, "HasGPS", 0, &pInfo->bHasGPS);
	of_property_read_u32_index(pInfo->node, "Has7173", 0, &pInfo->bHas7173);
	of_property_read_u32_index(pInfo->node, "Has5VEnable", 0, &pInfo->bHas5VEnable);
	of_property_read_u32_index(pInfo->node, "HasDigitalIO", 0, &pInfo->bHasDigitalIO);
	of_property_read_u32_index(pInfo->node, "HasKAKALed", 0, &pInfo->bHasKAKALed);
	of_property_read_u32_index(pInfo->node, "HasBuzzer", 0, &pInfo->bHasBuzzer);
	of_property_read_u32_index(pInfo->node, "HasKpBacklight",
				   0, &pInfo->bHasKpBacklight);
	of_property_read_u32_index(pInfo->node, "HasSoftwareControlledLaser",
				   0, &pInfo->bHasSoftwareControlledLaser);

	if(pInfo->bHasLaser)
		pr_info("flirdrv-fad: HasLaser\n");
	if(pInfo->bHasGPS)
		pr_info("flirdrv-fad: HasGPS\n");
	if(pInfo->bHas7173)
		pr_info("flirdrv-fad: Has7173\n");
	if(pInfo->bHas5VEnable)
		pr_info("flirdrv-fad: Has5VEnable\n");
	if(pInfo->bHasDigitalIO)
		pr_info("flirdrv-fad: HasDigitalIO\n");
	if(pInfo->bHasKAKALed)
		pr_info("flirdrv-fad: HasKAKALed\n");
	if(pInfo->bHasBuzzer)
		pr_info("flirdrv-fad: HasBuzzer\n");
	if(pInfo->bHasKpBacklight)
		pr_info("flirdrv-fad: HasKpBacklight\n");
	if(pInfo->bHasSoftwareControlledLaser)
		pr_info("flirdrv-fad: HasSoftwareControlledLaser\n");

	BspGetSubjBackLightLevel(&pInfo->Keypad_bl_low,
				 &pInfo->Keypad_bl_medium,
				 &pInfo->Keypad_bl_high);

	// Find LEDs
	down_read(&leds_list_lock);
	list_for_each_entry(led_cdev, &leds_list, node) {
		if (strcmp(led_cdev->name, "red_led") == 0)
			pInfo->red_led_cdev = led_cdev;
		else if (strcmp(led_cdev->name, "blue_led") == 0)
			pInfo->blue_led_cdev = led_cdev;
	}
	up_read(&leds_list_lock);



	if (pInfo->bHasLaser) {
		int pin;
		pin = of_get_named_gpio_flags(pInfo->node, "laser_on-gpios", 0, NULL);

		if (gpio_is_valid(pin) == 0)
			pr_err("flirdrv-fad: LaserON can not be used\n");
		gpio_request(pin, "LaserON");
		gpio_direction_input(pin);
		retval = InitLaserIrq(pInfo);
	}


	if (retval) {
		pr_err("flirdrv-fad: Failed to request Laser IRQ\n");
		retval = -ENOLASERIRQ;
		goto EXIT_NO_LASERIRQ;
	}

	goto EXIT;

EXIT_NO_LASERIRQ:
	if(pInfo->bHasLaser){
		FreeLaserIrq(pInfo);
	}

	i2c_put_adapter(pInfo->hI2C2);
EXIT_I2C2:
	i2c_put_adapter(pInfo->hI2C1);
EXIT_I2C1:
EXIT:
	return retval;
}

/** 
 * Inverse setup done in SetupMX6Q...
 * 
 * @param pInfo 
 */
void InvSetupMX6Q(PFAD_HW_INDEP_INFO pInfo)
{
	if (pInfo->bHasLaser) {
		int pin;
		FreeLaserIrq(pInfo);
		pin = of_get_named_gpio_flags(pInfo->node, "laser_on-gpios", 0, NULL);
		gpio_free(pin);
	}


	i2c_put_adapter(pInfo->hI2C1);
	i2c_put_adapter(pInfo->hI2C2);

}

void CleanupHW(PFAD_HW_INDEP_INFO pInfo)
{
	pr_warn("flirdrv-fad: CleanupHW handled by unloading the FAD kernel module...");
}

DWORD setKAKALedState(PFAD_HW_INDEP_INFO pInfo, FADDEVIOCTLLED * pLED)
{
	return 0;
}

DWORD getKAKALedState(PFAD_HW_INDEP_INFO pInfo, FADDEVIOCTLLED * pLED)
{
	return 0;
}

void getDigitalStatus(PFADDEVIOCTLDIGIO pDigioStatus)
{
}

void setLaserStatus(PFAD_HW_INDEP_INFO pInfo, BOOL LaserStatus)
{
	ledtrig_laser_ctrl(LaserStatus);
}

// Laser button has been pressed/released.
// In software controlled laser, we must enable/disable laser here.
void updateLaserOutput(PFAD_HW_INDEP_INFO pInfo)
{
}

void getLaserStatus(PFAD_HW_INDEP_INFO pInfo, PFADDEVIOCTLLASER pLaserStatus)
{
}

BOOL setGPSEnable(BOOL enabled)
{
	return TRUE;
}

BOOL getGPSEnable(BOOL * enabled)
{
	// GPS does not seem to receive correct signals when switching 
	// on and off, I2C problems? Temporary fallback solution is to 
	// Keep GPS switched on all the time.
	*enabled = TRUE;
	return TRUE;
}

void WdogInit(PFAD_HW_INDEP_INFO pInfo, UINT32 Timeout)
{
	pr_info("flirdrv-fad: Watchdog init deprecated\n");
}

BOOL WdogService(PFAD_HW_INDEP_INFO pInfo)
{
	pr_info("flirdrv-fad: WatchdogService deprecated\n");
	return TRUE;
}

void SetLaserActive(PFAD_HW_INDEP_INFO pInfo, BOOL bEnable)
{
	ledtrig_lasersw_ctrl(bEnable);
}

BOOL GetLaserActive(PFAD_HW_INDEP_INFO pInfo)
{
	return FALSE;
}

void SetBuzzerFrequency(USHORT usFreq, UCHAR ucPWM)
{
	pr_info("flirdrv-fad SetBuzzerFrequency not implemented\n");
}

DWORD SetKeypadSubjBacklight(PFAD_HW_INDEP_INFO pInfo,
			     PFADDEVIOCTLSUBJBACKLIGHT pBacklight)
{
	pr_info("flirdrv-fad SetKeypadSubjBacklight not implemented\n");
	return 0;
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
 * @param pInfo 
 * @param pBacklight 
 * 
 * @return 
 */
DWORD GetKeypadSubjBacklight(PFAD_HW_INDEP_INFO pInfo,
			     PFADDEVIOCTLSUBJBACKLIGHT pBacklight)
{
	pr_info("flirdrv-fad GetKeypadSubjBackligt not implemented\n");
	return 0;
}


DWORD GetKeypadBacklight(PFADDEVIOCTLBACKLIGHT pBacklight)
{
	pr_info("flirdrv-fad: GetKeypadBackligt not implemented\n");
	return 0;
}

DWORD SetKeypadBacklight(PFADDEVIOCTLBACKLIGHT pBacklight)
{
	pr_info("flirdrv-fad: SetKeypadBacklight not implemented\n");
	return 0;
}

void BspGetSubjBackLightLevel(UINT8 * pLow, UINT8 * pMedium, UINT8 * pHigh)
{
	*pLow = 10;
	*pMedium = 40;
	*pHigh = 75;
}
