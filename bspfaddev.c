/***********************************************************************
*                                                                     
* Project: Balthazar
* $Date$
* $Author$
*
* $Id$
*
* Description of file:
*    FLIR Application launcher. This simple util is used to launch programs
*    as part of the [HKEY_LOCAL_MACHINE\init] launch procedure. The files 
*    to launch are defined in an external launch description file. 
*    The launch description file name is read from the registry at startup.
*    For regitstry settings example, see applauncher.reg    
*
* Last check-in changelist:
* $Change$
* 
*
*  FADDEV Copyright : FLIR Systems AB
***********************************************************************/

#include "../fvd/flir_kernel_os.h"
#include "faddev.h"
#include "i2cdev.h"
#include "fad_internal.h"

// Debug definitions and variables

#define VCM_LED_EN          0
#define FOCUS_POWER_EN      3

// Global function prototypes

extern BOOL SetI2CIoport (UCHAR bit, BOOL value);

// Local function prototypes

// Code

BOOL BspHasKAKALed(void)
{
    return FALSE;
}

BOOL BspHasLaser(void)
{
    return TRUE;
}

BOOL BspHasTorch(void)
{
    return TRUE;
}

BOOL BspHasDigitalIO(void)
{
    return FALSE;
}

BOOL BspHas5VEnable(void)
{
    return TRUE;
}

BOOL BspHasLCDActive(void)
{
    return FALSE;
}

BOOL BspHasHdmi(void)
{
    return TRUE;
}

BOOL BspHasGPS(void)
{
    return TRUE;
}

BOOL BspHas7173(void)
{
    return FALSE;
}

BOOL BspHasCooler(void)
{
    return FALSE;
}

BOOL BspHasModeWheel(void)
{
    return FALSE;
}

BOOL BspHasBuzzer(void)
{
    return TRUE;
}

BOOL BspHasIOPortLaser(void)
{
    return FALSE;
}

BOOL BspHasKpBacklight(void)
{
    return TRUE;
}

BOOL BspHasTruckCharger(void)
{
    return FALSE;
}

#ifdef NOT_YET
DWORD getLedState(PFAD_HW_INDEP_INFO pInfo, FADDEVIOCTLLED *pLedData)
{
    DWORD pinState;

    DDKGpioReadDataPin(FLIR_GPIO_PIN_POWER_ON_LED, &pinState);

    if (pInfo->dwLedFlash)
    {
        pLedData->eColor = LED_COLOR_GREEN;
        if (pInfo->dwLedFlash < 500)
            pLedData->eState = LED_FLASH_FAST;
        else
            pLedData->eState = LED_FLASH_SLOW;
    }
    else if (pinState)
    {
        pLedData->eColor = LED_COLOR_GREEN;
        pLedData->eState = LED_STATE_ON;
    }
    else
    {
        pLedData->eColor = LED_COLOR_OFF;
        pLedData->eState = LED_STATE_OFF;
    }
    return ERROR_SUCCESS;
}

DWORD setLedState(PFAD_HW_INDEP_INFO pInfo, FADDEVIOCTLLED *pLedData)
{
    DWORD dwErr = ERROR_SUCCESS;

    DDKIomuxSetPinMux(FLIR_IOMUX_PIN_POWER_ON_LED);
    DDKIomuxSetPadConfig(FLIR_IOMUX_PAD_POWER_ON_LED);
    DDKGpioSetConfig(FLIR_GPIO_PIN_POWER_ON_LED, DDK_GPIO_DIR_OUT, DDK_GPIO_INTR_NONE);

    if ((pLedData->eColor != LED_COLOR_OFF) &&
        (pLedData->eState == LED_STATE_ON))
    {
        DDKGpioWriteDataPin(FLIR_GPIO_PIN_POWER_ON_LED, 1);
        pInfo->dwLedFlash = 0;
    }
    else if ((pLedData->eColor == LED_COLOR_OFF) ||
        (pLedData->eState == LED_STATE_OFF))
    {
        DDKGpioWriteDataPin(FLIR_GPIO_PIN_POWER_ON_LED, 0);
        pInfo->dwLedFlash = 0;
    }
    else if (pLedData->eState == LED_FLASH_SLOW)
    {
        pInfo->dwLedFlash = 1000;
        SetEvent(pInfo->hLedFlashEvent);
    }
    else if (pLedData->eState == LED_FLASH_FAST)
    {
        pInfo->dwLedFlash = 100;
        SetEvent(pInfo->hLedFlashEvent);
    }
    else
    {
        dwErr = ERROR_INVALID_PARAMETER;
        pInfo->dwLedFlash = 0;
    }
    return dwErr;
}

DWORD WINAPI fadFlashLed(PVOID pContext)
{
    PFAD_HW_INDEP_INFO	pInfo = (PFAD_HW_INDEP_INFO)pContext;
    ULONG timeout;
    BOOL state = FALSE;

    CeSetThreadPriority(GetCurrentThread(), 246);

    while ( TRUE ) 
	{
		// Wait for ISR interrupt notification
        if (pInfo->dwLedFlash == 0)
            timeout = INFINITE;
        else
            timeout = pInfo->dwLedFlash / 2;
        WaitForSingleObject(pInfo->hLedFlashEvent, timeout);

        if (pInfo->dwLedFlash)
        {
            state = !state;
            DDKGpioWriteDataPin(FLIR_GPIO_PIN_POWER_ON_LED, state);
        }
    }
    return(0);
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
    DDKGpioSetConfig(FLIR_GPIO_PIN_POWER_ON_LED, DDK_GPIO_DIR_OUT, DDK_GPIO_INTR_NONE);
    DDKGpioWriteDataPin(FLIR_GPIO_PIN_POWER_ON_LED, (down == FALSE));

    // Keypad backlight handled here as it must be used on ioctl level to use CSPI
    if (down)
    {
        // Keypad backlight
        GetKeypadBacklight(&backlight);
        PmicBacklightSetCurrentLevel(BACKLIGHT_KEYPAD, 0);
        PmicBacklightSetCurrentLevel(BACKLIGHT_AUX_DISPLAY, 0);

        // PIRI I2C expander
        SetI2CIoport(VCM_LED_EN, FALSE);
        SetI2CIoport(FOCUS_POWER_EN, FALSE);

        // 5V and 3V3 for USB PHY
        PmicRegisterWrite(MC13892_CHG_USB1_ADDR, 1, 0x409);
    }
    else
    {
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

// 
// Returns status of how the hardware handles the laser.
//
// FALSE: Hardware, controls if Laser should be enabled/disabled using button.
//        I.e. when button is pressed the hardware will enabled laser. Liston uses 
//        this approach.
//
// TRUE:  Software only. Here we only have 2 'GPIO'. One for detecting button
//        and one for activating the Laser.
//        Software is responsible for activation Laser, when button is pressed.
// 
BOOL BspSoftwareControlledLaser(void)
{
    return FALSE;
}

void BspGetSubjBackLightLevel(UINT8* pLow, UINT8* pMedium, UINT8* pHigh)
{
    *pLow = 10;
    *pMedium = 40;
    *pHigh = 75;
}
