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
	RESTART_REASON_NOT_SET = 0x00,
	RESTART_REASON_COLDSTART = 0x01,
	RESTART_REASON_WATCH = 0x02,
};

extern DWORD g_RestartReason;

// Generic GPIO definitions
#define LASER_ON			((7-1)*32 + 7)
#define PIN_3V6A_EN			((3-1)*32 + 30)
#define DIGIN_1             ((4-1)*32 + 15)
#define DIGOUT_1            ((4-1)*32 + 14)

// Internal variable
typedef struct __FAD_HW_INDEP_INFO {
	struct platform_device *pLinuxDevice;
	struct cdev fad_cdev;	// Linux character device
	struct class *fad_class;
	dev_t fad_dev;		// Major.Minor device number
	struct semaphore semDevice;	// serialize access to this device's state
	struct semaphore semIOport;
	struct i2c_adapter *hI2C1;
	struct i2c_adapter *hI2C2;
	BOOL bLaserEnable;	// True when laser enable active
	PVOID pWdog;		// Pointer to Watchdog CPU registers
	UINT8 Keypad_bl_low;
	UINT8 Keypad_bl_medium;
	UINT8 Keypad_bl_high;
	struct led_classdev *red_led_cdev;
	struct led_classdev *blue_led_cdev;

	// Wait for IRQ variables
	FAD_EVENT_E eEvent;
	wait_queue_head_t wq;

	BOOL bHasLaser;
	BOOL bHasGPS;
	BOOL bHas7173;
	BOOL bHas5VEnable;
	BOOL bHasDigitalIO;
	BOOL bHasKAKALed;
	BOOL bHasBuzzer;
	BOOL bHasKpBacklight;
	BOOL bHasSoftwareControlledLaser;

	 DWORD(*pGetKAKALedState) (struct __FAD_HW_INDEP_INFO * pInfo,
				   FADDEVIOCTLLED * pLED);
	 DWORD(*pSetKAKALedState) (struct __FAD_HW_INDEP_INFO * pInfo,
				   FADDEVIOCTLLED * pLED);
	void (*pGetDigitalStatus) (PFADDEVIOCTLDIGIO pDigioStatus);
	void (*pSetLaserStatus) (struct __FAD_HW_INDEP_INFO *,
				 BOOL LaserStatus);
	void (*pGetLaserStatus) (struct __FAD_HW_INDEP_INFO *,
				 PFADDEVIOCTLLASER pLaserStatus);
	void (*pUpdateLaserOutput) (struct __FAD_HW_INDEP_INFO * pInfo);
	void (*pSetBuzzerFrequency) (USHORT usFreq, UCHAR ucPWM);
	void (*pSetLaserActive) (struct __FAD_HW_INDEP_INFO * pInfo,
				 BOOL bEnable);
	 BOOL(*pGetLaserActive) (struct __FAD_HW_INDEP_INFO * pInfo);
	 DWORD(*pGetKeypadBacklight) (PFADDEVIOCTLBACKLIGHT pBacklight);
	 DWORD(*pSetKeypadBacklight) (PFADDEVIOCTLBACKLIGHT pBacklight);
	 DWORD(*pGetKeypadSubjBacklight) (struct __FAD_HW_INDEP_INFO * pInfo,
					  PFADDEVIOCTLSUBJBACKLIGHT pBacklight);
	 DWORD(*pSetKeypadSubjBacklight) (struct __FAD_HW_INDEP_INFO * pInfo,
					  PFADDEVIOCTLSUBJBACKLIGHT pBacklight);
	 BOOL(*pSetGPSEnable) (BOOL enabled);
	 BOOL(*pGetGPSEnable) (BOOL * enabled);
	void (*pWdogInit) (struct __FAD_HW_INDEP_INFO * pInfo, UINT32 Timeout);
	 BOOL(*pWdogService) (struct __FAD_HW_INDEP_INFO * pInfo);
	void (*pCleanupHW) (struct __FAD_HW_INDEP_INFO * pInfo);

} FAD_HW_INDEP_INFO, *PFAD_HW_INDEP_INFO;

// Driver serialization macros
#define	LOCK(pd)			down(&pd->semDevice)
#define	UNLOCK(pd)			up(&pd->semDevice)

#define	LOCK_IOPORT(pd)		down(&pd->semIOport)
#define	UNLOCK_IOPORT(pd)	up(&pd->semIOport)

// Function prototypes - fad_irq.c (Input pin interrupt handling)
int InitLaserIrq(PFAD_HW_INDEP_INFO pInfo);
int InitDigitalIOIrq(PFAD_HW_INDEP_INFO pInfo);

// Function prototypes - fad_io.c (Misc IO handling, both I2C and GPIO)
int SetupMX51(PFAD_HW_INDEP_INFO pInfo);
int SetupMX6S(PFAD_HW_INDEP_INFO pInfo);
int SetupMX6Q(PFAD_HW_INDEP_INFO pInfo);

void InvSetupMX51(PFAD_HW_INDEP_INFO pInfo);
void InvSetupMX6S(PFAD_HW_INDEP_INFO pInfo);
void InvSetupMX6Q(PFAD_HW_INDEP_INFO pInfo);

#endif
