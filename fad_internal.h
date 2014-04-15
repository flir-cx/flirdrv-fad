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
    struct class			*fad_class;
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

    BOOL 		bHasLaser;
    BOOL 		bHasHdmi;
    BOOL 		bHasGPS;
    BOOL 		bHas7173;
    BOOL 		bHas5VEnable;
    BOOL 		bHasDigitalIO;
    BOOL 		bHasKAKALed;
    BOOL 		bHasBuzzer;
    BOOL 		bHasKpBacklight;
    BOOL 		bHasSoftwareControlledLaser;

    DWORD (*pGetKAKALedState) (FADDEVIOCTLLED* pLED);
    DWORD (*pSetKAKALedState) (FADDEVIOCTLLED* pLED);
    void (*pGetDigitalStatus) (PFADDEVIOCTLDIGIO pDigioStatus);
    void (*pSetLaserStatus) (struct __FAD_HW_INDEP_INFO *, BOOL LaserStatus);
    void (*pGetLaserStatus) (struct __FAD_HW_INDEP_INFO *, PFADDEVIOCTLLASER pLaserStatus);
    void (*pUpdateLaserOutput) (struct __FAD_HW_INDEP_INFO * pInfo);
    void (*pGetHdmiStatus) (struct __FAD_HW_INDEP_INFO * pInfo, PFADDEVIOCTLHDMI pHdmiStatus);
    void (*pSetBuzzerFrequency) (USHORT usFreq, UCHAR ucPWM);
    void (*pSetHdmiI2cState) (DWORD state);
    void (*pSetLaserActive) (struct __FAD_HW_INDEP_INFO * pInfo, BOOL bEnable);
    BOOL (*pGetLaserActive) (struct __FAD_HW_INDEP_INFO * pInfo);
    DWORD (*pGetKeypadBacklight) (PFADDEVIOCTLBACKLIGHT pBacklight);
    DWORD (*pSetKeypadBacklight) (PFADDEVIOCTLBACKLIGHT pBacklight);
    DWORD (*pGetKeypadSubjBacklight) (struct __FAD_HW_INDEP_INFO * pInfo, PFADDEVIOCTLSUBJBACKLIGHT pBacklight);
    DWORD (*pSetKeypadSubjBacklight) (struct __FAD_HW_INDEP_INFO * pInfo, PFADDEVIOCTLSUBJBACKLIGHT pBacklight);
    BOOL (*pSetGPSEnable) (BOOL enabled);
    BOOL (*pGetGPSEnable) (BOOL *enabled);
    void (*pWdogInit) (struct __FAD_HW_INDEP_INFO * pInfo, UINT32 Timeout);
    BOOL (*pWdogService) (struct __FAD_HW_INDEP_INFO * pInfo);
    void (*pCleanupHW) (struct __FAD_HW_INDEP_INFO * pInfo);
    void (*pHandleHdmiInterrupt) (struct work_struct *hdmiWork);

} FAD_HW_INDEP_INFO, *PFAD_HW_INDEP_INFO;

// Driver serialization macros
#define	LOCK(pd)			down(&pd->semDevice)
#define	UNLOCK(pd)			up(&pd->semDevice)

#define	LOCK_IOPORT(pd)		down(&pd->semIOport)
#define	UNLOCK_IOPORT(pd)	up(&pd->semIOport)

// Function prototypes - fad_irq.c (Input pin interrupt handling)
BOOL InitLaserIrq(PFAD_HW_INDEP_INFO pInfo);
BOOL InitHdmiIrq(PFAD_HW_INDEP_INFO pInfo);
BOOL InitDigitalIOIrq(PFAD_HW_INDEP_INFO pInfo);
DWORD ApplicationEvent(PFAD_HW_INDEP_INFO pInfo);

// Function prototypes - fad_io.c (Misc IO handling, both I2C and GPIO)
void SetupMX51(PFAD_HW_INDEP_INFO pInfo);
void SetupMX6S(PFAD_HW_INDEP_INFO pInfo);

#endif
