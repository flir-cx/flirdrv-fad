/***********************************************************************
*                                                                     
* Project: Balthazar
* $Date$
* $Author$
*
* $Id$
*
* Description of file:
*    FLIR Application Driver (FAD) main file.
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
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/poll.h>
#include <linux/version.h>
#include "flir-kernel-version.h"

#define ENOLASERIRQ 1
#define ENODIGIOIRQ 2
#define EUNKNOWNCPU 3


DWORD g_RestartReason = RESTART_REASON_NOT_SET;

// Function prototypes
static long FAD_IOControl(struct file *filep,
			  unsigned int cmd, unsigned long arg);
static unsigned int FadPoll(struct file *filp, poll_table * pt);
static ssize_t FadRead(struct file *filp, char __user * buf, size_t count,
		       loff_t * f_pos);

static PFAD_HW_INDEP_INFO gpDev;

static struct file_operations fad_fops = {
	.owner = THIS_MODULE,
//              .ioctl = FAD_IOControl,
	.unlocked_ioctl = FAD_IOControl,
	.read = FadRead,
	.poll = FadPoll,
};

// Code

/** 
 * CPU Specific initialization
 * Initializes GPIO, GPIO IRQ, etc per platform
 * 
 * 
 * @return 0 on success
 */
static int cpu_initialize(void)
{
	int retval;

	if (cpu_is_mx51()){
		retval = SetupMX51(gpDev);
	} else 
	if (cpu_is_imx6s()){
		retval = SetupMX6S(gpDev);
	} else if (cpu_is_imx6q()){
		retval = SetupMX6Q(gpDev);
	} else{
		pr_info("Unknown System CPU\n");
		retval = -EUNKNOWNCPU;
	}
	return retval;
}


/** 
 * CPU Specific Deinitialization
 * 
 */
static void cpu_deinitialize(void)
{
	if (cpu_is_mx51()){
		InvSetupMX51(gpDev);
	} else if (cpu_is_imx6s()){
		InvSetupMX6S(gpDev);
	} else if (cpu_is_imx6q()){
		InvSetupMX6Q(gpDev);
	} else{
		pr_info("Unknown System CPU\n");
	}
}

/**
 * FAD_Init
 * Initializes FAD
 *
 *
 * @return
 */
static int __init FAD_Init(void)
{
	int retval = 0;
	struct device * dev;

	pr_info("FAD_Init\n");

	// Allocate (and zero-initiate) our control structure.
	gpDev = (PFAD_HW_INDEP_INFO) kzalloc(sizeof(FAD_HW_INDEP_INFO), GFP_KERNEL);

	if (! gpDev) {
		pr_err("Error allocating memory for pDev, FAD_Init failed\n");
		goto EXIT_OUT;
	}

	alloc_chrdev_region(&gpDev->fad_dev, 0, 1, "fad");
	cdev_init(&gpDev->fad_cdev, &fad_fops);
	gpDev->fad_cdev.owner = THIS_MODULE;
	gpDev->fad_cdev.ops = &fad_fops;

	retval = cdev_add(&gpDev->fad_cdev, gpDev->fad_dev, 1);

	if (retval) {
		pr_err("Error adding device driver\n");
		goto EXIT_OUT_ADDEVICE;
	}

	gpDev->pLinuxDevice = platform_device_alloc("fad", 1);
	if (gpDev->pLinuxDevice == NULL) {
		pr_err("Error adding allocating device\n");
		goto EXIT_OUT_PLATFORMALLOC;
	}

	retval = platform_device_add(gpDev->pLinuxDevice);
	if(retval) {
		pr_err("Error adding platform device\n");
		goto EXIT_OUT_PLATFORMADD;
	}

	pr_debug("FAD driver device id %d.%d added\n", MAJOR(gpDev->fad_dev),
		 MINOR(gpDev->fad_dev));
	gpDev->fad_class = class_create(THIS_MODULE, "fad");
	
	dev = device_create(gpDev->fad_class, NULL, gpDev->fad_dev, NULL, "fad0");

	if(dev == NULL) {
		pr_err("Device creation failed\n");
		goto EXIT_OUT_DEVICE;
	}

	// initialize this device instance
	sema_init(&gpDev->semDevice, 1);
	sema_init(&gpDev->semIOport, 1);

	// init wait queue
	init_waitqueue_head(&gpDev->wq);

	retval = cpu_initialize();

	if (retval < 0){
		pr_err("Failed SetupMX6Q\n");
		goto EXIT_OUT_INIT;
	}


	//Set up Laser IRQ
	retval = InitLaserIrq(gpDev);
	if (retval) {
		pr_err("Failed to request Laser IRQ\n");
		retval = -ENOLASERIRQ;
		goto EXIT_NO_LASERIRQ;
	} else {
		pr_info("Successfully requested Laser IRQ\n");
	}

	// Set up Digital I/O IRQ
	retval = InitDigitalIOIrq(gpDev);
	if (retval) {
		pr_err("Failed to request DIGIN_1 IRQ\n");
		retval=-ENODIGIOIRQ;
		goto EXIT_NO_DIGIOIRQ;
	} else {
	pr_info("Successfully requested DIGIN_1 IRQ\n");
	}

	return retval;


EXIT_NO_DIGIOIRQ:
	if(! system_is_roco())
		free_irq(gpio_to_irq(DIGIN_1), gpDev);

EXIT_NO_LASERIRQ:
	if(! system_is_roco())
		free_irq(gpio_to_irq(LASER_ON), gpDev);

EXIT_OUT_INIT:
	cpu_deinitialize();
EXIT_OUT_DEVICE:
	device_destroy(gpDev->fad_class, gpDev->fad_dev);
EXIT_OUT_PLATFORMADD:
	platform_device_unregister(gpDev->pLinuxDevice);
EXIT_OUT_PLATFORMALLOC:
EXIT_OUT_ADDEVICE:
	cdev_del(&gpDev->fad_cdev);
EXIT_OUT:
	return -1;

}

/** 
 * FAD_Deinit
 * Cleanup after FAD Init on module unload
 * 
 * @return 
 */
static void __devexit FAD_Deinit(void)
{
	pr_info("FAD_Deinit\n");


	cpu_deinitialize();

	
	unregister_chrdev_region(gpDev->fad_dev, 1);
	device_destroy(gpDev->fad_class, gpDev->fad_dev);
	class_destroy(gpDev->fad_class);
	platform_device_unregister(gpDev->pLinuxDevice);
	kfree(gpDev);
	gpDev = NULL;
}



/** 
 * DOIOControl
 * 
 * @param pInfo 
 * @param Ioctl 
 * @param pBuf 
 * @param pUserBuf 
 * 
 * @return 
 */
static DWORD DoIOControl(PFAD_HW_INDEP_INFO pInfo,
			 DWORD Ioctl, PUCHAR pBuf, PUCHAR pUserBuf)
{
	DWORD retval = ERROR_INVALID_PARAMETER;
//    static ULONG ulWdogTime = 5000;    // 5 seconds
	static BOOL bGPSEnable = FALSE;

	switch (Ioctl) {
	case IOCTL_FAD_SET_LASER_STATUS:
		if (!pInfo->bHasLaser)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(pInfo);
			pInfo->bLaserEnable =
			    ((PFADDEVIOCTLLASER) pBuf)->bLaserPowerEnabled;
			pInfo->pSetLaserStatus(pInfo, pInfo->bLaserEnable);
			retval = ERROR_SUCCESS;
			UNLOCK(pInfo);
		}
		break;

	case IOCTL_FAD_GET_LASER_STATUS:
		if (!pInfo->bHasLaser)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(pInfo);
			pInfo->pGetLaserStatus(pInfo, (PFADDEVIOCTLLASER) pBuf);
			retval = ERROR_SUCCESS;
			UNLOCK(pInfo);
		}
		break;

	case IOCTL_SET_APP_EVENT:
		retval = ERROR_SUCCESS;
		break;

	case IOCTL_FAD_BUZZER:
		if (!gpDev->bHasBuzzer)
			retval = ERROR_NOT_SUPPORTED;
		else {
			FADDEVIOCTLBUZZER *pBuzzerData =
			    (FADDEVIOCTLBUZZER *) pBuf;
			LOCK(pInfo);
			if ((pBuzzerData->eState == BUZZER_ON) ||
			    (pBuzzerData->eState == BUZZER_TIME)) {
				// Activate sound
				pInfo->pSetBuzzerFrequency(pBuzzerData->usFreq,
							   pBuzzerData->ucPWM);
			}
			if (pBuzzerData->eState == BUZZER_TIME) {
				UNLOCK(pInfo);
				msleep(pBuzzerData->usTime);
				LOCK(pInfo);
			}
			if ((pBuzzerData->eState == BUZZER_OFF) ||
			    (pBuzzerData->eState == BUZZER_TIME)) {
				// Switch off sound
				pInfo->pSetBuzzerFrequency(0, 0);
			}
			UNLOCK(pInfo);
			retval = ERROR_SUCCESS;
		}
		break;


	case IOCTL_FAD_GET_DIG_IO_STATUS:
		if (!pInfo->bHasDigitalIO)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(pInfo);
			pInfo->pGetDigitalStatus((PFADDEVIOCTLDIGIO) pBuf);
			retval = ERROR_SUCCESS;
			UNLOCK(pInfo);
		}
		break;

	case IOCTL_FAD_GET_KAKA_LED:
		if (!pInfo->bHasKAKALed)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(pInfo);
			pInfo->pGetKAKALedState(pInfo, (PFADDEVIOCTLLED) pBuf);
			retval = ERROR_SUCCESS;
			UNLOCK(pInfo);
		}
		break;

	case IOCTL_FAD_SET_KAKA_LED:
		if (!pInfo->bHasKAKALed)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(pInfo);
			pInfo->pSetKAKALedState(pInfo, (PFADDEVIOCTLLED) pBuf);
			retval = ERROR_SUCCESS;
			UNLOCK(pInfo);
		}
		break;

	case IOCTL_FAD_SET_GPS_ENABLE:
		if (!pInfo->bHasGPS)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(pInfo);
			pInfo->pSetGPSEnable(((PFADDEVIOCTLGPS) pBuf)->
					     bGPSEnabled);
			bGPSEnable = ((PFADDEVIOCTLGPS) pBuf)->bGPSEnabled;
			retval = ERROR_SUCCESS;
			UNLOCK(pInfo);
		}
		break;

	case IOCTL_FAD_GET_GPS_ENABLE:
		if (!pInfo->bHasGPS)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(pInfo);
			pInfo->
			    pGetGPSEnable(&
					  (((PFADDEVIOCTLGPS) pBuf)->
					   bGPSEnabled));
			retval = ERROR_SUCCESS;
			UNLOCK(pInfo);
		}
		break;

	case IOCTL_FAD_SET_LASER_ACTIVE:
		if (!gpDev->bHasLaser)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(pInfo);
			pInfo->pSetLaserActive(pInfo,
					       ((FADDEVIOCTLLASERACTIVE *)
						pBuf)->bLaserActive == TRUE);
			retval = ERROR_SUCCESS;
			UNLOCK(pInfo);
		}
		break;

	case IOCTL_FAD_GET_LASER_ACTIVE:
		if (!gpDev->bHasLaser)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(pInfo);
			((FADDEVIOCTLLASERACTIVE *) pBuf)->bLaserActive =
			    pInfo->pGetLaserActive(pInfo);
			retval = ERROR_SUCCESS;
			UNLOCK(pInfo);
		}
		break;

	case IOCTL_FAD_GET_HDMI_STATUS:
		retval = ERROR_NOT_SUPPORTED;
		break;

	case IOCTL_FAD_GET_MODE_WHEEL_POS:
		retval = ERROR_NOT_SUPPORTED;
		break;

	case IOCTL_FAD_SET_HDMI_ACCESS:
		retval = ERROR_NOT_SUPPORTED;
		break;

	case IOCTL_FAD_GET_KP_BACKLIGHT:
		if (!gpDev->bHasKpBacklight)
			retval = ERROR_NOT_SUPPORTED;
		else {
			retval =
			    pInfo->
			    pGetKeypadBacklight((FADDEVIOCTLBACKLIGHT *) pBuf);
		}
		break;

	case IOCTL_FAD_SET_KP_BACKLIGHT:
		if (!gpDev->bHasKpBacklight)
			retval = ERROR_NOT_SUPPORTED;
		else {
			retval =
			    pInfo->
			    pSetKeypadBacklight((FADDEVIOCTLBACKLIGHT *) pBuf);
		}
		break;

	case IOCTL_FAD_GET_KP_SUBJ_BACKLIGHT:
		if (!gpDev->bHasKpBacklight)
			retval = ERROR_NOT_SUPPORTED;
		else {
			retval =
			    pInfo->pGetKeypadSubjBacklight(pInfo,
							   (FADDEVIOCTLSUBJBACKLIGHT
							    *) pBuf);
		}
		break;

	case IOCTL_FAD_SET_KP_SUBJ_BACKLIGHT:
		if (!gpDev->bHasKpBacklight)
			retval = ERROR_NOT_SUPPORTED;
		else {
			retval =
			    pInfo->pSetKeypadSubjBacklight(pInfo,
							   (FADDEVIOCTLSUBJBACKLIGHT
							    *) pBuf);
		}
		break;

	case IOCTL_FAD_GET_START_REASON:
		memcpy(pBuf, &g_RestartReason, sizeof(DWORD));
		retval = ERROR_SUCCESS;
		break;

	case IOCTL_FAD_GET_SECURITY_PARAMS:
		{
			PFADDEVIOCTLSECURITY pSecurity =
			    (PFADDEVIOCTLSECURITY) pBuf;
			pSecurity->ulVersion = INITIAL_VERSION;
			pSecurity->ullUniqueID = 0;
			pSecurity->ulRequire30HzCFClevel = 0;
			pSecurity->ulRequiredConfigCFClevel = 0;
		}
		retval = ERROR_SUCCESS;
		break;

	case IOCTL_FAD_RELEASE_READ:
		pInfo->eEvent = FAD_RESET_EVENT;
		wake_up_interruptible(&pInfo->wq);
		break;

	default:
		pr_err("FAD: Unsupported IOCTL code %lX\n", Ioctl);
		retval = ERROR_NOT_SUPPORTED;
		break;
	}

	// pass back appropriate response codes
	return retval;
}

/** 
 * FAD_IOControl
 * 
 * @param filep 
 * @param cmd 
 * @param arg 
 * 
 * @return 
 */
static long FAD_IOControl(struct file *filep,
			  unsigned int cmd, unsigned long arg)
{
	DWORD retval = ERROR_SUCCESS;
	char *tmp;

	tmp = kzalloc(_IOC_SIZE(cmd), GFP_KERNEL);
	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		pr_debug("FAD Ioctl %X copy from user: %d\n", cmd,
			 _IOC_SIZE(cmd));
		retval = copy_from_user(tmp, (void *)arg, _IOC_SIZE(cmd));
		if (retval)
			pr_err("FAD Copy from user failed: %lu\n", retval);
	}

	if (retval == ERROR_SUCCESS) {
		pr_debug("FAD Ioctl %X\n", cmd);
		retval = DoIOControl(gpDev, cmd, tmp, (PUCHAR) arg);
		if (retval && (retval != ERROR_NOT_SUPPORTED))
			pr_err("FAD Ioctl failed: %X %ld %d\n", cmd, retval,
			       _IOC_NR(cmd));
	}

	if ((retval == ERROR_SUCCESS) && (_IOC_DIR(cmd) & _IOC_READ)) {
		pr_debug("FAD Ioctl %X copy to user: %u\n", cmd,
			 _IOC_SIZE(cmd));
		retval = copy_to_user((void *)arg, tmp, _IOC_SIZE(cmd));
		if (retval)
			pr_err("FAD Copy to user failed: %ld\n", retval);
	}
	kfree(tmp);

	return retval;
}
/** 
 * FADPoll
 * 
 * @param filp 
 * @param pt 
 * 
 * @return 
 */
static unsigned int FadPoll(struct file *filp, poll_table * pt)
{
	poll_wait(filp, &gpDev->wq, pt);

	return (gpDev->eEvent != FAD_NO_EVENT) ? (POLLIN | POLLRDNORM) : 0;
}
/** 
 * FadRead
 * 
 * @param filp 
 * @param buf 
 * @param count 
 * @param f_pos 
 * 
 * @return 
 */
static ssize_t FadRead(struct file *filp, char *buf, size_t count,
		       loff_t * f_pos)
{
	int res;

	if (count < 1)
		return -EINVAL;
	res =
	    wait_event_interruptible(gpDev->wq, gpDev->eEvent != FAD_NO_EVENT);
	if (res < 0)
		return res;
	*buf = gpDev->eEvent;
	gpDev->eEvent = FAD_NO_EVENT;
	return 1;
}



module_init(FAD_Init);
module_exit(FAD_Deinit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FLIR Application Driver");
MODULE_AUTHOR("Peter Fitger, FLIR Systems AB");
