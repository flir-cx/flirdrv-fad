// SPDX-License-Identifier: GPL-2.0-or-later
/***********************************************************************
 *
 * Project: Balthazar
 *
 * Description of file:
 *    FLIR Application Driver (FAD) main file.
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
#include <linux/leds.h>
#include <linux/suspend.h>
#include <linux/miscdevice.h>
#include <linux/alarmtimer.h>
#include <linux/reboot.h>
#include <linux/backlight.h>
#include <../drivers/base/power/power.h>
#if KERNEL_VERSION(3, 10, 0) <= LINUX_VERSION_CODE
#include <asm/system_info.h>
#else
#include <asm/system.h>
int cpu_is_imx6dl(void);
#endif
#define EUNKNOWNCPU 3

DWORD g_RestartReason = RESTART_REASON_NOT_SET;

static int power_state = ON_STATE;
module_param(power_state, int, 0);
MODULE_PARM_DESC(power_state, "Camera charge state: run=2,charge=3");
static int timed_standby;
module_param(timed_standby, int, 0);
MODULE_PARM_DESC(timed_standby,
		 "If set to 1, Wake up camera after 1 minute in suspend (instead of 6 hours)");

// Function prototypes
static long FAD_IOControl(struct file *filep,
			  unsigned int cmd, unsigned long arg);
static unsigned int FadPoll(struct file *filp, poll_table *pt);
static ssize_t FadRead(struct file *filp, char __user *buf, size_t count,
		       loff_t *f_pos);

// gpDev is global, which is generally undesired, we can fix this
// in fad_probe, platform_set_drvdata sets gpDev as the driverdata,
// if we have the device, we can get the platform with to_platform_device
static PFAD_HW_INDEP_INFO gpDev;

#if KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE
//Workaround to allow 3.14 kernel to work...
struct platform_device *pdev;
#endif

static const struct file_operations fad_fops = {
	.owner = THIS_MODULE,
//              .ioctl = FAD_IOControl,
	.unlocked_ioctl = FAD_IOControl,
	.read = FadRead,
	.poll = FadPoll,
};

static struct miscdevice fad_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "fad0",
	.fops = &fad_fops
};

#if KERNEL_VERSION(3, 14, 0) <= LINUX_VERSION_CODE  && KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE
// get_suspend_wakup_source is implemented in evander/lennox (3.14.x) kernel
struct wakeup_source *get_suspend_wakup_source(void);
#warning "we use (3.14.x) kernel get_suspend_wakeup_source"

#else
#warning "local dummy wakeup compatibility due to kernel version check - do not expect 6 hour standby->off to work"
void *get_suspend_wakup_source(void)
{
	return NULL;
}
#endif

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
#ifdef CONFIG_OF
	if (of_machine_is_compatible("fsl,imx6dl-ec101") ||
	    of_machine_is_compatible("fsl,imx6dl-ec501")) {
		if (of_machine_is_compatible("fsl,imx6dl-ec501"))
			gpDev->bHasKAKALed = TRUE;
		retval = SetupMX6Platform(gpDev);
	} else if (of_machine_is_compatible("fsl,imx6q")) {
		gpDev->node = of_find_compatible_node(NULL, NULL, "flir,fad");
		retval = SetupMX6Q(gpDev);
	} else
#endif
		if (cpu_is_imx6s()) {
			gpDev->bHasDigitalIO = TRUE;
			gpDev->bHasKAKALed = TRUE;
			retval = SetupMX6S(gpDev);
		} else {
			pr_err("Unknown System CPU\n");
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
#ifdef CONFIG_OF
	if (of_machine_is_compatible("fsl,imx6dl-ec101") ||
	    of_machine_is_compatible("fsl,imx6dl-ec501")) {
		InvSetupMX6Platform(gpDev);
	} else if (of_machine_is_compatible("fsl,imx6q")) {
		of_node_put(gpDev->node);
		InvSetupMX6Q(gpDev);
	} else
#endif
		if (cpu_is_imx6s()) {
			InvSetupMX6S(gpDev);
		} else {
			pr_err("Unknown System CPU\n");
		}
}

/**
 * Device attribute "fadsuspend" to sync with application during suspend/resume
 *
 */

static ssize_t charge_state_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	switch (power_state) {
	case SUSPEND_STATE:
		strcpy(buf, "suspend");
		break;

	case ON_STATE:
		strcpy(buf, "run");
		break;

	case USB_CHARGE_STATE:
		strcpy(buf, "charge");
		break;

	default:
		strcpy(buf, "unknown");
		break;
	}

	return strlen(buf);
}

static ssize_t fadsuspend_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	return charge_state_show(dev, attr, buf);
}

static ssize_t fadsuspend_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t len)
{
	if (gpDev->bSuspend) {
		if ((len == 1) && (*buf == '1'))
			gpDev->bSuspend = 0;
		else
			pr_err("App standby prepare fail %d %c\n", len,
			       (len > 0) ? *buf : ' ');
		complete(&gpDev->standbyComplete);
	} else
		pr_debug("App resume\n");

	return sizeof(char);
}

static ssize_t charge_state_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{

	if (!strncmp(buf, "run", strlen("run")))
		power_state = ON_STATE;
	else if (!strncmp(buf, "charge", strlen("charge")))
		power_state = USB_CHARGE_STATE;
	else
		return -EINVAL;

	sysfs_notify(&gpDev->pLinuxDevice->dev.kobj, NULL, "fadsuspend");
	return len;
}

static ssize_t timed_standby_show(struct device *dev, struct device_attribute *attr,
				  char *buf)
{
	if (timed_standby)
		strcpy(buf, "on");
	else
		strcpy(buf, "off");

	return strlen(buf);
}

static ssize_t timed_standby_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	if (!strncmp(buf, "1", 1))
		timed_standby = true;
	else if (!strncmp(buf, "0", 1))
		timed_standby = false;
	else
		return -EINVAL;
	return len;
}

static ssize_t chargersuspend_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t len)
{
	int ret = 0;

	if (gpDev->pSetChargerSuspend != NULL) {
		if (strncmp(buf, "1", 1) == 0) {
			gpDev->pSetChargerSuspend(gpDev, true);
			ret = len;
		} else if (strncmp(buf, "0", 1) == 0) {
			gpDev->pSetChargerSuspend(gpDev, false);
			ret = len;
		} else {
			pr_err
			    ("chargersuspend unknown command... 1/0 accepted\n");
		}
	}
	return ret;
}

static ssize_t trigger_poll_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	strcpy(buf, "X");

	return strlen(buf);
}

static DEVICE_ATTR(timed_standby, 0644, timed_standby_show, timed_standby_store);
static DEVICE_ATTR(charge_state, 0644, charge_state_show, charge_state_store);
static DEVICE_ATTR(fadsuspend, 0644, fadsuspend_show, fadsuspend_store);
static DEVICE_ATTR(chargersuspend, 0644, NULL, chargersuspend_store);
static DEVICE_ATTR(trigger_poll, 0444, trigger_poll_show, NULL);

static struct attribute *faddev_sysfs_attrs[] = {
	&dev_attr_chargersuspend.attr,
	NULL
};

static struct attribute_group faddev_sysfs_attr_grp = {
	.name = "control",
	.attrs = faddev_sysfs_attrs,
};

/*
 * Get reason camera woke from standby
 *
 */
int get_wake_reason(void)
{
	struct wakeup_source *ws;

	ws = get_suspend_wakup_source();

	if (timed_standby)
		return ON_OFF_BUTTON_WAKE;

	if (!ws) {
		pr_err("fad: No suspend wakeup source");
		return UNKNOWN_WAKE;
	}

	if (strstr(ws->name, "onkey"))
		return ON_OFF_BUTTON_WAKE;

	if (strstr(ws->name, "wake"))
		return USB_CABLE_WAKE;

	if (strstr(ws->name, "rtc") && !timed_standby) {
		pr_err("fad: Standby shutdown\n");
		orderly_poweroff(1);
		return UNKNOWN_WAKE;
	}

	pr_err("Unknown suspend wake reason");
	return UNKNOWN_WAKE;
}

/**
 * Power notify callback for application sync during suspend/resume
 *
 */

#ifdef CONFIG_OF
/** Switch off camera after 6 hours in standby */
enum alarmtimer_restart fad_standby_timeout(struct alarm *alarm, ktime_t kt)
{
	pr_err("Standby timeout\n");

	// Switch of backlight as fast as possible (just activated in early resume)
	if (gpDev->backlight) {
		gpDev->backlight->props.power = 4;
		gpDev->backlight->props.brightness = 0;
		backlight_update_status(gpDev->backlight);
	}
	// Actual switch off will be done in get_wake_reason() when resume finished
	return ALARMTIMER_NORESTART;
}

/** Wake up camera after 1 minute in (timed) standby */
enum alarmtimer_restart fad_standby_wakeup(struct alarm *alarm, ktime_t kt)
{
	pr_debug("Standby wakeup\n");

	return ALARMTIMER_NORESTART;
}

static int fad_notify(struct notifier_block *nb, unsigned long val, void *ign)
{
	ktime_t kt;

	switch (val) {
	case PM_SUSPEND_PREPARE:
		pr_debug("%s: SUSPEND %d\n", __func__, gpDev->standbyMinutes);

		// Make appcore enter standby
		power_state = SUSPEND_STATE;
		gpDev->bSuspend = 1;
		sysfs_notify(&gpDev->pLinuxDevice->dev.kobj, NULL,
			     "fadsuspend");

		if (timed_standby) {
			// Set a timer to wake us up in 1 minute
			alarm_init(gpDev->alarm, ALARM_REALTIME,
				   &fad_standby_wakeup);
			kt = ktime_set(60, 0);
		} else {
			// Set a timer to wake us up in 6 hours
			alarm_init(gpDev->alarm, ALARM_REALTIME,
				   &fad_standby_timeout);
			kt = ktime_set(60 * gpDev->standbyMinutes, 0);
		}
		alarm_start_relative(gpDev->alarm, kt);

		// Wait for appcore
		wait_for_completion_timeout(&gpDev->standbyComplete,
					    msecs_to_jiffies(10000));
		if (gpDev->bSuspend) {
			pr_err("Application suspend failed\n");
			return NOTIFY_BAD;
		}
		return NOTIFY_OK;

	case PM_POST_SUSPEND:
		pr_debug("%s: POST_SUSPEND\n", __func__);
		if (get_wake_reason() == USB_CABLE_WAKE)
			power_state = USB_CHARGE_STATE;
		else
			power_state = ON_STATE;

		gpDev->bSuspend = 0;
		alarm_cancel(gpDev->alarm);
		sysfs_notify(&gpDev->pLinuxDevice->dev.kobj, NULL,
			     "fadsuspend");
		return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}
#endif

static int fad_probe(struct platform_device *pdev)
{
	int ret;

	pr_debug("Probing FAD driver\n");

	/* Allocate (and zero-initiate) our control structure. */
	gpDev =
	    (PFAD_HW_INDEP_INFO) devm_kzalloc(&pdev->dev,
					      sizeof(FAD_HW_INDEP_INFO),
					      GFP_KERNEL);
	if (!gpDev) {
		ret = -ENOMEM;
		pr_err
		    ("flirdrv-fad: Error allocating memory for pDev, FAD_Init failed\n");
		goto exit;
	}
	gpDev->alarm =
	    (struct alarm *)devm_kzalloc(&pdev->dev, sizeof(struct alarm),
					 GFP_KERNEL);
	if (!gpDev->alarm) {
		ret = -ENOMEM;
		pr_err
		    ("flirdrv-fad: Error allocating memory for gpDev->alarm, FAD_Init failed\n");
		goto exit;
	}

	gpDev->pLinuxDevice = pdev;
	platform_set_drvdata(pdev, gpDev);
	ret = misc_register(&fad_miscdev);
	if (ret) {
		pr_err("Failed to register miscdev for FAD driver\n");
		goto exit;
	}
	// initialize this device instance
	sema_init(&gpDev->semDevice, 1);
	sema_init(&gpDev->semIOport, 1);

	// init wait queue
	init_waitqueue_head(&gpDev->wq);

	// Set up CPU specific stuff
	ret = cpu_initialize();
	if (ret < 0) {
		pr_err("flirdrv-fad: Failed to initialize CPU\n");
		goto exit_cpuinitialize;
	}
	// Set up suspend handling
	ret = device_create_file(&pdev->dev, &dev_attr_fadsuspend);
	if (ret) {
		pr_err("FADDEV Error creating sysfs grp control\n");
		goto exit_createfile_fadsuspend;
	}
	ret = device_create_file(&pdev->dev, &dev_attr_timed_standby);
	if (ret) {
		pr_err("FADDEV Error creating sysfs grp control\n");
		goto exit_createfile_timedstandby;
	}

	if (gpDev->bHasTrigger) {
		ret = device_create_file(&pdev->dev, &dev_attr_trigger_poll);
		if (ret) {
			pr_err("FADDEV Error creating sysfs grp control\n");
			goto exit_createfile_trigger_poll;
		}
	}
	ret = sysfs_create_group(&pdev->dev.kobj, &faddev_sysfs_attr_grp);
	if (ret) {
		pr_err("FADDEV Error creating sysfs grp control\n");
		goto exit_sysfs_create_group;
	}
#ifdef CONFIG_OF
	ret =
	    device_create_file(&gpDev->pLinuxDevice->dev,
			       &dev_attr_charge_state);
	if (ret) {
		pr_err("FADDEV Error creating sysfs grp control\n");
		goto exit_sysfs_createfile_chargestate;
	}
	gpDev->nb.notifier_call = fad_notify;
	gpDev->nb.priority = 0;
	ret = register_pm_notifier(&gpDev->nb);
	if (ret) {
		pr_err("FADDEV Error creating sysfs grp control\n");
		goto exit_register_pm_notifier;
	}
#endif
	init_completion(&gpDev->standbyComplete);

	return ret;

#ifdef CONFIG_OF
	unregister_pm_notifier(&gpDev->nb);
exit_register_pm_notifier:
	device_remove_file(&pdev->dev, &dev_attr_charge_state);
exit_sysfs_createfile_chargestate:
#endif
	sysfs_remove_group(&pdev->dev.kobj, &faddev_sysfs_attr_grp);
exit_sysfs_create_group:
	if (gpDev->bHasTrigger)
		device_remove_file(&pdev->dev, &dev_attr_trigger_poll);
exit_createfile_trigger_poll:
	device_remove_file(&pdev->dev, &dev_attr_timed_standby);
exit_createfile_timedstandby:
	device_remove_file(&pdev->dev, &dev_attr_fadsuspend);
exit_createfile_fadsuspend:
	cpu_deinitialize();
exit_cpuinitialize:
	misc_deregister(&fad_miscdev);
exit:
	return ret;
}

static int fad_remove(struct platform_device *pdev)
{
	pr_debug("Removing FAD driver\n");
#ifdef CONFIG_OF
	unregister_pm_notifier(&gpDev->nb);
	device_remove_file(&pdev->dev, &dev_attr_charge_state);
#endif
	device_remove_file(&pdev->dev, &dev_attr_fadsuspend);
	device_remove_file(&pdev->dev, &dev_attr_timed_standby);
	if (gpDev->bHasTrigger)
		device_remove_file(&pdev->dev, &dev_attr_trigger_poll);
	sysfs_remove_group(&pdev->dev.kobj, &faddev_sysfs_attr_grp);
	cpu_deinitialize();
	misc_deregister(&fad_miscdev);
//      kfree(gpDev);
	return 0;
}

static int fad_suspend(struct platform_device *pdev, pm_message_t state)
{
	if (gpDev->suspend)
		gpDev->suspend(gpDev);

	return 0;
}

static int fad_resume(struct platform_device *pdev)
{
	if (gpDev->resume)
		gpDev->resume(gpDev);

	return 0;
}

static void fad_shutdown(struct platform_device *pdev)
{
	if (gpDev->suspend)
		gpDev->suspend(gpDev);

}

static struct platform_driver fad_driver = {
	.probe = fad_probe,
	.remove = fad_remove,
	.suspend = fad_suspend,
	.resume = fad_resume,
	.shutdown = fad_shutdown,
	.driver = {
		   .name = "fad",
		   .owner = THIS_MODULE,
		    },
};

/**
 * FAD_Init
 * Initializes FAD
 *
 *
 * @return
 */
static int __init FAD_Init(void)
{
	int retval;

#if KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE
	pdev = platform_device_alloc("fad", 1);
	platform_device_add(pdev);
#endif
	retval = platform_driver_register(&fad_driver);
	if (retval) {
		pr_err("flirdrv-fad: Error adding platform driver\n");
	}

	return retval;
}

/**
 * FAD_Deinit
 * Cleanup after FAD Init on module unload
 *
 * @return
 */
static void __exit FAD_Deinit(void)
{
#if KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE
	platform_device_del(pdev);
#endif
	platform_driver_unregister(&fad_driver);
}

/**
 * DOIOControl
 *
 * @param gpDev
 * @param Ioctl
 * @param pBuf
 * @param pUserBuf
 *
 * @return
 */
static int DoIOControl(PFAD_HW_INDEP_INFO gpDev,
		       DWORD Ioctl, PUCHAR pBuf, PUCHAR pUserBuf)
{
	int retval = ERROR_INVALID_PARAMETER;
//    static ULONG ulWdogTime = 5000;    // 5 seconds
	static BOOL bGPSEnable = FALSE;

	switch (Ioctl) {
	case IOCTL_FAD_SET_LASER_STATUS:
		if (!gpDev->bHasLaser)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(gpDev);
			gpDev->bLaserEnable =
			    ((PFADDEVIOCTLLASER) pBuf)->bLaserPowerEnabled;
			gpDev->pSetLaserStatus(gpDev, gpDev->bLaserEnable);
			retval = ERROR_SUCCESS;
			UNLOCK(gpDev);
		}
		break;

	case IOCTL_FAD_GET_LASER_STATUS:
		if (!gpDev->bHasLaser)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(gpDev);
			gpDev->pGetLaserStatus(gpDev, (PFADDEVIOCTLLASER) pBuf);
			retval = ERROR_SUCCESS;
			UNLOCK(gpDev);
		}
		break;

	case IOCTL_FAD_SET_LASER_MODE:
		if (!gpDev->bHasLaser || !gpDev->pSetLaserMode) {
			retval = ERROR_NOT_SUPPORTED;
		} else {
			gpDev->pSetLaserMode(gpDev,
					     (PFADDEVIOCTLLASERMODE) pBuf);
			retval = ERROR_SUCCESS;
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
			LOCK(gpDev);
			if ((pBuzzerData->eState == BUZZER_ON) ||
			    (pBuzzerData->eState == BUZZER_TIME)) {
				// Activate sound
				gpDev->pSetBuzzerFrequency(pBuzzerData->usFreq,
							   pBuzzerData->ucPWM);
			}
			if (pBuzzerData->eState == BUZZER_TIME) {
				UNLOCK(gpDev);
				msleep(pBuzzerData->usTime);
				LOCK(gpDev);
			}
			if ((pBuzzerData->eState == BUZZER_OFF) ||
			    (pBuzzerData->eState == BUZZER_TIME)) {
				// Switch off sound
				gpDev->pSetBuzzerFrequency(0, 0);
			}
			UNLOCK(gpDev);
			retval = ERROR_SUCCESS;
		}
		break;

	case IOCTL_FAD_GET_DIG_IO_STATUS:
		if (!gpDev->bHasDigitalIO)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(gpDev);
			gpDev->pGetDigitalStatus(gpDev,
						 (PFADDEVIOCTLDIGIO) pBuf);
			retval = ERROR_SUCCESS;
			UNLOCK(gpDev);
		}
		break;

	case IOCTL_FAD_GET_LED:
		if (!gpDev->bHasKAKALed)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(gpDev);
			gpDev->pGetLedState(gpDev, (PFADDEVIOCTLLED) pBuf);
			retval = ERROR_SUCCESS;
			UNLOCK(gpDev);
		}
		break;

	case IOCTL_FAD_SET_LED:
		if (!gpDev->bHasKAKALed)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(gpDev);
			gpDev->pSetLedState(gpDev, (PFADDEVIOCTLLED) pBuf);
			retval = ERROR_SUCCESS;
			UNLOCK(gpDev);
		}
		break;

	case IOCTL_FAD_GET_KAKA_LED:
		if (!gpDev->bHasKAKALed)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(gpDev);
			gpDev->pGetKAKALedState(gpDev, (PFADDEVIOCTLLED) pBuf);
			retval = ERROR_SUCCESS;
			UNLOCK(gpDev);
		}
		break;

	case IOCTL_FAD_SET_KAKA_LED:
		if (!gpDev->bHasKAKALed)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(gpDev);
			gpDev->pSetKAKALedState(gpDev, (PFADDEVIOCTLLED) pBuf);
			retval = ERROR_SUCCESS;
			UNLOCK(gpDev);
		}
		break;

	case IOCTL_FAD_SET_GPS_ENABLE:
		if (!gpDev->bHasGPS)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(gpDev);
			gpDev->
			    pSetGPSEnable(((PFADDEVIOCTLGPS)
					   pBuf)->bGPSEnabled);
			bGPSEnable = ((PFADDEVIOCTLGPS) pBuf)->bGPSEnabled;
			retval = ERROR_SUCCESS;
			UNLOCK(gpDev);
		}
		break;

	case IOCTL_FAD_GET_GPS_ENABLE:
		if (!gpDev->bHasGPS)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(gpDev);
			gpDev->
			    pGetGPSEnable(&
					  (((PFADDEVIOCTLGPS)
					    pBuf)->bGPSEnabled));
			retval = ERROR_SUCCESS;
			UNLOCK(gpDev);
		}
		break;

	case IOCTL_FAD_SET_LASER_ACTIVE:
		if (!gpDev->bHasLaser)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(gpDev);
			gpDev->pSetLaserActive(gpDev,
					       ((FADDEVIOCTLLASERACTIVE *)
						pBuf)->bLaserActive == TRUE);
			retval = ERROR_SUCCESS;
			UNLOCK(gpDev);
		}
		break;

	case IOCTL_FAD_GET_LASER_ACTIVE:
		if (!gpDev->bHasLaser)
			retval = ERROR_NOT_SUPPORTED;
		else {
			LOCK(gpDev);
			((FADDEVIOCTLLASERACTIVE *) pBuf)->bLaserActive =
			    gpDev->pGetLaserActive(gpDev);
			retval = ERROR_SUCCESS;
			UNLOCK(gpDev);
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
			    gpDev->pGetKeypadBacklight((FADDEVIOCTLBACKLIGHT *)
						       pBuf);
		}
		break;

	case IOCTL_FAD_SET_KP_BACKLIGHT:
		if (!gpDev->bHasKpBacklight)
			retval = ERROR_NOT_SUPPORTED;
		else {
			retval =
			    gpDev->pSetKeypadBacklight((FADDEVIOCTLBACKLIGHT *)
						       pBuf);
		}
		break;

	case IOCTL_FAD_GET_KP_SUBJ_BACKLIGHT:
		if (!gpDev->bHasKpBacklight)
			retval = ERROR_NOT_SUPPORTED;
		else {
			retval =
			    gpDev->pGetKeypadSubjBacklight(gpDev,
							   (FADDEVIOCTLSUBJBACKLIGHT
							    *) pBuf);
		}
		break;

	case IOCTL_FAD_SET_KP_SUBJ_BACKLIGHT:
		if (!gpDev->bHasKpBacklight)
			retval = ERROR_NOT_SUPPORTED;
		else {
			retval =
			    gpDev->pSetKeypadSubjBacklight(gpDev,
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
			pSecurity->ullUniqueID = system_serial_high;
			pSecurity->ullUniqueID <<= 32;
			pSecurity->ullUniqueID += system_serial_low;
			pSecurity->ulRequire30HzCFClevel = 0;
			pSecurity->ulRequiredConfigCFClevel = 0;
		}
		retval = ERROR_SUCCESS;
		break;

	case IOCTL_FAD_RELEASE_READ:
		gpDev->eEvent = FAD_RESET_EVENT;
		wake_up_interruptible(&gpDev->wq);
		break;

	default:
		pr_err("flirdrv-fad: FAD: Unsupported IOCTL code %lX\n", Ioctl);
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
	int retval = ERROR_SUCCESS;
	char *tmp;

	tmp = kzalloc(_IOC_SIZE(cmd), GFP_KERNEL);
	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		pr_debug("flirdrv-fad: FAD Ioctl %X copy from user: %d\n", cmd,
			 _IOC_SIZE(cmd));
		retval = copy_from_user(tmp, (void *)arg, _IOC_SIZE(cmd));
		if (retval)
			pr_err("flirdrv-fad: FAD Copy from user failed: %i\n",
			       retval);
	}

	if (retval == ERROR_SUCCESS) {
		pr_debug("flirdrv-fad: FAD Ioctl %X\n", cmd);
		retval = DoIOControl(gpDev, cmd, tmp, (PUCHAR) arg);
		if (retval && (retval != ERROR_NOT_SUPPORTED))
			pr_err("flirdrv-fad: FAD Ioctl failed: %X %i %d\n", cmd,
			       retval, _IOC_NR(cmd));
	}

	if ((retval == ERROR_SUCCESS) && (_IOC_DIR(cmd) & _IOC_READ)) {
		pr_debug("flirdrv-fad: FAD Ioctl %X copy to user: %u\n", cmd,
			 _IOC_SIZE(cmd));
		retval = copy_to_user((void *)arg, tmp, _IOC_SIZE(cmd));
		if (retval)
			pr_err("flirdrv-fad: FAD Copy to user failed: %i\n",
			       retval);
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
static unsigned int FadPoll(struct file *filp, poll_table *pt)
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
		       loff_t *f_pos)
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
MODULE_AUTHOR("Bo Svangård, FLIR Systems AB");
