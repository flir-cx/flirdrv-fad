/***********************************************************************
*                                                                     
* Project: Balthazar
* $Date$
* $Author$
*
* $Id$
*
* Description of file:
*    Common definitions for FLIR Application Driver (FAD)  
*
* Last check-in changelist:
* $Change$
* 
*
*  FADDEV Copyright : FLIR Systems AB
***********************************************************************/

#ifndef FAD_INTERNAL_H
#define FAD_INTERNAL_H

enum {
    RESTART_REASON_NOT_SET    = 0x00,
    RESTART_REASON_COLDSTART  = 0x01,
    RESTART_REASON_WATCH      = 0x02,
    };

extern DWORD g_RestartReason;

// Generic GPIO definitions
#define LASER_ON			((3-1)*32 + 31)
#define PIN_3V6A_EN			((3-1)*32 + 30)
#define I2C2_HDMI_INT		((4-1)*32 + 16)

// Register definitions for HDMI PHY ADV7521NK
#define HDMI_REG_POWER			0x41
#define HDMI_REG_STATE			0x42
#define HDMI_REG_IRQ			0x96
#define HDMI_REG_CLK_DELAY		0xBA
#define HDMI_REG_INPUT_FMT		0x15
#define HDMI_REG_INPUT_TYPE		0x16
#define HDMI_REG_INPUT_SELECT	0x17
#define HDMI_REG_DDR			0xD0
#define HDMI_REG_MUX			0xD6
#define HDMI_REG_MODE			0xAF
#define HDMI_REG_TMDS_CODING	0xD5
#define HDMI_REG_TMDS_TRANSC	0xA1
#define HDMI_STATE_HPD			0x40
#define HDMI_EDID_READ          0x04

// Internal variable
typedef struct __FAD_HW_INDEP_INFO {
	struct platform_device *pLinuxDevice;
    struct cdev 			fad_cdev;			// Linux character device
    dev_t 					fad_dev;			// Major.Minor device number
	struct semaphore        semDevice;			// serialize access to this device's state
    struct semaphore	    semIOport;
    struct i2c_adapter 		*hI2C1;
    struct i2c_adapter 		*hI2C2;
    BOOL                    bLaserEnable;       // True when laser enable active
    PVOID                   pWdog;              // Pointer to Watchdog CPU registers
    UINT8 					Keypad_bl_low;
    UINT8 					Keypad_bl_medium;
    UINT8 					Keypad_bl_high;
    struct work_struct 		hdmiWork;
    struct workqueue_struct *hdmiIrqQueue;
} FAD_HW_INDEP_INFO, *PFAD_HW_INDEP_INFO;

// Driver serialization macros
#define	LOCK(pd)			down(&pd->semDevice)
#define	UNLOCK(pd)			up(&pd->semDevice)

#define	LOCK_IOPORT(pd)		down(&pd->semIOport)
#define	UNLOCK_IOPORT(pd)	up(&pd->semIOport)

// Function prototypes - bspfaddev.c (BSP specific functions)
void BspGetIOPortLaserSetting(BYTE* pLaserSwitchOnIOAddr, BYTE* pLaserSwitchOn, 
                              BYTE* pLaserSoftOnIOAddr, BYTE* pLaserSoftOn,
                              BYTE* pLaserOnIOAddr, BYTE* LaserOn,
                              BYTE* pLaserI2cAddrKeyb, BYTE* pLaserSwPressed);

BOOL BspNeedIOExpanderInit(void);
void BspIOExpanderValues(USHORT* pAddr, USHORT* pInputs, USHORT* pOutputValues);
void BspGetSubjBackLightLevel(UINT8* pLow, UINT8* pMedium, UINT8* pHigh);

BOOL BspSoftwareControlledLaser(void);
BOOL BspHasLaser(void);
BOOL BspHasHdmi(void);
BOOL BspHasGPS(void);
BOOL BspHas7173(void);
BOOL BspHas5VEnable(void);
BOOL BspHasDigitalIO(void);
BOOL BspHasKAKALed(void);
BOOL BspHasBuzzer(void);
BOOL BspHasSimpleIOPortTorch(void);
BOOL BspHasFocusTrig(void);
BOOL BspHasPleora(void);
BOOL BspHasKpBacklight(void);

int BspGet7173I2cBus(void);
DWORD getLedState(PFAD_HW_INDEP_INFO pInfo, FADDEVIOCTLLED *pLedData);
DWORD setLedState(PFAD_HW_INDEP_INFO pInfo, FADDEVIOCTLLED *pLedData);
DWORD GetKeypadBacklight(PFADDEVIOCTLBACKLIGHT pBacklight);
DWORD SetKeypadBacklight(PFADDEVIOCTLBACKLIGHT pBacklight);
DWORD GetKeypadSubjBacklight(PFAD_HW_INDEP_INFO pInfo, PFADDEVIOCTLSUBJBACKLIGHT pBacklight);
DWORD SetKeypadSubjBacklight(PFAD_HW_INDEP_INFO pInfo, PFADDEVIOCTLSUBJBACKLIGHT pBacklight);

DWORD WINAPI fadFlashLed(PVOID pContext);
void BspFadPowerDown(BOOL down);

// Function prototypes - fad_irq.c (Input pin interrupt handling)
BOOL InitLaserIrq(PFAD_HW_INDEP_INFO pInfo);
BOOL InitHdmiIrq(PFAD_HW_INDEP_INFO pInfo);
BOOL InitDigitalIOIrq(PFAD_HW_INDEP_INFO pInfo);
DWORD ApplicationEvent(PFAD_HW_INDEP_INFO pInfo);

// Function prototypes - fad_io.c (Misc IO handling, both I2C and GPIO)
FADDEVIOCTLTCPOWERSTATES GetTcState(void);
void HandleHdmiInterrupt(struct work_struct *hdmiWork);
void initHW(PFAD_HW_INDEP_INFO pInfo);
void CleanupHW(PFAD_HW_INDEP_INFO pInfo);
void resetPT1000(void);
void getDigitalStatus(PFADDEVIOCTLDIGIO pDigioStatus);
void setLaserStatus(PFAD_HW_INDEP_INFO pInfo, BOOL LaserStatus);
void getLaserStatus(PFAD_HW_INDEP_INFO pInfo, PFADDEVIOCTLLASER pLaserStatus);
void updateLaserOutput(PFAD_HW_INDEP_INFO pInfo);
void getLCDStatus(PFADDEVIOCTLLCD pLCDStatus);
void getHdmiStatus(PFAD_HW_INDEP_INFO pInfo, PFADDEVIOCTLHDMI pHdmiStatus);
void trigPositionSync(void);
BOOL setGPSEnable(BOOL enabled);
BOOL getGPSEnable(BOOL *enabled);
BOOL initGPSControl(PFAD_HW_INDEP_INFO pInfo);
void WdogInit(PFAD_HW_INDEP_INFO pInfo, UINT32 Timeout);
BOOL WdogService(PFAD_HW_INDEP_INFO pInfo);
void setHdmiI2cState (DWORD state);
void SetLaserActive(PFAD_HW_INDEP_INFO pInfo, BOOL bEnable);
BOOL GetLaserActive(PFAD_HW_INDEP_INFO pInfo);
void SetHDMIEnable(BOOL bEnable);
void Set5VEnable(BOOL bEnable);
void SetBuzzerFrequency(USHORT usFreq, UCHAR ucPWM);
DWORD setL2cache(DWORD activate);
DWORD getCPUinfo(DWORD *pOut);
DWORD writeHdmiPhy(PFAD_HW_INDEP_INFO pInfo, UCHAR reg, UCHAR value);
DWORD readHdmiPhy(PFAD_HW_INDEP_INFO pInfo, UCHAR reg, UCHAR* pValue);
void initHdmi(PFAD_HW_INDEP_INFO pInfo);

#endif
