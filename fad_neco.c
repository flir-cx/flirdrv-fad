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
#include <linux/irq.h>

// Definitions

// Local variables

// Function prototypes

static DWORD setKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED * pLED);
static DWORD getKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED * pLED);
static void getDigitalStatus(PFAD_HW_INDEP_INFO gpDev, PFADDEVIOCTLDIGIO pDigioStatus);
static void setLaserStatus(PFAD_HW_INDEP_INFO gpDev, BOOL on);
static void getLaserStatus(PFAD_HW_INDEP_INFO gpDev,
			   PFADDEVIOCTLLASER pLaserStatus);
static void updateLaserOutput(PFAD_HW_INDEP_INFO gpDev);
static void SetLaserActive(PFAD_HW_INDEP_INFO gpDev, BOOL on);
static BOOL GetLaserActive(PFAD_HW_INDEP_INFO gpDev);
static void SetBuzzerFrequency(USHORT usFreq, UCHAR ucPWM);
static DWORD SetKeypadBacklight(PFADDEVIOCTLBACKLIGHT pBacklight);
static DWORD GetKeypadBacklight(PFADDEVIOCTLBACKLIGHT pBacklight);
static DWORD SetKeypadSubjBacklight(PFAD_HW_INDEP_INFO gpDev,
				    PFADDEVIOCTLSUBJBACKLIGHT pBacklight);
static DWORD GetKeypadSubjBacklight(PFAD_HW_INDEP_INFO gpDev,
				    PFADDEVIOCTLSUBJBACKLIGHT pBacklight);
static BOOL setGPSEnable(BOOL on);
static BOOL getGPSEnable(BOOL * on);
static void WdogInit(PFAD_HW_INDEP_INFO gpDev, UINT32 Timeout);
static BOOL WdogService(PFAD_HW_INDEP_INFO gpDev);
static void BspGetSubjBackLightLevel(UINT8 * pLow, UINT8 * pMedium,
				     UINT8 * pHigh);
static void CleanupHW(PFAD_HW_INDEP_INFO gpDev);
static irqreturn_t fadDigIN1IST(int irq, void *dev_id);
int InitDigitalIOIrq(PFAD_HW_INDEP_INFO gpDev);
void FreeDigitalIOIrq(PFAD_HW_INDEP_INFO gpDev);

// Code

int SetupMX6S(PFAD_HW_INDEP_INFO gpDev)
{
	int retval;
	extern struct list_head leds_list;
	extern struct rw_semaphore leds_list_lock;
	struct led_classdev *led_cdev;


	gpDev->pGetKAKALedState = getKAKALedState;
	gpDev->pSetKAKALedState = setKAKALedState;
	gpDev->pGetDigitalStatus = getDigitalStatus;
	gpDev->pSetLaserStatus = setLaserStatus;
	gpDev->pGetLaserStatus = getLaserStatus;
	gpDev->pUpdateLaserOutput = updateLaserOutput;
	gpDev->pSetBuzzerFrequency = SetBuzzerFrequency;
	gpDev->pSetLaserActive = SetLaserActive;
	gpDev->pGetLaserActive = GetLaserActive;
	gpDev->pSetKeypadBacklight = SetKeypadBacklight;
	gpDev->pGetKeypadBacklight = GetKeypadBacklight;
	gpDev->pSetKeypadSubjBacklight = SetKeypadSubjBacklight;
	gpDev->pGetKeypadSubjBacklight = GetKeypadSubjBacklight;
	gpDev->pSetGPSEnable = setGPSEnable;
	gpDev->pGetGPSEnable = getGPSEnable;
	gpDev->pSetChargerSuspend = NULL;
	gpDev->pWdogInit = WdogInit;
	gpDev->pWdogService = WdogService;
	gpDev->pCleanupHW = CleanupHW;

	gpDev->hI2C1 = i2c_get_adapter(1);
	gpDev->hI2C2 = i2c_get_adapter(2);

	// Laser ON
	if (gpDev->bHasLaser) {
		if (gpio_is_valid(LASER_ON) == 0)
			pr_err("flirdrv-fad: LaserON can not be used\n");
		gpio_request(LASER_ON, "LaserON");
		gpio_direction_input(LASER_ON);
	}

	if (gpDev->bHas5VEnable) {
		if (gpio_is_valid(PIN_3V6A_EN) == 0)
			pr_err("flirdrv-fad: 3V6A_EN can not be used\n");
		gpio_request(PIN_3V6A_EN, "3V6AEN");
		gpio_direction_output(PIN_3V6A_EN, 1);
	}

	if (gpDev->bHasDigitalIO) {
		if (gpio_is_valid(DIGIN_1) == 0)
			pr_err("flirdrv-fad: DIGIN1 can not be used\n");
		gpio_request(DIGIN_1, "DIGIN1");
		gpio_direction_input(DIGIN_1);
		if (gpio_is_valid(DIGOUT_1) == 0)
			pr_err("flirdrv-fad: DIGOUT1 can not be used\n");
		gpio_request(DIGOUT_1, "DIGOUT1");
		gpio_direction_input(DIGOUT_1);
	}

	if (gpDev->bHasBuzzer) {
//        DDKIomuxSetPinMux(FLIR_IOMUX_PIN_PWM_BUZZER);
//        DDKIomuxSetPadConfig(FLIR_IOMUX_PAD_PWM_BUZZER);
	}

	BspGetSubjBackLightLevel(&gpDev->Keypad_bl_low,
				 &gpDev->Keypad_bl_medium,
				 &gpDev->Keypad_bl_high);

	// Find LEDs
	down_read(&leds_list_lock);
	list_for_each_entry(led_cdev, &leds_list, node) {
		if (strcmp(led_cdev->name, "red_led") == 0)
			gpDev->red_led_cdev = led_cdev;
		else if (strcmp(led_cdev->name, "blue_led") == 0)
			gpDev->blue_led_cdev = led_cdev;
	}
	up_read(&leds_list_lock);

	pr_debug("I2C drivers %p and %p\n", gpDev->hI2C1, gpDev->hI2C2);


	//Set up Laser IRQ
	retval = InitLaserIrq(gpDev);
	if (retval) {
		pr_err("flirdrv-fad: Failed to request Laser IRQ\n");
	} else {
		pr_debug("Successfully requested Laser IRQ\n");
	}

	// Set up Digital I/O IRQ
	retval = InitDigitalIOIrq(gpDev);
	if (retval) {
		pr_err("flirdrv-fad: Failed to request DIGIN_1 IRQ\n");
	} else {
	pr_debug("Successfully requested DIGIN_1 IRQ\n");
	}



	return 0;
}

void InvSetupMX6S(PFAD_HW_INDEP_INFO gpDev)
{
	i2c_put_adapter(gpDev->hI2C1);
	i2c_put_adapter(gpDev->hI2C2);
}

void CleanupHW(PFAD_HW_INDEP_INFO gpDev)
{
	// Laser ON
	if (gpDev->bHasLaser) {
		free_irq(gpio_to_irq(LASER_ON), gpDev);
		gpio_free(LASER_ON);
	}

	if (gpDev->bHas5VEnable) {
		gpio_free(PIN_3V6A_EN);
	}

	if (gpDev->bHasDigitalIO) {
		free_irq(gpio_to_irq(DIGIN_1), gpDev);
		gpio_free(DIGIN_1);
		gpio_free(DIGOUT_1);
	}
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

void getDigitalStatus(PFAD_HW_INDEP_INFO gpDev, PFADDEVIOCTLDIGIO pDigioStatus)
{
	pDigioStatus->ucNumOfDigIn = 1;
	pDigioStatus->ucNumOfDigOut = 1;
	pDigioStatus->usInputState = gpio_get_value(DIGIN_1) ? 1 : 0;
	pDigioStatus->usOutputState = gpio_get_value(DIGOUT_1) ? 1 : 0;
}

void setLaserStatus(PFAD_HW_INDEP_INFO gpDev, BOOL on)
{
}

// Laser button has been pressed/released.
// In software controlled laser, we must enable/disable laser here.
void updateLaserOutput(PFAD_HW_INDEP_INFO gpDev)
{
}

void getLaserStatus(PFAD_HW_INDEP_INFO gpDev, PFADDEVIOCTLLASER pLaserStatus)
{
}

BOOL setGPSEnable(BOOL on)
{
	return TRUE;
}

BOOL getGPSEnable(BOOL * on)
{
	// GPS does not seem to receive correct signals when switching 
	// on and off, I2C problems? Temporary fallback solution is to 
	// Keep GPS switched on all the time.
	*on = TRUE;
	return TRUE;
}

void WdogInit(PFAD_HW_INDEP_INFO gpDev, UINT32 Timeout)
{
#ifdef NOT_YET
	PCSP_WDOG_REGS pWdog;
	UINT16 wcr;

	if (gpDev->pWdog == NULL) {
		PHYSICAL_ADDRESS ioPhysicalBase = { CSP_BASE_REG_PA_WDOG1, 0 };
		gpDev->pWdog =
		    MmMapIoSpace(ioPhysicalBase, sizeof(PCSP_WDOG_REGS), FALSE);
	}
	pWdog = gpDev->pWdog;

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

	WdogService(gpDev);
#endif
}

BOOL WdogService(PFAD_HW_INDEP_INFO gpDev)
{
#ifdef NOT_YET
	PCSP_WDOG_REGS pWdog;

	if (gpDev->pWdog == NULL) {
		return FALSE;
	}
	pWdog = gpDev->pWdog;

	// 1. write 0x5555
	pWdog->WSR = WDOG_WSR_WSR_RELOAD1;

	// 2. write 0xAAAA
	pWdog->WSR = WDOG_WSR_WSR_RELOAD2;

#endif
	return TRUE;
}

void SetLaserActive(PFAD_HW_INDEP_INFO gpDev, BOOL on)
{
}

BOOL GetLaserActive(PFAD_HW_INDEP_INFO gpDev)
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

DWORD SetKeypadSubjBacklight(PFAD_HW_INDEP_INFO gpDev,
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
DWORD GetKeypadSubjBacklight(PFAD_HW_INDEP_INFO gpDev,
			     PFADDEVIOCTLSUBJBACKLIGHT pBacklight)
{
	return ERROR_SUCCESS;
}

#ifdef NOT_YET
DWORD getLedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED * pLedData)
{
	DWORD pinState;

	DDKGpioReadDataPin(FLIR_GPIO_PIN_POWER_ON_LED, &pinState);

	if (gpDev->dwLedFlash) {
		pLedData->eColor = LED_COLOR_GREEN;
		if (gpDev->dwLedFlash < 500)
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

DWORD setLedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED * pLedData)
{
	DWORD dwErr = ERROR_SUCCESS;

	DDKIomuxSetPinMux(FLIR_IOMUX_PIN_POWER_ON_LED);
	DDKIomuxSetPadConfig(FLIR_IOMUX_PAD_POWER_ON_LED);
	DDKGpioSetConfig(FLIR_GPIO_PIN_POWER_ON_LED, DDK_GPIO_DIR_OUT,
			 DDK_GPIO_INTR_NONE);

	if ((pLedData->eColor != LED_COLOR_OFF) &&
	    (pLedData->eState == LED_STATE_ON)) {
		DDKGpioWriteDataPin(FLIR_GPIO_PIN_POWER_ON_LED, 1);
		gpDev->dwLedFlash = 0;
	} else if ((pLedData->eColor == LED_COLOR_OFF) ||
		   (pLedData->eState == LED_STATE_OFF)) {
		DDKGpioWriteDataPin(FLIR_GPIO_PIN_POWER_ON_LED, 0);
		gpDev->dwLedFlash = 0;
	} else if (pLedData->eState == LED_FLASH_SLOW) {
		gpDev->dwLedFlash = 1000;
		SetEvent(gpDev->hLedFlashEvent);
	} else if (pLedData->eState == LED_FLASH_FAST) {
		gpDev->dwLedFlash = 100;
		SetEvent(gpDev->hLedFlashEvent);
	} else {
		dwErr = ERROR_INVALID_PARAMETER;
		gpDev->dwLedFlash = 0;
	}
	return dwErr;
}

DWORD WINAPI fadFlashLed(PVOID pContext)
{
	PFAD_HW_INDEP_INFO gpDev = (PFAD_HW_INDEP_INFO) pContext;
	ULONG timeout;
	BOOL state = FALSE;

	CeSetThreadPriority(GetCurrentThread(), 246);

	while (TRUE) {
		// Wait for ISR interrupt notification
		if (gpDev->dwLedFlash == 0)
			timeout = INFINITE;
		else
			timeout = gpDev->dwLedFlash / 2;
		WaitForSingleObject(gpDev->hLedFlashEvent, timeout);

		if (gpDev->dwLedFlash) {
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


irqreturn_t fadDigIN1IST(int irq, void *dev_id)
{
	PFAD_HW_INDEP_INFO gpDev = (PFAD_HW_INDEP_INFO) dev_id;
	static BOOL bWaitForNeg = FALSE;

	ApplicationEvent(gpDev, FAD_DIGIN_EVENT);
	if (bWaitForNeg) {
		irq_set_irq_type(gpio_to_irq(DIGIN_1),
				 IRQF_TRIGGER_HIGH | IRQF_ONESHOT);
		bWaitForNeg = FALSE;
	} else {
		irq_set_irq_type(gpio_to_irq(DIGIN_1),
				 IRQF_TRIGGER_LOW | IRQF_ONESHOT);
		bWaitForNeg = TRUE;
	}

	pr_err("flirdrv-fad: fadDigIN1IST\n");

	return IRQ_HANDLED;
}
/** 
 * InitDigitalIOIrq
 * 
 * Initialize digital io irq if system requires it
 *
 * @param gpDev 
 * 
 * @return retval
 */
int InitDigitalIOIrq(PFAD_HW_INDEP_INFO gpDev)
{
	int ret = 0;
	if (gpDev->bHasDigitalIO) {
		ret = request_irq(gpio_to_irq(DIGIN_1), fadDigIN1IST,
				  IRQF_TRIGGER_HIGH | IRQF_ONESHOT, "Digin1", gpDev);
	}
	return ret;
}

void FreeDigitalIOIrq(PFAD_HW_INDEP_INFO gpDev)
{
	if (gpDev->bHasLaser){
		free_irq(gpio_to_irq(DIGIN_1), gpDev);
	}
}
