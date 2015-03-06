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

#define DEBUG_CLOCK_OUT 0	// Set to 1 to enable clock outputs on CLKO and CLKO2 (for debugging)

// Definitions

#define IOPORT_I2C_ADDR     0x46

#define VCM_LED_EN          0
#define LASER_SWITCH_ON     2
#define FOCUS_POWER_EN      3
#define LASER_SOFT_ON       5

typedef struct {
	volatile unsigned long PWMCR;
	volatile unsigned long PWMSR;
	volatile unsigned long PWMIR;
	volatile unsigned long PWMSAR;
	volatile unsigned long PWMPR;
	volatile unsigned long PWMCNR;
} CSP_PWM_REG, *PCSP_PWM_REG;

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
static UINT8 KeypadLowMediumLimit(PFAD_HW_INDEP_INFO pInfo);
static UINT8 KeypadMediumHighLimit(PFAD_HW_INDEP_INFO pInfo);
static void CleanupHW(PFAD_HW_INDEP_INFO pInfo);

// Code

//-----------------------------------------------------------------------------
//
// Function: InitI2CIoport
//
// This function will set up the ioport on PIRI as outputs
//
// Parameters:
//
// Returns:
//      Returns status of init code.
//
//-----------------------------------------------------------------------------

static BOOL InitI2CIoport(PFAD_HW_INDEP_INFO pInfo)
{
	struct i2c_msg msgs[2];
	int res;
	UCHAR buf[2];
	UCHAR cmd;

	LOCK_IOPORT(pInfo);

	msgs[0].addr = IOPORT_I2C_ADDR >> 1;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &cmd;
	msgs[1].addr = IOPORT_I2C_ADDR >> 1;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = buf;

	cmd = 3;		// Read config register
	res = i2c_transfer(pInfo->hI2C1, msgs, 2);

	if (res > 0) {
		msgs[0].len = 2;
		msgs[0].buf = buf;
		buf[1] = buf[0] & ~((1 << VCM_LED_EN) |
				    (1 << LASER_SWITCH_ON) |
				    (1 << FOCUS_POWER_EN) |
				    (1 << LASER_SOFT_ON));
//        pr_err("FAD: IOPORT %02X -> %02X\n", buf[0], buf[1]);
		buf[0] = 3;
		res = i2c_transfer(pInfo->hI2C1, msgs, 1);
	}

	UNLOCK_IOPORT(pInfo);

	return (res > 0);
}

//-----------------------------------------------------------------------------
//
// Function: SetI2CIoport
//
// This function will set one bit of the ioport on PIRI
//
// Parameters:
//
// Returns:
//      Returns status of set operation.
//
//-----------------------------------------------------------------------------

static BOOL SetI2CIoport(PFAD_HW_INDEP_INFO pInfo, UCHAR bit, BOOL value)
{
	struct i2c_msg msgs[2];
	int res;
	UCHAR buf[2];
	UCHAR cmd;

	LOCK_IOPORT(pInfo);

	msgs[0].addr = IOPORT_I2C_ADDR >> 1;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &cmd;
	msgs[1].addr = IOPORT_I2C_ADDR >> 1;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = buf;

	cmd = 1;		// Read output register
	res = i2c_transfer(pInfo->hI2C1, msgs, 2);

	if (res > 0) {
		msgs[0].len = 2;
		msgs[0].buf = buf;
		buf[1] = buf[0];	// Initial value before changes
		if (value)
			buf[1] |= (1 << bit);
		else
			buf[1] &= ~(1 << bit);
//        pr_err("FAD: IO Set %02X -> %02X\n", buf[0], buf[1]);
		buf[0] = 1;	// Set output value

		res = i2c_transfer(pInfo->hI2C1, msgs, 1);
	}

	UNLOCK_IOPORT(pInfo);

	return (res > 0);
}

//-----------------------------------------------------------------------------
//
// Function: GetI2CIoport
//
// This function will get one bit of the ioport on PIRI
//
// Parameters:
//
// Returns:
//      Returns status of bit.
//
//-----------------------------------------------------------------------------

static BOOL GetI2CIoport(PFAD_HW_INDEP_INFO pInfo, UCHAR bit)
{
	struct i2c_msg msgs[2];
	int res;
	UCHAR buf[2];
	UCHAR cmd;

	LOCK_IOPORT(pInfo);

	msgs[0].addr = IOPORT_I2C_ADDR >> 1;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &cmd;
	msgs[1].addr = IOPORT_I2C_ADDR >> 1;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = buf;

	cmd = 0;		// Read port register
	buf[0] = 0;
	res = i2c_transfer(pInfo->hI2C1, msgs, 2);

	UNLOCK_IOPORT(pInfo);

	return ((buf[0] & (1 << bit)) != 0);
}

void SetupMX51(PFAD_HW_INDEP_INFO pInfo)
{
	pInfo->bHasLaser = TRUE;
	pInfo->bHasGPS = TRUE;
	pInfo->bHas7173 = FALSE;
	pInfo->bHas5VEnable = TRUE;
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

	pInfo->hI2C1 = i2c_get_adapter(0);
	pInfo->hI2C2 = i2c_get_adapter(1);

	SetI2CIoport(pInfo, VCM_LED_EN, TRUE);
	SetI2CIoport(pInfo, LASER_SWITCH_ON, FALSE);
	SetI2CIoport(pInfo, FOCUS_POWER_EN, TRUE);
	SetI2CIoport(pInfo, LASER_SOFT_ON, FALSE);
	InitI2CIoport(pInfo);

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

	if (pInfo->bHasBuzzer) {
//        DDKIomuxSetPinMux(FLIR_IOMUX_PIN_PWM_BUZZER);
//        DDKIomuxSetPadConfig(FLIR_IOMUX_PAD_PWM_BUZZER);
	}

	BspGetSubjBackLightLevel(&pInfo->Keypad_bl_low,
				 &pInfo->Keypad_bl_medium,
				 &pInfo->Keypad_bl_high);

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
	SetI2CIoport(pInfo, LASER_SWITCH_ON, LaserStatus);
}

// Laser button has been pressed/released.
// In software controlled laser, we must enable/disable laser here.
void updateLaserOutput(PFAD_HW_INDEP_INFO pInfo)
{
	if (pInfo->bHasSoftwareControlledLaser) {
		FADDEVIOCTLLASER laserStatus = { 0 };
		getLaserStatus(pInfo, &laserStatus);

		if (laserStatus.bLaserIsOn && laserStatus.bLaserPowerEnabled) {
			SetI2CIoport(pInfo, LASER_SWITCH_ON, 1);
		} else {
			SetI2CIoport(pInfo, LASER_SWITCH_ON, 0);
		}
	}
}

void getLaserStatus(PFAD_HW_INDEP_INFO pInfo, PFADDEVIOCTLLASER pLaserStatus)
{
	pLaserStatus->bLaserIsOn = (gpio_get_value(LASER_ON) == 0);
	pLaserStatus->bLaserPowerEnabled = GetI2CIoport(pInfo, LASER_SWITCH_ON);
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
	SetI2CIoport(pInfo, LASER_SOFT_ON, bEnable);
}

BOOL GetLaserActive(PFAD_HW_INDEP_INFO pInfo)
{
	return GetI2CIoport(pInfo, LASER_SOFT_ON);
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
	FADDEVIOCTLBACKLIGHT bl;

	switch (pBacklight->subjectiveBacklight) {
	case KP_SUBJ_LOW:
		bl.backlight = pInfo->Keypad_bl_low;
		break;
	case KP_SUBJ_MEDIUM:
		bl.backlight = pInfo->Keypad_bl_medium;
		break;
	case KP_SUBJ_HIGH:
		bl.backlight = pInfo->Keypad_bl_high;
		break;
	default:
		return ERROR_INVALID_DATA;
	}

	return SetKeypadBacklight(&bl);
}

UINT8 KeypadLowMediumLimit(PFAD_HW_INDEP_INFO pInfo)
{
	return ((pInfo->Keypad_bl_low + pInfo->Keypad_bl_medium) / 2);
}

UINT8 KeypadMediumHighLimit(PFAD_HW_INDEP_INFO pInfo)
{
	return ((pInfo->Keypad_bl_medium + pInfo->Keypad_bl_high) / 2);
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
	FADDEVIOCTLBACKLIGHT backlight = { 0 };
	UINT8 bl_value;

	GetKeypadBacklight(&backlight);
	bl_value = backlight.backlight;

	if (bl_value <= KeypadLowMediumLimit(pInfo))
		pBacklight->subjectiveBacklight = KP_SUBJ_LOW;
	else if (bl_value <= KeypadMediumHighLimit(pInfo))
		pBacklight->subjectiveBacklight = KP_SUBJ_MEDIUM;
	else
		pBacklight->subjectiveBacklight = KP_SUBJ_HIGH;

	// RETAILMSG(1, (_T("GetKeypadSubjBacklight %x\r\n"),pBacklight->subjectiveBacklight));
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
