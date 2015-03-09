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

int SetupMX6S(PFAD_HW_INDEP_INFO pInfo)
{
	int retval;
	extern struct list_head leds_list;
	extern struct rw_semaphore leds_list_lock;
	struct led_classdev *led_cdev;

	pInfo->bHasLaser = FALSE;
	pInfo->bHasGPS = FALSE;
	pInfo->bHas7173 = FALSE;
	pInfo->bHas5VEnable = FALSE;
	pInfo->bHasDigitalIO = TRUE;
	pInfo->bHasKAKALed = TRUE;
	pInfo->bHasBuzzer = FALSE;
	pInfo->bHasKpBacklight = FALSE;
	pInfo->bHasSoftwareControlledLaser = FALSE;

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

	pr_info("I2C drivers %p and %p\n", pInfo->hI2C1, pInfo->hI2C2);


	//Set up Laser IRQ
	retval = InitLaserIrq(pInfo);
	if (retval) {
		pr_err("Failed to request Laser IRQ\n");
	} else {
		pr_info("Successfully requested Laser IRQ\n");
	}

	// Set up Digital I/O IRQ
	retval = InitDigitalIOIrq(pInfo);
	if (retval) {
		pr_err("Failed to request DIGIN_1 IRQ\n");
	} else {
	pr_info("Successfully requested DIGIN_1 IRQ\n");
	}



	return 0;
}

void InvSetupMX6S(PFAD_HW_INDEP_INFO pInfo)
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
	if (pInfo->red_led_cdev) {
		pInfo->red_led_cdev->brightness = redLed;
		pInfo->red_led_cdev->brightness_set(pInfo->red_led_cdev,
						    redLed);
	}

	if (pInfo->blue_led_cdev) {
		pInfo->blue_led_cdev->brightness = blueLed;
		pInfo->blue_led_cdev->brightness_set(pInfo->blue_led_cdev,
						     blueLed);
	}

	return ERROR_SUCCESS;
}

DWORD getKAKALedState(PFAD_HW_INDEP_INFO pInfo, FADDEVIOCTLLED * pLED)
{
	BOOL redLed = FALSE;
	BOOL blueLed = FALSE;

	if (pInfo->red_led_cdev && pInfo->red_led_cdev->brightness)
		redLed = TRUE;
	if (pInfo->blue_led_cdev && pInfo->blue_led_cdev->brightness)
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

void getDigitalStatus(PFADDEVIOCTLDIGIO pDigioStatus)
{
	pDigioStatus->ucNumOfDigIn = 1;
	pDigioStatus->ucNumOfDigOut = 1;
	pDigioStatus->usInputState = gpio_get_value(DIGIN_1) ? 1 : 0;
	pDigioStatus->usOutputState = gpio_get_value(DIGOUT_1) ? 1 : 0;
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
#ifdef NOT_YET
	PCSP_WDOG_REGS pWdog;
	UINT16 wcr;

	if (pInfo->pWdog == NULL) {
		PHYSICAL_ADDRESS ioPhysicalBase = { CSP_BASE_REG_PA_WDOG1, 0 };
		pInfo->pWdog =
		    MmMapIoSpace(ioPhysicalBase, sizeof(PCSP_WDOG_REGS), FALSE);
	}
	pWdog = pInfo->pWdog;

	//  WDW = continue timer operation in low-power wait mode
	//  WOE = tri-state WDOG output pin
	//  WDA = no software assertion of WDOG output pin
	//  SRS = no software reset of WDOG
	//  WRE = generate reset signal upon watchdog timeout
	//  WDE = disable watchdog (will be enabled after configuration)
	//  WDBG = suspend timer operation in debug mode
	//  WDZST = suspend timer operation in low-power stop mode
	wcr = CSP_BITFVAL(WDOG_WCR_WOE, WDOG_WCR_WOE_TRISTATE) |
	    CSP_BITFVAL(WDOG_WCR_WDA, WDOG_WCR_WDA_NOEFFECT) |
	    CSP_BITFVAL(WDOG_WCR_SRS, WDOG_WCR_SRS_NOEFFECT) |
	    CSP_BITFVAL(WDOG_WCR_WRE, WDOG_WCR_WRE_SIG_RESET) |
	    CSP_BITFVAL(WDOG_WCR_WDE, WDOG_WCR_WDE_DISABLE) |
	    CSP_BITFVAL(WDOG_WCR_WDBG, WDOG_WCR_WDBG_SUSPEND) |
	    CSP_BITFVAL(WDOG_WCR_WDZST, WDOG_WCR_WDZST_SUSPEND) |
	    CSP_BITFVAL(WDOG_WCR_WT,
			(UINT16) (Timeout / 500) & WDOG_WCR_WT_MASK);

	// Configure and then enable the watchdog
	OUTREG16(&pWdog->WCR, wcr);
	wcr |= CSP_BITFVAL(WDOG_WCR_WDE, WDOG_WCR_WDE_ENABLE);
	OUTREG16(&pWdog->WCR, wcr);

	WdogService(pInfo);
#endif
}

BOOL WdogService(PFAD_HW_INDEP_INFO pInfo)
{
#ifdef NOT_YET
	PCSP_WDOG_REGS pWdog;

	if (pInfo->pWdog == NULL) {
		return FALSE;
	}
	pWdog = pInfo->pWdog;

	// 1. write 0x5555
	pWdog->WSR = WDOG_WSR_WSR_RELOAD1;

	// 2. write 0xAAAA
	pWdog->WSR = WDOG_WSR_WSR_RELOAD2;

#endif
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
#ifdef NOT_YET
	static PCSP_PWM_REG pPWM;
	int period;
	int sample;

	if (pPWM == NULL) {
		PHYSICAL_ADDRESS phyAddr;

		DDKClockSetGatingMode(DDK_CLOCK_GATE_INDEX_PWM2_IPG,
				      DDK_CLOCK_GATE_MODE_ENABLED_ALL);

		phyAddr.QuadPart = CSP_BASE_REG_PA_PWM2;
		pPWM =
		    (PCSP_PWM_REG) MmMapIoSpace(phyAddr, sizeof(CSP_PWM_REG),
						FALSE);

		pPWM->PWMCR = 0x00C10416;	// 1 MHz, enabled during debug
	}

	if (usFreq && ucPWM) {
		period = 1000000 / usFreq;

		if (period < 10)
			period = 10;
		if (period > 0xFFFE)
			period = 0xFFFE;

		sample = (period * ucPWM + 50) / 100;
		if (sample == 0)
			sample = 1;

		pPWM->PWMSAR = sample;
		pPWM->PWMPR = period;
		pPWM->PWMCR |= 1;
	} else {
		pPWM->PWMCR &= ~1;
	}
#endif
}

DWORD SetKeypadSubjBacklight(PFAD_HW_INDEP_INFO pInfo,
			     PFADDEVIOCTLSUBJBACKLIGHT pBacklight)
{
	return 0;
}

//------------------------------------------------------------------------------
//
//  Function:  GetKeypadSubjBacklight
//
//  This method returns the keypad backlight in subjective levels.
//      Since this function works in parallel with the older functions setting
//      percentage values, it copes with values that differs from the defined 
//  subjective levels.
DWORD GetKeypadSubjBacklight(PFAD_HW_INDEP_INFO pInfo,
			     PFADDEVIOCTLSUBJBACKLIGHT pBacklight)
{
	return ERROR_SUCCESS;
}

#ifdef NOT_YET
DWORD getLedState(PFAD_HW_INDEP_INFO pInfo, FADDEVIOCTLLED * pLedData)
{
	DWORD pinState;

	DDKGpioReadDataPin(FLIR_GPIO_PIN_POWER_ON_LED, &pinState);

	if (pInfo->dwLedFlash) {
		pLedData->eColor = LED_COLOR_GREEN;
		if (pInfo->dwLedFlash < 500)
			pLedData->eState = LED_FLASH_FAST;
		else
			pLedData->eState = LED_FLASH_SLOW;
	} else if (pinState) {
		pLedData->eColor = LED_COLOR_GREEN;
		pLedData->eState = LED_STATE_ON;
	} else {
		pLedData->eColor = LED_COLOR_OFF;
		pLedData->eState = LED_STATE_OFF;
	}
	return ERROR_SUCCESS;
}

DWORD setLedState(PFAD_HW_INDEP_INFO pInfo, FADDEVIOCTLLED * pLedData)
{
	DWORD dwErr = ERROR_SUCCESS;

	DDKIomuxSetPinMux(FLIR_IOMUX_PIN_POWER_ON_LED);
	DDKIomuxSetPadConfig(FLIR_IOMUX_PAD_POWER_ON_LED);
	DDKGpioSetConfig(FLIR_GPIO_PIN_POWER_ON_LED, DDK_GPIO_DIR_OUT,
			 DDK_GPIO_INTR_NONE);

	if ((pLedData->eColor != LED_COLOR_OFF) &&
	    (pLedData->eState == LED_STATE_ON)) {
		DDKGpioWriteDataPin(FLIR_GPIO_PIN_POWER_ON_LED, 1);
		pInfo->dwLedFlash = 0;
	} else if ((pLedData->eColor == LED_COLOR_OFF) ||
		   (pLedData->eState == LED_STATE_OFF)) {
		DDKGpioWriteDataPin(FLIR_GPIO_PIN_POWER_ON_LED, 0);
		pInfo->dwLedFlash = 0;
	} else if (pLedData->eState == LED_FLASH_SLOW) {
		pInfo->dwLedFlash = 1000;
		SetEvent(pInfo->hLedFlashEvent);
	} else if (pLedData->eState == LED_FLASH_FAST) {
		pInfo->dwLedFlash = 100;
		SetEvent(pInfo->hLedFlashEvent);
	} else {
		dwErr = ERROR_INVALID_PARAMETER;
		pInfo->dwLedFlash = 0;
	}
	return dwErr;
}

DWORD WINAPI fadFlashLed(PVOID pContext)
{
	PFAD_HW_INDEP_INFO pInfo = (PFAD_HW_INDEP_INFO) pContext;
	ULONG timeout;
	BOOL state = FALSE;

	CeSetThreadPriority(GetCurrentThread(), 246);

	while (TRUE) {
		// Wait for ISR interrupt notification
		if (pInfo->dwLedFlash == 0)
			timeout = INFINITE;
		else
			timeout = pInfo->dwLedFlash / 2;
		WaitForSingleObject(pInfo->hLedFlashEvent, timeout);

		if (pInfo->dwLedFlash) {
			state = !state;
			DDKGpioWriteDataPin(FLIR_GPIO_PIN_POWER_ON_LED, state);
		}
	}
	return (0);
}
#endif

DWORD GetKeypadBacklight(PFADDEVIOCTLBACKLIGHT pBacklight)
{
#ifdef NOT_YET
	UINT8 current;

	PmicBacklightGetCurrentLevel(BACKLIGHT_KEYPAD, &current);

	pBacklight->backlight = current * 25;
#endif
	return ERROR_SUCCESS;
}

DWORD SetKeypadBacklight(PFADDEVIOCTLBACKLIGHT pBacklight)
{
#ifdef NOT_YET
	UINT8 level;
	UINT8 backlight = pBacklight->backlight;

	if ((backlight > 100) || (backlight < 0))
		return ERROR_INVALID_PARAMETER;

	//turn the 0-100 range to mc13892's current range 0-4;
	level = (backlight + 12) / 25;

	// set the current level for max for the Display
	PmicBacklightSetCurrentLevel(BACKLIGHT_KEYPAD, level);
	PmicBacklightSetDutyCycle(BACKLIGHT_KEYPAD, 32);
	PmicBacklightSetCurrentLevel(BACKLIGHT_AUX_DISPLAY, level);
	PmicBacklightSetDutyCycle(BACKLIGHT_AUX_DISPLAY, 32);
#endif
	return ERROR_SUCCESS;
}

#ifdef NOT_YET
void BspFadPowerDown(BOOL down)
{
	static FADDEVIOCTLBACKLIGHT backlight;

	DDKIomuxSetPinMux(FLIR_IOMUX_PIN_POWER_ON_LED);
	DDKIomuxSetPadConfig(FLIR_IOMUX_PAD_POWER_ON_LED);
	DDKGpioSetConfig(FLIR_GPIO_PIN_POWER_ON_LED, DDK_GPIO_DIR_OUT,
			 DDK_GPIO_INTR_NONE);
	DDKGpioWriteDataPin(FLIR_GPIO_PIN_POWER_ON_LED, (down == FALSE));

	// Keypad backlight handled here as it must be used on ioctl level to use CSPI
	if (down) {
		// Keypad backlight
		GetKeypadBacklight(&backlight);
		PmicBacklightSetCurrentLevel(BACKLIGHT_KEYPAD, 0);
		PmicBacklightSetCurrentLevel(BACKLIGHT_AUX_DISPLAY, 0);

		// PIRI I2C expander
		SetI2CIoport(VCM_LED_EN, FALSE);
		SetI2CIoport(FOCUS_POWER_EN, FALSE);

		// 5V and 3V3 for USB PHY
		PmicRegisterWrite(MC13892_CHG_USB1_ADDR, 1, 0x409);
	} else {
		// Keypad backlight
		SetKeypadBacklight(&backlight);

		// PIRI I2C expander
		SetI2CIoport(VCM_LED_EN, TRUE);
		SetI2CIoport(FOCUS_POWER_EN, TRUE);

		// 5V and 3V3 for USB PHY
		PmicRegisterWrite(MC13892_CHG_USB1_ADDR, 0x409, 0x409);
	}
}
#endif

void BspGetSubjBackLightLevel(UINT8 * pLow, UINT8 * pMedium, UINT8 * pHigh)
{
	*pLow = 10;
	*pMedium = 40;
	*pHigh = 75;
}
