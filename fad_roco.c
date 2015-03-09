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

// Definitions

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

	pInfo->bHasLaser = TRUE;
	pInfo->bHasGPS = TRUE;
	pInfo->bHas7173 = FALSE;
	pInfo->bHas5VEnable = FALSE;
	pInfo->bHasDigitalIO = FALSE;
	pInfo->bHasKAKALed = FALSE;
	pInfo->bHasBuzzer = TRUE;
	pInfo->bHasKpBacklight = TRUE;
	pInfo->bHasSoftwareControlledLaser = TRUE;
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

	pInfo->hI2C1 = i2c_get_adapter(1);
	pInfo->hI2C2 = i2c_get_adapter(2);

	// Laser ON
	if (pInfo->bHasLaser) {
		if (gpio_is_valid(LASER_ON) == 0)
			pr_err("LaserON can not be used\n");
		gpio_request(LASER_ON, "LaserON");
		gpio_direction_input(LASER_ON);
	}

	if (pInfo->bHas5VEnable) {
		if (gpio_is_valid(PIN_3V6A_EN) == 0)
			pr_err("3V6A_EN can not be used\n");
		gpio_request(PIN_3V6A_EN, "3V6AEN");
		gpio_direction_output(PIN_3V6A_EN, 1);
	}

	if (pInfo->bHasDigitalIO) {
		if (gpio_is_valid(DIGIN_1) == 0)
			pr_err("DIGIN1 can not be used\n");
		gpio_request(DIGIN_1, "DIGIN1");
		gpio_direction_input(DIGIN_1);
		if (gpio_is_valid(DIGOUT_1) == 0)
			pr_err("DIGOUT1 can not be used\n");
		gpio_request(DIGOUT_1, "DIGOUT1");
		gpio_direction_input(DIGOUT_1);
	}

	if (pInfo->bHasBuzzer) {
//        DDKIomuxSetPinMux(FLIR_IOMUX_PIN_PWM_BUZZER);
//        DDKIomuxSetPadConfig(FLIR_IOMUX_PAD_PWM_BUZZER);
	}

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
	return 0;
}

/** 
 * Inverse setup done in SetupMX6Q...
 * 
 * @param pInfo 
 */
void InvSetupMX6Q(PFAD_HW_INDEP_INFO pInfo)
{
	i2c_put_adapter(pInfo->hI2C1);
	i2c_put_adapter(pInfo->hI2C2);

}

void CleanupHW(PFAD_HW_INDEP_INFO pInfo)
{
	// Laser ON
	if (pInfo->bHasLaser) {
		free_irq(gpio_to_irq(LASER_ON), pInfo);
		gpio_free(LASER_ON);
	}

	if (pInfo->bHas5VEnable) {
		gpio_free(PIN_3V6A_EN);
	}

	if (pInfo->bHasDigitalIO) {
		free_irq(gpio_to_irq(DIGIN_1), pInfo);
		gpio_free(DIGIN_1);
		gpio_free(DIGOUT_1);
	}
}

DWORD setKAKALedState(PFAD_HW_INDEP_INFO pInfo, FADDEVIOCTLLED * pLED)
{
	return ERROR_SUCCESS;
}

DWORD getKAKALedState(PFAD_HW_INDEP_INFO pInfo, FADDEVIOCTLLED * pLED)
{
	return ERROR_SUCCESS;
}

void getDigitalStatus(PFADDEVIOCTLDIGIO pDigioStatus)
{
}

void setLaserStatus(PFAD_HW_INDEP_INFO pInfo, BOOL LaserStatus)
{
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
	return ERROR_SUCCESS;
}


DWORD GetKeypadBacklight(PFADDEVIOCTLBACKLIGHT pBacklight)
{
	pr_info("GetKeypadBackligt not implemented\n");
	return ERROR_SUCCESS;
}

DWORD SetKeypadBacklight(PFADDEVIOCTLBACKLIGHT pBacklight)
{
	pr_info("SetKeypadBacklight not implemented\n");
	return ERROR_SUCCESS;
}

void BspGetSubjBackLightLevel(UINT8 * pLow, UINT8 * pMedium, UINT8 * pHigh)
{
	*pLow = 10;
	*pMedium = 40;
	*pHigh = 75;
}
