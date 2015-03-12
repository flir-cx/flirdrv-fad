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
#include "flir-kernel-version.h"
#include <linux/of_gpio.h>

// Definitions
#define ENOLASERIRQ 1
#define ENODIGIOIRQ 2
// Local variables

// Function prototypes


static DWORD setKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED * pLED);
static DWORD getKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED * pLED);
static void getDigitalStatus(PFADDEVIOCTLDIGIO pDigioStatus);
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
static BOOL getGPSEnable(BOOL *on);
static void WdogInit(PFAD_HW_INDEP_INFO gpDev, UINT32 Timeout);
static BOOL WdogService(PFAD_HW_INDEP_INFO gpDev);
static void BspGetSubjBackLightLevel(UINT8 * pLow, UINT8 * pMedium,
				     UINT8 * pHigh);
static void CleanupHW(PFAD_HW_INDEP_INFO gpDev);

// Code

int SetupMX6Q(PFAD_HW_INDEP_INFO gpDev)
{
	extern struct list_head leds_list;
	extern struct rw_semaphore leds_list_lock;
	struct led_classdev *led_cdev;
	int retval;
	u32 tmp;


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
	gpDev->pWdogInit = WdogInit;
	gpDev->pWdogService = WdogService;
	gpDev->pCleanupHW = CleanupHW;


/* Configure I2C from Devicetree */
	retval = of_property_read_u32_index(gpDev->node, "hI2C1", 0, &tmp);
	if (retval) {
		pr_err("flirdrv-fad: couldn't read hI2C1 from DT!\n");
		goto EXIT_I2C1;
	}

	gpDev->hI2C1 = i2c_get_adapter(tmp);

	retval = of_property_read_u32_index(gpDev->node, "hI2C2", 0, &tmp);
	if (retval) {
		pr_err("flirdrv-fad: couldn't read hI2C2 from DT!\n");
		goto EXIT_I2C2;
	}

	gpDev->hI2C2 = i2c_get_adapter(tmp);

	pr_info("flirdrv-fad: I2C drivers %p and %p\n", gpDev->hI2C1, gpDev->hI2C2);


	/* Configure devices (bools) from DT */
	//Do not care about return value of function
	//If property is missing, assume device doesnt exist!
	//Better to wrap this in separate function... (int -> bool etc...)
	of_property_read_u32_index(gpDev->node, "hasLaser", 0, &gpDev->bHasLaser);
	of_property_read_u32_index(gpDev->node, "HasGPS", 0, &gpDev->bHasGPS);
	of_property_read_u32_index(gpDev->node, "Has7173", 0, &gpDev->bHas7173);
	of_property_read_u32_index(gpDev->node, "Has5VEnable", 0, &gpDev->bHas5VEnable);
	of_property_read_u32_index(gpDev->node, "HasDigitalIO", 0, &gpDev->bHasDigitalIO);
	of_property_read_u32_index(gpDev->node, "HasKAKALed", 0, &gpDev->bHasKAKALed);
	of_property_read_u32_index(gpDev->node, "HasBuzzer", 0, &gpDev->bHasBuzzer);
	of_property_read_u32_index(gpDev->node, "HasKpBacklight",
				   0, &gpDev->bHasKpBacklight);
	of_property_read_u32_index(gpDev->node, "HasSoftwareControlledLaser",
				   0, &gpDev->bHasSoftwareControlledLaser);

	if(gpDev->bHasLaser)
		pr_info("flirdrv-fad: HasLaser\n");
	if(gpDev->bHasGPS)
		pr_info("flirdrv-fad: HasGPS\n");
	if(gpDev->bHas7173)
		pr_info("flirdrv-fad: Has7173\n");
	if(gpDev->bHas5VEnable)
		pr_info("flirdrv-fad: Has5VEnable\n");
	if(gpDev->bHasDigitalIO)
		pr_info("flirdrv-fad: HasDigitalIO\n");
	if(gpDev->bHasKAKALed)
		pr_info("flirdrv-fad: HasKAKALed\n");
	if(gpDev->bHasBuzzer)
		pr_info("flirdrv-fad: HasBuzzer\n");
	if(gpDev->bHasKpBacklight)
		pr_info("flirdrv-fad: HasKpBacklight\n");
	if(gpDev->bHasSoftwareControlledLaser)
		pr_info("flirdrv-fad: HasSoftwareControlledLaser\n");

	BspGetSubjBackLightLevel(&gpDev->Keypad_bl_low,
				 &gpDev->Keypad_bl_medium,
				 &gpDev->Keypad_bl_high);

	if (gpDev->bHasLaser) {
		int pin;
		pin = of_get_named_gpio_flags(gpDev->node, "laser_on-gpios", 0, NULL);
		if (gpio_is_valid(pin) == 0){
			pr_err("flirdrv-fad: LaserON can not be used\n");
		} else {
			gpio_request(pin, "LaserON");
			gpio_direction_input(pin);
			retval = InitLaserIrq(gpDev);
			if (retval) {
				pr_err("flirdrv-fad: Failed to request Laser IRQ\n");
				retval = -ENOLASERIRQ;
				goto EXIT_NO_LASERIRQ;
			}
		}
	}


	if (gpDev->bHasLaser) {
		int pin;
		pin = of_get_named_gpio_flags(gpDev->node, "laser_soft-gpios", 0, NULL);
		if (gpio_is_valid(pin) == 0){
			pr_err("flirdrv-fad: Laser Soft On can not be used\n");
		} else {
			retval = gpio_request(pin, "LaserSoftOn");
			if(retval){
				pr_err("Fail registering lasersofton\n");
			}
			retval = gpio_direction_output(pin, 0);
			if(retval){
				pr_err("Fail setting direction lasersofton\n");
			}
			//TODO Error handling
		}
	}


	if (gpDev->bHasLaser) {
		int pin;
		pin = of_get_named_gpio_flags(gpDev->node, "laser_switch-gpios", 0, NULL);
		if (gpio_is_valid(pin) == 0){
			pr_err("flirdrv-fad: Laser Switch can not be used\n");
		} else {
			retval = gpio_request(pin, "LaserSwitchOn");
			if(retval){
				pr_err("Fail registering laserswitchon\n");
			}
			retval = gpio_direction_output(pin, 0);
			if(retval){
				pr_err("Fail setting direction laserswitcon\n");
			}

			//TODO Error handling
		}
	}

	goto EXIT;

EXIT_NO_LASERIRQ:
	if(gpDev->bHasLaser){
		FreeLaserIrq(gpDev);
	}

	i2c_put_adapter(gpDev->hI2C2);
EXIT_I2C2:
	i2c_put_adapter(gpDev->hI2C1);
EXIT_I2C1:
EXIT:
	return retval;
}

/** 
 * Inverse setup done in SetupMX6Q...
 * 
 * @param gpDev 
 */
void InvSetupMX6Q(PFAD_HW_INDEP_INFO gpDev)
{
	if (gpDev->bHasLaser) {
		int pin;
		FreeLaserIrq(gpDev);
		pin = of_get_named_gpio_flags(gpDev->node, "laser_on-gpios", 0, NULL);
		gpio_free(pin);
	}
	if (gpDev->bHasLaser) {
		int pin;
		pin = of_get_named_gpio_flags(gpDev->node, "laser_soft-gpios", 0, NULL);
		gpio_free(pin);
	}
	if (gpDev->bHasLaser) {
		int pin;
		pin = of_get_named_gpio_flags(gpDev->node, "laser_switch-gpios", 0, NULL);
		gpio_free(pin);
	}

	

	i2c_put_adapter(gpDev->hI2C1);
	i2c_put_adapter(gpDev->hI2C2);

}

void CleanupHW(PFAD_HW_INDEP_INFO gpDev)
{
	pr_warn("flirdrv-fad: CleanupHW handled by unloading the FAD kernel module...");
}

DWORD setKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED * pLED)
{
	return 0;
}

DWORD getKAKALedState(PFAD_HW_INDEP_INFO gpDev, FADDEVIOCTLLED * pLED)
{
	return 0;
}

void getDigitalStatus(PFADDEVIOCTLDIGIO pDigioStatus)
{
}

void setLaserStatus(PFAD_HW_INDEP_INFO gpDev, BOOL on)
{
//	SetI2CIoport(pInfo, LASER_SWITCH_ON, LaserStatus);
	int pin;
	pin = of_get_named_gpio_flags(gpDev->node, "laser_switch-gpios",0,NULL);
	gpio_set_value_cansleep(pin, on);
}

// Laser button has been pressed/released.
// In software controlled laser, we must enable/disable laser here.
void updateLaserOutput(PFAD_HW_INDEP_INFO gpDev)
{
	if (gpDev->bHasSoftwareControlledLaser) {
		FADDEVIOCTLLASER laserStatus = { 0 };
		getLaserStatus(gpDev, &laserStatus);

		if (laserStatus.bLaserIsOn && laserStatus.bLaserPowerEnabled) {
//			SetI2CIoport(pInfo, LASER_SWITCH_ON, 1);
			setLaserStatus(gpDev, 1);
		} else {
//			SetI2CIoport(pInfo, LASER_SWITCH_ON, 0);
			setLaserStatus(gpDev, 0);
		}
	}
}
void getLaserStatus(PFAD_HW_INDEP_INFO gpDev, PFADDEVIOCTLLASER pLaserStatus)
{
	int pin;
	int value;
	pin = of_get_named_gpio_flags(gpDev->node, "laser_on-gpios", 0, NULL);
	value=gpio_get_value_cansleep(pin) == 0;
	pLaserStatus->bLaserIsOn = value;

//	pLaserStatus->bLaserPowerEnabled = GetI2CIoport(pInfo, LASER_SWITCH_ON);
	pin = of_get_named_gpio_flags(gpDev->node, "laser_switch-gpios", 0, NULL);
	value = (gpio_get_value_cansleep(pin));
	pLaserStatus->bLaserPowerEnabled = value;
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
	pr_info("flirdrv-fad: Watchdog init deprecated\n");
}

BOOL WdogService(PFAD_HW_INDEP_INFO gpDev)
{
	pr_info("flirdrv-fad: WatchdogService deprecated\n");
	return TRUE;
}

void SetLaserActive(PFAD_HW_INDEP_INFO gpDev, BOOL on)
{
//	SetI2CIoport(pInfo, LASER_SOFT_ON, bEnable);

	int pin;
	pin = of_get_named_gpio_flags(gpDev->node, "laser_soft-gpios",0,NULL);
	gpio_set_value_cansleep(pin, on);

}

/** 
 * Read gpio value laser_soft-gpios,
 * 
 * 
 * @param gpDev 
 * 
 * @return output of gpio_get_value_cansleep(pin)
 */
BOOL GetLaserActive(PFAD_HW_INDEP_INFO gpDev)
{
//	return GetI2CIoport(pInfo, LASER_SOFT_ON);

	int pin;
	pin = of_get_named_gpio_flags(gpDev->node, "laser_soft-gpios",0,NULL);
	return (gpio_get_value_cansleep(pin)) ;
}

void SetBuzzerFrequency(USHORT usFreq, UCHAR ucPWM)
{
	pr_info("flirdrv-fad SetBuzzerFrequency not implemented\n");
}

DWORD SetKeypadSubjBacklight(PFAD_HW_INDEP_INFO gpDev,
			     PFADDEVIOCTLSUBJBACKLIGHT pBacklight)
{
	pr_info("flirdrv-fad SetKeypadSubjBacklight not implemented\n");
	return 0;
}


/** 
 * 
 *  Function:  GetKeypadSubjBacklight
 *
 *    This method returns the keypad backlight in subjective levels.
 *    Since this function works in parallel with the older functions setting
 *    percentage values, it copes with values that differs from the defined 
 *    subjective levels.
 *
 * @param gpDev 
 * @param pBacklight 
 * 
 * @return 
 */
DWORD GetKeypadSubjBacklight(PFAD_HW_INDEP_INFO gpDev,
			     PFADDEVIOCTLSUBJBACKLIGHT pBacklight)
{
	pr_info("flirdrv-fad GetKeypadSubjBackligt not implemented\n");
	return 0;
}


DWORD GetKeypadBacklight(PFADDEVIOCTLBACKLIGHT pBacklight)
{
	pr_info("flirdrv-fad: GetKeypadBackligt not implemented\n");
	return 0;
}

DWORD SetKeypadBacklight(PFADDEVIOCTLBACKLIGHT pBacklight)
{
	pr_info("flirdrv-fad: SetKeypadBacklight not implemented\n");
	return 0;
}

void BspGetSubjBackLightLevel(UINT8 * pLow, UINT8 * pMedium, UINT8 * pHigh)
{
	*pLow = 10;
	*pMedium = 40;
	*pHigh = 75;
}
