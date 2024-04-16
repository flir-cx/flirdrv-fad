#ifndef __FADDEV_H__
#define __FADDEV_H__

// Definitions

typedef struct _FADDEVIOCTLLASER {
	BOOL	bLaserPowerEnabled;
	BOOL	bLaserIsOn;
} FADDEVIOCTLLASER, *PFADDEVIOCTLLASER;

typedef struct _FADDEVIOCTLSHUTTER {
	BOOL	bShutterEnabled;
} FADDEVIOCTLSHUTTER, *PFADDEVIOCTLSHUTTER;

typedef struct _FADDEVIOCTLTORCH {
	BOOL	bTorchEnabled;
	BOOL	bFlashEnabled;
} FADDEVIOCTLTORCH, *PFADDEVIOCTLTORCH;

typedef struct _FADDEVIOCTLBUZZER {
	enum { BUZZER_ON, BUZZER_OFF, BUZZER_TIME } eState;
	UCHAR   ucPWM;      // Sound PWM in % (0-100, default 50)
	USHORT  usFreq;     // Sound frequency in Hz (200 - 12000)
	USHORT  usTime;     // Sound length in ms (if BUZZER_TIME)
} FADDEVIOCTLBUZZER, *PFADDEVIOCTLBUZZER;

typedef struct _FADDEVIOCTL7173MODE {
	BOOL    bPALmode;       // TRUE = PAL, FALSE = NTSC
	BOOL    bTESTmode;      // TRUE = Color bar, FALSE = normal image
	BOOL    bCableDetect;   // Only used in GET operations
} FADDEVIOCTL7173MODE, *PFADDEVIOCTL7173MODE;

typedef struct _FADDEVIOCTLLCD {
	BOOL	bLCDOut;        // True = LCD out (used), False = LCD in (not used)
} FADDEVIOCTLLCD, *PFADDEVIOCTLLCD;

typedef struct _FADDEVIOCTLCOMPASS {
	USHORT usAngle;        // 0-359 value
	enum { COMPASS_UNKNOWN,
		COMPASS_N,
		COMPASS_NE,
		COMPASS_E,
		COMPASS_SE,
		COMPASS_S,
		COMPASS_SW,
		COMPASS_W,
		COMPASS_NW} eDirection;
} FADDEVIOCTLCOMPASS, *PFADDEVIOCTLCOMPASS;

typedef struct _FADDEVIOCTLDIGIO {
	UCHAR   ucNumOfDigIn;   // Output: Number of valid bits (inputs)
	UCHAR   ucNumOfDigOut;  // Output: Number of valid bits (outputs)
	USHORT  usInputState;   // Output: State of each input (LSB = IN 0)
	USHORT  usOutputState;  // Output: State of each output
	USHORT  usOutputSet;    // Input:  Outputs to set high (SET only)
	USHORT  usOutputClear;  // Input:  Outputs to set low  (SET only)
	USHORT  usReserved;
	ULONG   ulReserved;
} FADDEVIOCTLDIGIO, *PFADDEVIOCTLDIGIO;

typedef struct _FADDEVIOCTLWDOG {
	BOOL	bEnable;        // True = Enable watchdog, False = disable (set time to 22 minutes)
	USHORT  usTimeMs;       // Time in ms from watchdog trig until watchdog reset (default 5000)
} FADDEVIOCTLWDOG, *PFADDEVIOCTLWDOG;

typedef struct _FADDEVIOCTLLED {
	enum { LED_COLOR_OFF,
		LED_COLOR_GREEN,
		LED_COLOR_RED,
		LED_COLOR_YELLOW
	} eColor;
	enum { LED_STATE_OFF,
		LED_STATE_ON,
		LED_FLASH_SLOW,  // 1 Hz
		LED_FLASH_FAST   // 3 Hz
	} eState;
} FADDEVIOCTLLED, *PFADDEVIOCTLLED;

typedef struct _FADDEVIOCTLGPS {
	BOOL	bGPSEnabled;
} FADDEVIOCTLGPS, *PFADDEVIOCTLGPS;

typedef struct _FADDEVIOCTLLASERACTIVE  {
	BOOL	bLaserActive;
} FADDEVIOCTLLASERACTIVE, *PFADDEVIOCTLLASERACTIVE;

typedef struct _FADDEVIOCTLHDMI {
	BOOL	bHdmiPresent;
} FADDEVIOCTLHDMI, *PFADDEVIOCTLHDMI;

typedef struct _FADDEVIOCTLCOOLER {
	BOOL	bFanOn;
	BOOL	bCoolerDisable;
} FADDEVIOCTLCOOLER, *PFADDEVIOCTLCOOLER;

typedef struct _FADDEVIOCTLBACKLIGHT {
	UINT8 backlight;
	// IN/OUT: Percentage value
	// 0 = Off
	// 100 = fully on
} FADDEVIOCTLBACKLIGHT, *PFADDEVIOCTLBACKLIGHT;

typedef	enum {
	KP_SUBJ_LOW,
	KP_SUBJ_MEDIUM,
	KP_SUBJ_HIGH,
	KP_SUBJ_OFF
} SUBJ_KEYPAD_BACKL_E;

typedef enum {
	FAD_NO_EVENT,
	FAD_RESET_EVENT,
	FAD_LASER_EVENT,
	FAD_DIGIN_EVENT
} FAD_EVENT_E;

typedef struct _FADDEVIOCTLSUBJBACKLIGHT {
	SUBJ_KEYPAD_BACKL_E	subjectiveBacklight;
} FADDEVIOCTLSUBJBACKLIGHT, *PFADDEVIOCTLSUBJBACKLIGHT;

#define INITIAL_VERSION 101
// version 100 uses BOOL on the CFC levels below...
typedef struct _FADDEVIOCTLSECURITY {
	ULONG       ulVersion;
	ULONG       ulRequiredConfigCFClevel;
	ULONG       ulRequire30HzCFClevel;
	ULONGLONG   ullUniqueID;
} FADDEVIOCTLSECURITY, *PFADDEVIOCTLSECURITY;

typedef struct _FADDEVIOCTLTRIGPRESSED {
	BOOL	bTrigPressed;
} FADDEVIOCTLTRIGPRESSED, *PFADDEVIOCTLTRIGPRESSED;

typedef enum {
	LASERMODE_POINTER,
	LASERMODE_DISTANCE
} FADDEVIOCTLLASERMODES;

typedef enum {
	LASERMODE_DISTANCE_NONE = 0,
	LASERMODE_DISTANCE_LOW_ACCURACY,
	LASERMODE_DISTANCE_HIGH_ACCURACY
} FADDEVIOCTLLASERMODEACCURACY;

typedef struct _FADDEVIOCTLLASERMODE {
	FADDEVIOCTLLASERMODES mode;
	BOOL continousMeasurment;
	FADDEVIOCTLLASERMODEACCURACY accuracy;
} FADDEVIOCTLLASERMODE, *PFADDEVIOCTLLASERMODE;

// The diffrent power-state we can when we use the Truck Mounted Charger in Fenix.
typedef enum { TC_HANDHELD, TC_PRODUCTION, TC_IN_TC_POWER, TC_IN_TC_NOPOWER} FADDEVIOCTLTCPOWERSTATES;

//Public defines:
#define FAD_FILE_MUTEX_NAME				TEXT("FAD_FILE_FLUSH_MUTEX")
#define FAD_FILE_MUTEX_TIMEOUT			500

#define FAD_REG_NAME_SOURCEPATH			TEXT("SourcePath")
#define FAD_REG_NAME_DESTPATH			TEXT("DestPath")

// IOCTL codes.
#define FAD_SERVICE						0x8000

// Quickie fix to avoid lots of new preproc defines
#if defined(_WIN32) && !(defined(UNDER_CE)) && !(defined(BTZCAMSIM))
#define BTZCAMSIM
#endif

#ifdef UNDER_CE
#define FAD_IOCTL_W(code, type)   CTL_CODE(FAD_SERVICE, code, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FAD_IOCTL_R_W(code, type) CTL_CODE(FAD_SERVICE, code, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FAD_IOCTL_R(code, type)   CTL_CODE(FAD_SERVICE, code, METHOD_BUFFERED, FILE_READ_ACCESS)
#define FAD_IOCTL_N(code)        CTL_CODE(FAD_SERVICE, code, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#else
#define FAD_IOCTL_W(code, type)   _IOW('a', code, type)
#define FAD_IOCTL_R_W(code, type) _IOR('a', code, type)
#define FAD_IOCTL_R(code, type)   _IOR('a', code, type)
#define FAD_IOCTL_N(code)        _IO('a', code)
#endif

#define IOCTL_FAD_GET_LASER_STATUS		FAD_IOCTL_R(1, FADDEVIOCTLLASER)
#define IOCTL_FAD_SET_LASER_STATUS		FAD_IOCTL_W(2, FADDEVIOCTLLASER)
#define IOCTL_SET_APP_EVENT				FAD_IOCTL_W(3, DWORD)	// Null-terminated string
#define IOCTL_FAD_GET_SHUTTER_STATUS	FAD_IOCTL_R(4, NotUsedAnyMore)
#define IOCTL_FAD_SET_SHUTTER_STATUS	FAD_IOCTL_W(5, NotUsedAnyMore)
// The following reset enters programming state
#define IOCTL_FAD_RESET_TEMPCPU			FAD_IOCTL_W(6, NotUsedAnyMore)
#define IOCTL_FAD_AQUIRE_FILE_ACCESS	FAD_IOCTL_W(7, NotUsedAnyMore)
#define IOCTL_FAD_RELEASE_FILE_ACCESS	FAD_IOCTL_R(8, NotUsedAnyMore)
// The following reset restarts the ADuC without entering bootloader mode
#define IOCTL_FAD_RESTART_TEMPCPU		FAD_IOCTL_W(9, NotUsedAnyMore)
#define IOCTL_FAD_GET_TORCH_STATUS      FAD_IOCTL_R(10, NotUsedAnyMore)
#define IOCTL_FAD_SET_TORCH_STATUS      FAD_IOCTL_W(11, NotUsedAnyMore)
#define IOCTL_FAD_BUZZER                FAD_IOCTL_W(12, FADDEVIOCTLBUZZER)
#define IOCTL_FAD_7173_GET_MODE         FAD_IOCTL_R(13, NotUsedAnyMore)
#define IOCTL_FAD_7173_SET_MODE         FAD_IOCTL_W(14, FADDEVIOCTL7173MODE)
#define IOCTL_FAD_GET_LCD_STATUS		FAD_IOCTL_R(15, FADDEVIOCTLLCD)
#define IOCTL_FAD_TRIG_OPTICS           FAD_IOCTL_N(16)
#define IOCTL_FAD_DISABLE_IRDA          FAD_IOCTL_W(17, NotUsedAnyMore)
#define IOCTL_FAD_ENABLE_IRDA           FAD_IOCTL_W(18, NotUsedAnyMore)
#define IOCTL_FAD_GET_COMPASS           FAD_IOCTL_R(19, NotUsedAnyMore)
#define IOCTL_FAD_GET_DIG_IO_STATUS     FAD_IOCTL_R(20, FADDEVIOCTLDIGIO)
#define IOCTL_FAD_SET_DIG_IO_STATUS     FAD_IOCTL_R(21, FADDEVIOCTLDIGIO)
#define IOCTL_FAD_ENABLE_WATCHDOG       FAD_IOCTL_R(22, NotUsedAnyMore)
#define IOCTL_FAD_TRIG_WATCHDOG         FAD_IOCTL_R(23, NotUsedAnyMore)
#define IOCTL_FAD_ENABLE_PT1000			FAD_IOCTL_R(24, NotUsedAnyMore)
#define IOCTL_FAD_GET_LED				FAD_IOCTL_R(25, FADDEVIOCTLLED)
#define IOCTL_FAD_SET_LED				FAD_IOCTL_W(26, FADDEVIOCTLLED)
#define IOCTL_FAD_GET_GPS_ENABLE		FAD_IOCTL_R(27, FADDEVIOCTLGPS)
#define IOCTL_FAD_SET_GPS_ENABLE		FAD_IOCTL_W(28, FADDEVIOCTLGPS)
#define IOCTL_FAD_GET_LASER_ACTIVE		FAD_IOCTL_R(29, FADDEVIOCTLLASERACTIVE)
#define IOCTL_FAD_SET_LASER_ACTIVE		FAD_IOCTL_W(30, FADDEVIOCTLLASERACTIVE)
#define IOCTL_FAD_GET_HDMI_STATUS		FAD_IOCTL_R(31, FADDEVIOCTLHDMI)
#define IOCTL_FAD_GET_COOLER_STATE		FAD_IOCTL_W(32, NotUsedAnyMore)
#define IOCTL_FAD_SET_COOLER_STATE		FAD_IOCTL_W(33, NotUsedAnyMore)
#define IOCTL_FAD_GET_MODE_WHEEL_POS    FAD_IOCTL_R(34, DWORD)
#define IOCTL_FAD_SET_HDMI_ACCESS       FAD_IOCTL_W(35, DWORD)
#define IOCTL_FAD_GET_KAKA_LED			FAD_IOCTL_R(36, FADDEVIOCTLLED)
#define IOCTL_FAD_SET_KAKA_LED			FAD_IOCTL_W(37, FADDEVIOCTLLED)
#define IOCTL_FAD_GET_CPU_INFO			FAD_IOCTL_R(38, NotUsedAnyMore)
#define IOCTL_FAD_SET_L2CACHE			FAD_IOCTL_W(39, NotUsedAnyMore)
#define IOCTL_FAD_GET_KP_BACKLIGHT      FAD_IOCTL_R(40, FADDEVIOCTLBACKLIGHT)
#define IOCTL_FAD_SET_KP_BACKLIGHT      FAD_IOCTL_W(41, FADDEVIOCTLBACKLIGHT)
#define IOCTL_FAD_GET_KP_SUBJ_BACKLIGHT FAD_IOCTL_R(42, FADDEVIOCTLSUBJBACKLIGHT)
#define IOCTL_FAD_SET_KP_SUBJ_BACKLIGHT FAD_IOCTL_W(43, FADDEVIOCTLSUBJBACKLIGHT)
#define IOCTL_FAD_GET_TC_STATE          FAD_IOCTL_W(44, NotUsedAnyMore)
#define IOCTL_FAD_GET_PAGE_POOL_INFO    FAD_IOCTL_W(45, NotUsedAnyMore)
#define IOCTL_FAD_GET_START_REASON      FAD_IOCTL_R_W(46, DWORD)
#define IOCTL_FAD_IS_WDOG_DISABLE_SUPP  FAD_IOCTL_W(47, NotUsedAnyMore)
#define IOCTL_FAD_GET_SECURITY_PARAMS   FAD_IOCTL_R(48, FADDEVIOCTLSECURITY)
#define IOCTL_FAD_RELEASE_READ          FAD_IOCTL_N(49)
#define IOCTL_FAD_GET_TRIG_PRESSED      FAD_IOCTL_R(50, FADDEVIOCTLTRIGPRESSED)
#define IOCTL_FAD_SET_LASER_MODE        FAD_IOCTL_W(51, FADDEVIOCTLLASERMODE)

// DeviceIoControl wrapper for CE/Linux/BTZCAMSIM crosscompatibility

// Win CE
//
#ifdef UNDER_CE

#define FADDRVNAME L"FAD1:"
#define FADControl(dev, func, inbuf, sz, outbuf, outbufsz, bytesret) DeviceIoControl(dev, func, inbuf, sz, outbuf, outbufsz, bytesret, NULL)
#define FADCreateHandle(path) CreateFile(path, 0, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)
#define FADCloseHandle(hnd) CloseHandle(hnd)

// BTZCAMSIM (NT, Linux, iOS, OSX) - Not fully supported
//
#elif defined(BTZCAMSIM)

#define FADControl(dev, func, inbuf, sz, outbuf, outbufsz, bytesret) TRUE
#define FADCreateHandle(path) (DHANDLE) 1
#define FADCloseHandle(hnd)

// Linux device
//
#else

#define FADDRVNAME "/dev/fad0"

#ifdef __cplusplus
extern "C" {
#endif

DHANDLE FOpenDriver(const char *name,
		    DWORD dwDesiredAccess,
		    DWORD dwShareMode,
		    void *res,
		    DWORD dwCreationDisposition,
		    DWORD dwFlagsAndAttributes,
		    void *res2);
BOOL DEVControl(DHANDLE hDev,
		DWORD dwIoControlCode,
		LPVOID lpInBuffer,
		DWORD nInBufSize,
		LPVOID lpOutBuffer,
		DWORD nOutBufSize,
		LPDWORD lpBytesReturned);

#ifdef __cplusplus
};
#endif

#define FADCreateHandle(path) FOpenDriver(path, 0, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)
#define FADCloseHandle(f) close((f))
#define FADControl(a, b, c, d, e, f, g) DEVControl(a, b, c, d, e, f, g)

#endif /* UNDER_CE */

#endif /* __MISCDEV_H__ */
