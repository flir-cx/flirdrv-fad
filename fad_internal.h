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

enum POWER_STATE{
	UNKNOWN_STATE,
	SUSPEND_STATE,
	ON_STATE,
	USB_CHARGE_STATE
};

enum WAKE_REASON{
	UNKNOWN_WAKE,
	ON_OFF_BUTTON_WAKE,
	USB_CABLE_WAKE
};

extern DWORD g_RestartReason;

struct alarm;

// Generic GPIO definitions
#define LASER_ON			((7-1)*32 + 7)
#define PIN_3V6A_EN			((3-1)*32 + 30)
#define DIGIN_1             ((4-1)*32 + 15)
#define DIGOUT_1            ((4-1)*32 + 14)

// Internal variable
typedef struct __FAD_HW_INDEP_INFO {
	struct platform_device *pLinuxDevice;
	struct notifier_block nb;
	struct semaphore semDevice;	// serialize access to this device's state
	struct semaphore semIOport;
	struct i2c_adapter *hI2C1;
	struct i2c_adapter *hI2C2;
	struct completion standbyComplete;
	BOOL bLaserEnable;	// True when laser enable active
	PVOID pWdog;		// Pointer to Watchdog CPU registers
	UINT8 Keypad_bl_low;
	UINT8 Keypad_bl_medium;
	UINT8 Keypad_bl_high;
	struct led_classdev *red_led_cdev;
	struct led_classdev *blue_led_cdev;
	struct alarm *alarm;

	// Wait for IRQ variables
	FAD_EVENT_E eEvent;
	wait_queue_head_t wq;

#ifdef CONFIG_OF
	struct device_node *node;
	int laser_on_gpio;
	int laser_soft_gpio;
	int laser_switch_gpio;
	int digin0_gpio;
	int digin1_gpio;
	int trigger_gpio;

	struct regulator *reg_opt3v6;
	struct regulator *reg_optics_power;
	struct regulator *reg_position_sensor;
	struct regulator *reg_ring_sensor;
	struct regulator *reg_motor_sleep;

	struct led_classdev *pijk_cdev;
	struct led_classdev *pike_cdev;

	int laserMode;
	int ldmAccuracy;
	int ldmContinous;
	int standbyMinutes;

	struct backlight_device *backlight;
#endif
	BOOL bHasLaser;
	BOOL bHasGPS;
	BOOL bHas7173;
	BOOL bHas5VEnable;
	BOOL bHasDigitalIO;
	BOOL bHasKAKALed;
	BOOL bHasBuzzer;
	BOOL bHasKpBacklight;
	BOOL bHasSoftwareControlledLaser;
	BOOL bHasTrigger;
	BOOL bSuspend;

    DWORD(*pGetLedState) (struct __FAD_HW_INDEP_INFO * gpDev,
                              FADDEVIOCTLLED * pLED);
    DWORD(*pSetLedState) (struct __FAD_HW_INDEP_INFO * gpDev,
                              FADDEVIOCTLLED * pLED);
    DWORD(*pGetKAKALedState) (struct __FAD_HW_INDEP_INFO * gpDev,
                              FADDEVIOCTLLED * pLED);
    DWORD(*pSetKAKALedState) (struct __FAD_HW_INDEP_INFO * gpDev,
                              FADDEVIOCTLLED * pLED);
	void (*pGetDigitalStatus) (struct __FAD_HW_INDEP_INFO *, PFADDEVIOCTLDIGIO pDigioStatus);
	void (*pSetLaserStatus) (struct __FAD_HW_INDEP_INFO *,
                             BOOL on);
	void (*pGetLaserStatus) (struct __FAD_HW_INDEP_INFO *,
                             PFADDEVIOCTLLASER pLaserStatus);
	void (*pUpdateLaserOutput) (struct __FAD_HW_INDEP_INFO * gpDev);
	void (*pSetBuzzerFrequency) (USHORT usFreq, UCHAR ucPWM);
	void (*pSetLaserActive) (struct __FAD_HW_INDEP_INFO * gpDev,
                             BOOL bEnable);
	BOOL(*pGetLaserActive) (struct __FAD_HW_INDEP_INFO * gpDev);
    void (*pSetLaserMode) (struct __FAD_HW_INDEP_INFO * gpDev, PFADDEVIOCTLLASERMODE pLaserMode);
	DWORD(*pGetKeypadBacklight) (PFADDEVIOCTLBACKLIGHT pBacklight);
	DWORD(*pSetKeypadBacklight) (PFADDEVIOCTLBACKLIGHT pBacklight);
	DWORD(*pGetKeypadSubjBacklight) (struct __FAD_HW_INDEP_INFO * gpDev,
                                     PFADDEVIOCTLSUBJBACKLIGHT pBacklight);
    DWORD(*pSetKeypadSubjBacklight) (struct __FAD_HW_INDEP_INFO * gpDev,
                                     PFADDEVIOCTLSUBJBACKLIGHT pBacklight);
    BOOL(*pSetGPSEnable) (BOOL enabled);
    BOOL(*pGetGPSEnable) (BOOL * enabled);
	int (*pSetChargerSuspend)(struct __FAD_HW_INDEP_INFO *gpDev, BOOL suspend);
	void (*pWdogInit) (struct __FAD_HW_INDEP_INFO * gpDev, UINT32 Timeout);
	 BOOL(*pWdogService) (struct __FAD_HW_INDEP_INFO * gpDev);
	void (*pCleanupHW) (struct __FAD_HW_INDEP_INFO * gpDev);
	int (*suspend) (struct __FAD_HW_INDEP_INFO * gpDev);
	int (*resume) (struct __FAD_HW_INDEP_INFO * gpDev);
} FAD_HW_INDEP_INFO, *PFAD_HW_INDEP_INFO;

// Driver serialization macros
#define	LOCK(pd)			down(&pd->semDevice)
#define	UNLOCK(pd)			up(&pd->semDevice)

#define	LOCK_IOPORT(pd)		down(&pd->semIOport)
#define	UNLOCK_IOPORT(pd)	up(&pd->semIOport)

// Function prototypes - fad_irq.c (Input pin interrupt handling)
int InitLaserIrq(PFAD_HW_INDEP_INFO gpDev);
void FreeLaserIrq(PFAD_HW_INDEP_INFO gpDev);
irqreturn_t fadTriggerIST(int irq, void *dev_id);
void ApplicationEvent(PFAD_HW_INDEP_INFO gpDev, FAD_EVENT_E event);

// Function prototypes - fad_io.c (Misc IO handling, both I2C and GPIO)
int SetupMX51(PFAD_HW_INDEP_INFO gpDev);
int SetupMX6S(PFAD_HW_INDEP_INFO gpDev);
int SetupMX6Q(PFAD_HW_INDEP_INFO gpDev);
int SetupMX6Platform(PFAD_HW_INDEP_INFO gpDev);

void InvSetupMX51(PFAD_HW_INDEP_INFO gpDev);
void InvSetupMX6S(PFAD_HW_INDEP_INFO gpDev);
void InvSetupMX6Q(PFAD_HW_INDEP_INFO gpDev);
void InvSetupMX6Platform(PFAD_HW_INDEP_INFO gpDev);

#endif
