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

#include "../fvd/flir_kernel_os.h"
#include "faddev.h"
#include "i2cdev.h"
#include "fad_internal.h"
#include <linux/i2c.h>
#include <linux/errno.h>

#define DEBUG_CLOCK_OUT 0   // Set to 1 to enable clock outputs on CLKO and CLKO2 (for debugging)

// Definitions

#define IOPORT_I2C_ADDR     0x46
#define HDMI_I2C_ADDR		0x72

#define VCM_LED_EN          0
#define LASER_SWITCH_ON     2
#define FOCUS_POWER_EN      3
#define LASER_SOFT_ON       5

typedef struct
{
     volatile unsigned long PWMCR;
     volatile unsigned long PWMSR;
     volatile unsigned long PWMIR;
     volatile unsigned long PWMSAR;
     volatile unsigned long PWMPR;
     volatile unsigned long PWMCNR;
} CSP_PWM_REG, *PCSP_PWM_REG;

// Local variables

// Function prototypes

void BspEnablePower(BOOL bEnable);

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

static BOOL InitI2CIoport (PFAD_HW_INDEP_INFO pInfo)
{
	struct i2c_msg msgs[2];
    int res;
    UCHAR buf[2];
    UCHAR cmd;
    struct i2c_adapter *adap;

    LOCK_IOPORT(pInfo);

    adap = i2c_get_adapter(0);

    msgs[0].addr = IOPORT_I2C_ADDR >> 1;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &cmd;
    msgs[1].addr = IOPORT_I2C_ADDR >> 1;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = buf;

    cmd = 3;    // Read config register
	res = i2c_transfer(pInfo->hI2C1, msgs, 2);

    if (res > 0)
    {
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

    i2c_put_adapter(adap);

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

BOOL SetI2CIoport (PFAD_HW_INDEP_INFO pInfo, UCHAR bit, BOOL value)
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

    cmd = 1;    // Read output register
	res = i2c_transfer(pInfo->hI2C1, msgs, 2);

    if (res > 0)
    {
    	msgs[0].len = 2;
    	msgs[0].buf = buf;
        buf[1] = buf[0];  // Initial value before changes
        if (value)
            buf[1] |= (1 << bit);
        else
            buf[1] &= ~(1 << bit);
//        pr_err("FAD: IO Set %02X -> %02X\n", buf[0], buf[1]);
        buf[0] = 1;       // Set output value

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

static BOOL GetI2CIoport (PFAD_HW_INDEP_INFO pInfo, UCHAR bit)
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

    cmd = 0;    // Read port register
    buf[0] = 0;
	res = i2c_transfer(pInfo->hI2C1, msgs, 2);

    UNLOCK_IOPORT(pInfo);

    return ((buf[0] & (1 << bit)) != 0);
}

void initHW(PFAD_HW_INDEP_INFO pInfo)
{
    SetI2CIoport(pInfo, VCM_LED_EN, TRUE);
    SetI2CIoport(pInfo, LASER_SWITCH_ON, FALSE);
    SetI2CIoport(pInfo, FOCUS_POWER_EN, TRUE);
    SetI2CIoport(pInfo, LASER_SOFT_ON, FALSE);
    InitI2CIoport(pInfo);

    // Laser ON
    if (BspHasLaser())
	{
    	if (gpio_is_valid(LASER_ON) == 0)
    	    pr_err("LaserON can not be used\n");
    	gpio_request(LASER_ON, "LaserON");
    	gpio_direction_input(LASER_ON);
    }

	if (BspHas5VEnable())
	{
    	if (gpio_is_valid(PIN_3V6A_EN) == 0)
    	    pr_err("3V6A_EN can not be used\n");
    	gpio_request(PIN_3V6A_EN, "3V6AEN");
    	gpio_direction_output(PIN_3V6A_EN, 1);
	}

    if (BspHasHdmi())
	{
	    UCHAR val;

    	if (gpio_is_valid(I2C2_HDMI_INT) == 0)
    	    pr_err("I2C2_HDMI_INT can not be used\n");
    	gpio_request(I2C2_HDMI_INT, "HdmiInt");
    	gpio_direction_input(I2C2_HDMI_INT);

		readHdmiPhy(pInfo, HDMI_REG_STATE, &val); // Check HPD state
		if (val & HDMI_STATE_HPD) {
			// Hot plug detected
			initHdmi(pInfo);
		}
	}

    if (BspHasBuzzer())
	{
//        DDKIomuxSetPinMux(FLIR_IOMUX_PIN_PWM_BUZZER);
//        DDKIomuxSetPadConfig(FLIR_IOMUX_PAD_PWM_BUZZER);
    }

    BspGetSubjBackLightLevel( &pInfo->Keypad_bl_low,
                              &pInfo->Keypad_bl_medium,
                              &pInfo->Keypad_bl_high);

}


void HandleHdmiInterrupt(struct work_struct *hdmiWork)
{
	UCHAR		val;
	static BOOL bLastConnected;
	BOOL        bNowConnected;

	PFAD_HW_INDEP_INFO pInfo = container_of(hdmiWork, FAD_HW_INDEP_INFO, hdmiWork);

    pr_err("HandleHdmiInterrupt\n");

	// Clear interrupt register
	readHdmiPhy(pInfo, HDMI_REG_IRQ, &val);
	writeHdmiPhy(pInfo, HDMI_REG_IRQ, val);

	// Check if connected
	readHdmiPhy(pInfo, HDMI_REG_STATE, &val);
	bNowConnected = (val & HDMI_STATE_HPD) != 0;
	if (bNowConnected != bLastConnected)
	{
		if (bNowConnected)
		{
			pr_err("FAD: HDMI Hot Plug Detected\n");
			initHdmi(pInfo);
		}
		bLastConnected = bNowConnected;
	    ApplicationEvent(pInfo);
	}

    pr_err("HDMI state = %d\n", bNowConnected);
}


DWORD setKAKALedState(FADDEVIOCTLLED* pLED)
{
	return ERROR_SUCCESS;
}

DWORD getKAKALedState(FADDEVIOCTLLED* pLED)
{
	return ERROR_SUCCESS;
}


void getDigitalStatus(PFADDEVIOCTLDIGIO pDigioStatus)
{
}

void resetPT1000(void)
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
    if(BspSoftwareControlledLaser())
    {    
        FADDEVIOCTLLASER laserStatus = {0};
        getLaserStatus(pInfo, &laserStatus);

        if(laserStatus.bLaserIsOn && laserStatus.bLaserPowerEnabled)
        {
            SetI2CIoport(pInfo, LASER_SWITCH_ON, 1);
        }
        else
        {
            SetI2CIoport(pInfo, LASER_SWITCH_ON, 0);
        }
    }
}

void getLaserStatus(PFAD_HW_INDEP_INFO pInfo, PFADDEVIOCTLLASER pLaserStatus)
{
    pLaserStatus->bLaserIsOn = (gpio_get_value(LASER_ON) == 0);
    pLaserStatus->bLaserPowerEnabled = GetI2CIoport(pInfo, LASER_SWITCH_ON);
}

void getLCDStatus(PFADDEVIOCTLLCD pLCDStatus)
{
}

DWORD readHdmiPhy(PFAD_HW_INDEP_INFO pInfo, UCHAR reg, UCHAR* pValue)
{
	struct i2c_msg msgs[2];
    int res;
    UCHAR buf;
    UCHAR cmd;

	*pValue = 0;

    msgs[0].addr = HDMI_I2C_ADDR >> 1;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &cmd;
    msgs[1].addr = HDMI_I2C_ADDR >> 1;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = &buf;

	cmd = reg;
    buf = 0;

    res = i2c_transfer(pInfo->hI2C2, msgs, 2);

	if (res > 0)
	{
		*pValue = buf;
	}

	return ((res > 0) ? ERROR_SUCCESS : 1);
}

DWORD writeHdmiPhy(PFAD_HW_INDEP_INFO pInfo, UCHAR reg, UCHAR value)
{
	struct i2c_msg msgs[1];
    int res;
    UCHAR buf[2];

    msgs[0].addr = HDMI_I2C_ADDR >> 1;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = buf;

    buf[0] = reg;
    buf[1] = value;

    res = i2c_transfer(pInfo->hI2C2, msgs, 1);

	pr_err("writeHdmiPhy res=%d\n", res);

	return res;
}

void getHdmiStatus(PFAD_HW_INDEP_INFO pInfo, PFADDEVIOCTLHDMI pHdmiStatus)
{
    UCHAR val;

	pHdmiStatus->bHdmiPresent = FALSE; // Default

	if (ERROR_SUCCESS == readHdmiPhy(pInfo, HDMI_REG_STATE, &val))
	{
		pHdmiStatus->bHdmiPresent = val & HDMI_STATE_HPD ? TRUE : FALSE;
	}
}

void initHdmi(PFAD_HW_INDEP_INFO pInfo)
{
    UCHAR val;
    UCHAR tmo = 40;

    pr_err("initHdmi\n");

	writeHdmiPhy(pInfo, HDMI_REG_TMDS_TRANSC, 0x3C); // Turn of TMDS trannsceivers
	writeHdmiPhy(pInfo, HDMI_REG_POWER, 0x10); // Power up
	writeHdmiPhy(pInfo, 0x98, 0x03); // Required fixed value
	writeHdmiPhy(pInfo, 0x9C, 0x38); // Required fixed value
	writeHdmiPhy(pInfo, 0x9D, 0x61); // Required fixed value
	writeHdmiPhy(pInfo, 0xA2, 0x94); // Required fixed value
	writeHdmiPhy(pInfo, 0xA3, 0x94); // Required fixed value
	writeHdmiPhy(pInfo, HDMI_REG_CLK_DELAY, 0x70);
	writeHdmiPhy(pInfo, 0xDE, 0x88); // Required fixed value
	writeHdmiPhy(pInfo, HDMI_REG_INPUT_FMT, 0x0A);  // 12-bit RGB 4:4:4 with separate sync
	writeHdmiPhy(pInfo, HDMI_REG_INPUT_TYPE, 0x02); // Input style 1, Rising edge, RGB color space
	writeHdmiPhy(pInfo, HDMI_REG_INPUT_SELECT, 0x00);
	writeHdmiPhy(pInfo, HDMI_REG_DDR, 0x3C);
	writeHdmiPhy(pInfo, HDMI_REG_TMDS_CODING, 0x10); // Soft TMDS clock turn on A
	writeHdmiPhy(pInfo, HDMI_REG_MUX, 0x08); // Soft TMDS clock turn on B
	writeHdmiPhy(pInfo, HDMI_REG_MODE, 0x14);	// DVI mode default, appcore switches to HDMI if that is supported by sink

    // Wait for EDID to be read by PHY
	while (ERROR_SUCCESS == readHdmiPhy(pInfo, HDMI_REG_IRQ, &val) && tmo--)
    {
        if (val & HDMI_EDID_READ)
            break;
        msleep(25);
    }
}

void setCoolerState(PFADDEVIOCTLCOOLER pCoolerState)
{
}

void getCoolerState(PFADDEVIOCTLCOOLER pCoolerState)
{
}

void setHdmiI2cState (DWORD state)
{
}

// Enter programming state in temp sensor ADUC
DWORD resetTempCPUforProgramming(void)
{
	return ERROR_SUCCESS;
}

// Reset temp sensor ADUC
DWORD resetTempCPUwoProg(void)
{
	return ERROR_SUCCESS;
}

void trigPositionSync(void)
{
}


BOOL setGPSEnable(BOOL enabled)
{
    return TRUE;
}

BOOL getGPSEnable(BOOL *enabled)
{
    // GPS does not seem to receive correct signals when switching 
    // on and off, I2C problems? Temporary fallback solution is to 
    // Keep GPS switched on all the time.
    *enabled = TRUE;
    return TRUE;
}

BOOL initGPSControl(PFAD_HW_INDEP_INFO pInfo)
{
	return TRUE;
}

void WdogInit(PFAD_HW_INDEP_INFO pInfo, UINT32 Timeout)
{
#ifdef NOT_YET
    PCSP_WDOG_REGS pWdog;
    UINT16 wcr;

    if (pInfo->pWdog == NULL) {
        PHYSICAL_ADDRESS ioPhysicalBase = {CSP_BASE_REG_PA_WDOG1, 0};
        pInfo->pWdog = MmMapIoSpace(ioPhysicalBase, sizeof(PCSP_WDOG_REGS), FALSE);
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
            CSP_BITFVAL(WDOG_WCR_WT, (UINT16)(Timeout/500) & WDOG_WCR_WT_MASK);    

    // Configure and then enable the watchdog
    OUTREG16(&pWdog->WCR, wcr);
    wcr |= CSP_BITFVAL(WDOG_WCR_WDE, WDOG_WCR_WDE_ENABLE);
    OUTREG16(&pWdog->WCR,  wcr);

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

void StopCoolerByI2C(PFAD_HW_INDEP_INFO pInfo, BOOL bStop)
{
}

void SetLaserActive(PFAD_HW_INDEP_INFO pInfo, BOOL bEnable)
{
    SetI2CIoport(pInfo, LASER_SOFT_ON, bEnable);
}

BOOL GetLaserActive(PFAD_HW_INDEP_INFO pInfo)
{
    return GetI2CIoport(pInfo, LASER_SOFT_ON);
}

void SetHDMIEnable(BOOL bEnable)
{
}

void Set5VEnable(BOOL bEnable)
{
	gpio_set_value(PIN_3V6A_EN, bEnable);
}

BOOL InitModeWheel(void)
{
    return FALSE;
}

DWORD GetModeWheelPosition(void)
{
    return 0;
}

void SetBuzzerFrequency(USHORT usFreq, UCHAR ucPWM)
{
#ifdef NOT_YET
    static PCSP_PWM_REG pPWM;
    int period;
    int sample;

    if (pPWM == NULL)
    {
        PHYSICAL_ADDRESS phyAddr;

        DDKClockSetGatingMode(DDK_CLOCK_GATE_INDEX_PWM2_IPG, DDK_CLOCK_GATE_MODE_ENABLED_ALL);

        phyAddr.QuadPart = CSP_BASE_REG_PA_PWM2;
        pPWM = (PCSP_PWM_REG) MmMapIoSpace(phyAddr, sizeof(CSP_PWM_REG), FALSE);

        pPWM->PWMCR = 0x00C10416;   // 1 MHz, enabled during debug
    }

    if (usFreq && ucPWM)
    {
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
    }
    else
    {
        pPWM->PWMCR &= ~1;
    }
#endif
}

DWORD SetKeypadSubjBacklight(PFAD_HW_INDEP_INFO pInfo, PFADDEVIOCTLSUBJBACKLIGHT pBacklight)
{
    FADDEVIOCTLBACKLIGHT bl;

    switch(pBacklight->subjectiveBacklight)
    {
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
    return ((pInfo->Keypad_bl_low + pInfo->Keypad_bl_medium)/2);
}

UINT8 KeypadMediumHighLimit(PFAD_HW_INDEP_INFO pInfo)
{
    return ((pInfo->Keypad_bl_medium + pInfo->Keypad_bl_high)/2);
}
//------------------------------------------------------------------------------
//
//  Function:  GetKeypadSubjBacklight
//
//  This method returns the keypad backlight in subjective levels.
//	Since this function works in parallel with the older functions setting
//	percentage values, it copes with values that differs from the defined 
//  subjective levels.
DWORD GetKeypadSubjBacklight(PFAD_HW_INDEP_INFO pInfo, PFADDEVIOCTLSUBJBACKLIGHT pBacklight)
{
    FADDEVIOCTLBACKLIGHT backlight;
    UINT8 bl_value;

    GetKeypadBacklight(&backlight);
    bl_value = backlight.backlight;

	if( bl_value <= KeypadLowMediumLimit(pInfo) )
		pBacklight->subjectiveBacklight = KP_SUBJ_LOW;
	else if( bl_value <= KeypadMediumHighLimit(pInfo) )
		pBacklight->subjectiveBacklight = KP_SUBJ_MEDIUM;
	else 
		pBacklight->subjectiveBacklight = KP_SUBJ_HIGH;

    // RETAILMSG(1, (_T("GetKeypadSubjBacklight %x\r\n"),pBacklight->subjectiveBacklight));
    return ERROR_SUCCESS;
}


