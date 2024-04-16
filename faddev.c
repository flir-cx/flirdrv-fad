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
#include <linux/kernel.h>
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

static long standby_off_timer = 360;
module_param(standby_off_timer, long, 0);
MODULE_PARM_DESC(standby_off_timer,
		 "Standby-to-poweroff timer [min], must be >0");

static long standby_on_timer = 0;
module_param(standby_on_timer, long, 0);
MODULE_PARM_DESC(standby_on_timer,
		 "Standby-to-wakeup timer [min], overrides standby_off_timer, 0 to disable");

// Function prototypes
static long FAD_IOControl(struct file *filep, unsigned int cmd, unsigned long arg);
static unsigned int FadPoll(struct file *filep, poll_table *pt);
static ssize_t FadRead(struct file *filep, char __user *buf, size_t count, loff_t *f_pos);

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
	.unlocked_ioctl = FAD_IOControl,
	.read = FadRead,
	.poll = FadPoll,
};

#if (KERNEL_VERSION(3, 14, 0) <= LINUX_VERSION_CODE && KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE) || (KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE)

// get_suspend_wakup_source is implemented in evander/lennox 3.14.x & 5.10.y kernels
struct wakeup_source *get_suspend_wakup_source(void);
#warning "we use 3.14.x & 5.10.y kernels get_suspend_wakeup_source"

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
static int cpu_initialize(struct device *dev)
{
	int retval;
	struct faddata *data = dev_get_drvdata(dev);

#ifdef CONFIG_OF
	if (of_machine_is_compatible("fsl,imx6dl-ec501"))
		data->pDev.bHasKAKALed = TRUE;

	if (of_machine_is_compatible("flir,ninjago") ||
	    of_machine_is_compatible("fsl,imx6dl-ec101") ||
	    of_machine_is_compatible("fsl,imx6dl-ec701") ||
	    of_machine_is_compatible("fsl,imx6dl-ec501")) {
		retval = SetupMX6Platform(&data->pDev);
	} else if (of_machine_is_compatible("fsl,imx6q")) {
		data->pDev.node = of_find_compatible_node(NULL, NULL, "flir,fad");
		retval = SetupMX6Q(&data->pDev);
	} else
#endif
		if (cpu_is_imx6s()) {
			data->pDev.bHasDigitalIO = TRUE;
			data->pDev.bHasKAKALed = TRUE;
			retval = SetupMX6S(&data->pDev);
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
static void cpu_deinitialize(struct device *dev)
{
	struct faddata *data = dev_get_drvdata(dev);

#ifdef CONFIG_OF
	if (of_machine_is_compatible("flir,ninjago") ||
	    of_machine_is_compatible("fsl,imx6dl-ec101") ||
	    of_machine_is_compatible("fsl,imx6dl-ec501")) {
		InvSetupMX6Platform(&data->pDev);
	} else if (of_machine_is_compatible("fsl,imx6q")) {
		of_node_put(data->pDev.node);
		InvSetupMX6Q(&data->pDev);
	} else
#endif
		if (cpu_is_imx6s()) {
			InvSetupMX6S(&data->pDev);
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
		strcpy(buf, "suspend\n");
		break;

	case ON_STATE:
		strcpy(buf, "run\n");
		break;

	case USB_CHARGE_STATE:
		strcpy(buf, "charge\n");
		break;

	default:
		strcpy(buf, "unknown\n");
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
	struct faddata *data = dev_get_drvdata(dev);
	
	if (data->pDev.bSuspend) {
		if ((len == 1) && (*buf == '1'))
			data->pDev.bSuspend = 0;
		else
			pr_err("App standby prepare fail %d %c\n", len,
			       (len > 0) ? *buf : ' ');
		complete(&data->pDev.standbyComplete);
	} else
		pr_debug("FAD: App resume\n");

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

	sysfs_notify(&dev->kobj, "control", "fadsuspend");
	return len;
}

static ssize_t standby_off_timer_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	return sprintf(buf, "%lu\n", standby_off_timer);
}

static ssize_t standby_off_timer_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	long val;
	int ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		pr_err("FAD: Poweroff timer conversion error\n");
		return ret;
	}

	if (val > 0) {
		pr_debug("FAD: Power-off timer set to %lu minutes", val);
		standby_off_timer = val;
		ret = len;
	} else {
		pr_err("FAD: Timer value must be >0\n");
		ret = -EINVAL;
	}
	return ret;
}

static ssize_t standby_on_timer_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	return sprintf(buf, "%lu\n", standby_on_timer);
}

static ssize_t standby_on_timer_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t len)
{
	long val;
	int ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		pr_err("FAD: Wakeup timer conversion error\n");
		return ret;
	}

	if (val >= 0) {
		pr_debug("FAD: Wakeup timer set to %lu minutes\n", val);
		standby_on_timer = val;
		ret = len;
	} else {
		pr_err("FAD: Wakeup timer value must be >=0\n");
		ret = -EINVAL;
	}
	return ret;
}


static ssize_t chargersuspend_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t len)
{
	int ret = 0;
	struct faddata *data = dev_get_drvdata(dev);

	if (data->pDev.pSetChargerSuspend != NULL) {
		if (strncmp(buf, "1", 1) == 0) {
			data->pDev.pSetChargerSuspend(&data->pDev, true);
			ret = len;
		} else if (strncmp(buf, "0", 1) == 0) {
			data->pDev.pSetChargerSuspend(&data->pDev, false);
			ret = len;
		} else {
			pr_err ("chargersuspend unknown command... 1/0 accepted\n");
			ret = -EINVAL;
		}
	}
	return ret;
}

static ssize_t trigger_poll_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	strcpy(buf, "X\n");
	return strlen(buf);
}

static DEVICE_ATTR(standby_off_timer, 0644, standby_off_timer_show, standby_off_timer_store);
static DEVICE_ATTR(standby_on_timer, 0644, standby_on_timer_show, standby_on_timer_store);
static DEVICE_ATTR(charge_state, 0644, charge_state_show, charge_state_store);
static DEVICE_ATTR(fadsuspend, 0644, fadsuspend_show, fadsuspend_store);
static DEVICE_ATTR(chargersuspend, 0644, NULL, chargersuspend_store);
static DEVICE_ATTR(trigger_poll, 0444, trigger_poll_show, NULL);

static struct attribute *faddev_sysfs_attrs[] = {
	&dev_attr_standby_off_timer.attr,
	&dev_attr_standby_on_timer.attr,
	&dev_attr_charge_state.attr,
	&dev_attr_fadsuspend.attr,
	&dev_attr_chargersuspend.attr,
	&dev_attr_trigger_poll.attr,
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
	if (!ws) {
		pr_err("FAD: No suspend wakeup source\n");
		return UNKNOWN_WAKE;
	}
	pr_debug("FAD: Resume wakeup source '%s'\n", ws->name);

	if (strstr(ws->name, "onkey"))
		return ON_OFF_BUTTON_WAKE;

	if (strstr(ws->name, "wake"))
		return USB_CABLE_WAKE;

	if (strstr(ws->name, "rtc")) {
		if (!standby_on_timer) {
			pr_info("FAD: Poweroff after %lu min standby\n", standby_off_timer);
			orderly_poweroff(1);
			return UNKNOWN_WAKE;
		} else {
			pr_info("FAD: Wakeup after %lu min standby\n", standby_on_timer);
			return ON_OFF_BUTTON_WAKE;
		}
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
	struct faddata *data = container_of(alarm->data, struct faddata, alarm);

	pr_debug("FAD: Standby timeout, powering off");

	// Switch of backlight as fast as possible (just activated in early resume)
	if (data->pDev.backlight) {
		data->pDev.backlight->props.power = 4;
		data->pDev.backlight->props.brightness = 0;
		backlight_update_status(data->pDev.backlight);
	}
	// Actual switch off will be done in get_wake_reason() when resume finished
	return ALARMTIMER_NORESTART;
}

/** Wake up camera after 1 minute in (timed) standby */
enum alarmtimer_restart fad_standby_wakeup(struct alarm *alarm, ktime_t kt)
{
	pr_debug("FAD: Standby wakeup, resuming");

	return ALARMTIMER_NORESTART;
}

static int fad_notify(struct notifier_block *nb, unsigned long val, void *ign)
{
	ktime_t kt;
	unsigned long jifs;
	struct platform_device *pdev = gpDev->pLinuxDevice;
	struct faddata *data = platform_get_drvdata(pdev);
	struct device *dev = data->dev;
	
	switch (val) {
	case PM_SUSPEND_PREPARE:
		// Make appcore enter standby
		power_state = SUSPEND_STATE;
		data->pDev.bSuspend = 1;
		sysfs_notify(&dev->kobj, "control", "fadsuspend");

		if (standby_on_timer) {
			alarm_init(&data->alarm, ALARM_REALTIME, &fad_standby_wakeup);
			kt = ktime_set(60 * standby_on_timer, 0);
		} else {
			alarm_init(&data->alarm, ALARM_REALTIME, &fad_standby_timeout);
			kt = ktime_set(60 * standby_off_timer, 0);
		}
		pr_debug("FAD: SUSPEND %lu min\n", (long int)ktime_divns(kt, NSEC_PER_SEC) / 60);
		alarm_start_relative(&data->alarm, kt);

		// Wait for appcore
		jifs = wait_for_completion_timeout(&data->pDev.standbyComplete,
						   msecs_to_jiffies(10000));
		if (!jifs) {
			pr_debug("FAD: Timeout waiting for standby completion\n");
		}
		if (data->pDev.bSuspend) {
			pr_err("FAD: Application suspend failed\n");
			return NOTIFY_BAD;
		}
		return NOTIFY_OK;

	case PM_POST_SUSPEND:
		pr_debug("FAD: POST_SUSPEND\n");
		if (get_wake_reason() == USB_CABLE_WAKE)
			power_state = USB_CHARGE_STATE;
		else
			power_state = ON_STATE;

		data->pDev.bSuspend = 0;
		alarm_cancel(&data->alarm);
		sysfs_notify(&dev->kobj, "control", "fadsuspend");
		return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}
#endif

static int fad_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct faddata *data;

	pr_debug("Probing FAD driver\n");

	data = devm_kzalloc(dev, sizeof(struct faddata), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	gpDev = &data->pDev;
	data->miscdev.minor = MISC_DYNAMIC_MINOR;
	data->miscdev.name = devm_kasprintf(dev, GFP_KERNEL, "fad0");
	data->miscdev.fops = &fad_fops;
	data->miscdev.parent = dev;

	data->dev = dev;
	dev_set_drvdata(dev, data);
	platform_set_drvdata(pdev, data);
	data->pDev.pLinuxDevice = pdev;

	ret = misc_register(&data->miscdev);
	if (ret) {
		pr_err("Failed to register miscdev for FAD driver\n");
		goto exit;
	}
	// initialize this device instance
	sema_init(&data->pDev.semDevice, 1);

	// init wait queue
	init_waitqueue_head(&data->pDev.wq);

	// Set up CPU specific stuff
	ret = cpu_initialize(dev);
	if (ret < 0) {
		pr_err("flirdrv-fad: Failed to initialize CPU\n");
		goto exit_cpuinitialize;
	}

	ret = sysfs_create_group(&dev->kobj, &faddev_sysfs_attr_grp);
	if (ret) {
		pr_err("FADDEV Error creating sysfs grp control\n");
		goto exit_sysfs_create_group;
	}

#ifdef CONFIG_OF
	data->pDev.nb.notifier_call = fad_notify;
	data->pDev.nb.priority = 0;
	ret = register_pm_notifier(&data->pDev.nb);
	if (ret) {
		pr_err("FADDEV Error creating sysfs grp control\n");
		goto exit_register_pm_notifier;
	}
#endif
	init_completion(&data->pDev.standbyComplete);
	standby_off_timer = data->pDev.standbyMinutes;	
	return ret;

#ifdef CONFIG_OF
	unregister_pm_notifier(&data->pDev.nb);
exit_register_pm_notifier:
#endif
	sysfs_remove_group(&dev->kobj, &faddev_sysfs_attr_grp);
exit_sysfs_create_group:
	cpu_deinitialize(dev);
exit_cpuinitialize:
	misc_deregister(&data->miscdev);
exit:
	return ret;
}

static int fad_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct faddata *data = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "Removing FAD driver\n");
#ifdef CONFIG_OF
	unregister_pm_notifier(&data->pDev.nb);
#endif
	sysfs_remove_group(&dev->kobj, &faddev_sysfs_attr_grp);
	cpu_deinitialize(dev);
	misc_deregister(&data->miscdev);
	return 0;
}

static int fad_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct faddata *data = platform_get_drvdata(pdev);
	
	if (data->pDev.suspend)
		data->pDev.suspend(&data->pDev);

	return 0;
}

static int fad_resume(struct platform_device *pdev)
{
	struct faddata *data = platform_get_drvdata(pdev);
	
	if (data->pDev.resume)
		data->pDev.resume(&data->pDev);
	return 0;
}

static void fad_shutdown(struct platform_device *pdev)
{
	struct faddata *data = platform_get_drvdata(pdev);
	if (data->pDev.suspend)
		data->pDev.suspend(&data->pDev);

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
 * DOIOControl
 *
 * @param gpDev
 * @param Ioctl
 * @param pBuf
 * @param pUserBuf
 *
 * @return
 */
static int DoIOControl(struct device *dev, DWORD Ioctl, PUCHAR pBuf, PUCHAR pUserBuf)
{
	struct faddata *data = dev_get_drvdata(dev);
	int retval;
	//    static ULONG ulWdogTime = 5000;    // 5 seconds
	static BOOL bGPSEnable = FALSE;

	switch (Ioctl) {
	case IOCTL_FAD_SET_LASER_STATUS:
		if (!data->pDev.bHasLaser)
			retval = ERROR_NOT_SUPPORTED;
		else {
			down(&data->pDev.semDevice);
			data->pDev.bLaserEnable = ((PFADDEVIOCTLLASER) pBuf)->bLaserPowerEnabled;
			data->pDev.pSetLaserStatus(&data->pDev, data->pDev.bLaserEnable);
			retval = ERROR_SUCCESS;
			up(&data->pDev.semDevice);
		}
		break;

	case IOCTL_FAD_GET_LASER_STATUS:
		if (!data->pDev.bHasLaser)
			retval = ERROR_NOT_SUPPORTED;
		else {
			down(&data->pDev.semDevice);
			data->pDev.pGetLaserStatus(&data->pDev, (PFADDEVIOCTLLASER) pBuf);
			retval = ERROR_SUCCESS;
			up(&data->pDev.semDevice);
		}
		break;

	case IOCTL_FAD_SET_LASER_MODE:
		if (!data->pDev.bHasLaser || !data->pDev.pSetLaserMode) {
			retval = ERROR_NOT_SUPPORTED;
		} else {
			data->pDev.pSetLaserMode(&data->pDev, (PFADDEVIOCTLLASERMODE) pBuf);
			retval = ERROR_SUCCESS;
		}
		break;

	case IOCTL_SET_APP_EVENT:
		retval = ERROR_SUCCESS;
		break;

	case IOCTL_FAD_BUZZER:
		if (!data->pDev.bHasBuzzer)
			retval = ERROR_NOT_SUPPORTED;
		else {
			FADDEVIOCTLBUZZER *pBuzzerData =
			    (FADDEVIOCTLBUZZER *) pBuf;
			down(&data->pDev.semDevice);
			if ((pBuzzerData->eState == BUZZER_ON) ||
			    (pBuzzerData->eState == BUZZER_TIME)) {
				// Activate sound
				data->pDev.pSetBuzzerFrequency(pBuzzerData->usFreq,
							   pBuzzerData->ucPWM);
			}
			if (pBuzzerData->eState == BUZZER_TIME) {
				up(&data->pDev.semDevice);
				msleep(pBuzzerData->usTime);
				down(&data->pDev.semDevice);
			}
			if ((pBuzzerData->eState == BUZZER_OFF) ||
			    (pBuzzerData->eState == BUZZER_TIME)) {
				// Switch off sound
				data->pDev.pSetBuzzerFrequency(0, 0);
			}
			up(&data->pDev.semDevice);
			retval = ERROR_SUCCESS;
		}
		break;

	case IOCTL_FAD_GET_DIG_IO_STATUS:
		if (!data->pDev.bHasDigitalIO)
			retval = ERROR_NOT_SUPPORTED;
		else {
			down(&data->pDev.semDevice);
			data->pDev.pGetDigitalStatus(&data->pDev,
						 (PFADDEVIOCTLDIGIO) pBuf);
			retval = ERROR_SUCCESS;
			up(&data->pDev.semDevice);
		}
		break;

	case IOCTL_FAD_GET_LED:
		if (!data->pDev.bHasKAKALed)
			retval = ERROR_NOT_SUPPORTED;
		else {
			down(&data->pDev.semDevice);
			data->pDev.pGetLedState(&data->pDev, (PFADDEVIOCTLLED) pBuf);
			retval = ERROR_SUCCESS;
			up(&data->pDev.semDevice);
		}
		break;

	case IOCTL_FAD_SET_LED:
		if (!data->pDev.bHasKAKALed)
			retval = ERROR_NOT_SUPPORTED;
		else {
			down(&data->pDev.semDevice);
			data->pDev.pSetLedState(&data->pDev, (PFADDEVIOCTLLED) pBuf);
			retval = ERROR_SUCCESS;
			up(&data->pDev.semDevice);
		}
		break;

	case IOCTL_FAD_GET_KAKA_LED:
		if (!data->pDev.bHasKAKALed)
			retval = ERROR_NOT_SUPPORTED;
		else {
			down(&data->pDev.semDevice);
			data->pDev.pGetKAKALedState(&data->pDev, (PFADDEVIOCTLLED) pBuf);
			retval = ERROR_SUCCESS;
			up(&data->pDev.semDevice);
		}
		break;

	case IOCTL_FAD_SET_KAKA_LED:
		if (!data->pDev.bHasKAKALed)
			retval = ERROR_NOT_SUPPORTED;
		else {
			down(&data->pDev.semDevice);
			data->pDev.pSetKAKALedState(&data->pDev, (PFADDEVIOCTLLED) pBuf);
			retval = ERROR_SUCCESS;
			up(&data->pDev.semDevice);
		}
		break;

	case IOCTL_FAD_SET_GPS_ENABLE:
		if (!data->pDev.bHasGPS)
			retval = ERROR_NOT_SUPPORTED;
		else {
			down(&data->pDev.semDevice);
			data->pDev.pSetGPSEnable(((PFADDEVIOCTLGPS)pBuf)->bGPSEnabled);
			bGPSEnable = ((PFADDEVIOCTLGPS) pBuf)->bGPSEnabled;
			retval = ERROR_SUCCESS;
			up(&data->pDev.semDevice);
		}
		break;

	case IOCTL_FAD_GET_GPS_ENABLE:
		if (!data->pDev.bHasGPS)
			retval = ERROR_NOT_SUPPORTED;
		else {
			down(&data->pDev.semDevice);
			data->pDev.pGetGPSEnable(&(((PFADDEVIOCTLGPS)pBuf)->bGPSEnabled));
			retval = ERROR_SUCCESS;
			up(&data->pDev.semDevice);
		}
		break;

	case IOCTL_FAD_SET_LASER_ACTIVE:
		if (!data->pDev.bHasLaser)
			retval = ERROR_NOT_SUPPORTED;
		else {
			down(&data->pDev.semDevice);
			data->pDev.pSetLaserActive(&data->pDev,
					       ((FADDEVIOCTLLASERACTIVE *)
						pBuf)->bLaserActive == TRUE);
			retval = ERROR_SUCCESS;
			up(&data->pDev.semDevice);
		}
		break;

	case IOCTL_FAD_GET_LASER_ACTIVE:
		if (!data->pDev.bHasLaser)
			retval = ERROR_NOT_SUPPORTED;
		else {
			down(&data->pDev.semDevice);
			((FADDEVIOCTLLASERACTIVE *) pBuf)->bLaserActive =
			    data->pDev.pGetLaserActive(&data->pDev);
			retval = ERROR_SUCCESS;
			up(&data->pDev.semDevice);
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
		if (!data->pDev.bHasKpBacklight)
			retval = ERROR_NOT_SUPPORTED;
		else {
			retval =
			    data->pDev.pGetKeypadBacklight((FADDEVIOCTLBACKLIGHT *)
						       pBuf);
		}
		break;

	case IOCTL_FAD_SET_KP_BACKLIGHT:
		if (!data->pDev.bHasKpBacklight)
			retval = ERROR_NOT_SUPPORTED;
		else {
			retval =
			    data->pDev.pSetKeypadBacklight((FADDEVIOCTLBACKLIGHT *)
						       pBuf);
		}
		break;

	case IOCTL_FAD_GET_KP_SUBJ_BACKLIGHT:
		if (!data->pDev.bHasKpBacklight)
			retval = ERROR_NOT_SUPPORTED;
		else {
			retval =
			    data->pDev.pGetKeypadSubjBacklight(&data->pDev,
							   (FADDEVIOCTLSUBJBACKLIGHT
							    *) pBuf);
		}
		break;

	case IOCTL_FAD_SET_KP_SUBJ_BACKLIGHT:
		if (!data->pDev.bHasKpBacklight)
			retval = ERROR_NOT_SUPPORTED;
		else {
			retval =
			    data->pDev.pSetKeypadSubjBacklight(&data->pDev,
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
		data->pDev.eEvent = FAD_RESET_EVENT;
		wake_up_interruptible(&data->pDev.wq);
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
		retval = DoIOControl(&gpDev->pLinuxDevice->dev, cmd, tmp, (PUCHAR) arg);
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
static unsigned int FadPoll(struct file *filep, poll_table *pt)
{
	struct faddata *data = container_of(filep->private_data, struct faddata, miscdev);
	
	poll_wait(filep, &data->pDev.wq, pt);
	return (data->pDev.eEvent != FAD_NO_EVENT) ? (POLLIN | POLLRDNORM) : 0;
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
static ssize_t FadRead(struct file *filep, char *buf, size_t count,
		       loff_t *f_pos)
{
	struct faddata *data = container_of(filep->private_data, struct faddata, miscdev);
	int res;

	if (count < 1)
		return -EINVAL;
	res = wait_event_interruptible(data->pDev.wq, data->pDev.eEvent != FAD_NO_EVENT);
	if (res < 0)
		return res;
	res = copy_to_user((void *)buf,  &data->pDev.eEvent, 1);
	if (res < 0) {
		pr_err("FAD copy-to-user failed: %i\n", res);
	}
	data->pDev.eEvent = FAD_NO_EVENT;
	return 1;
}

module_platform_driver(fad_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FLIR Application Driver");
MODULE_AUTHOR("Peter Fitger, FLIR Systems AB");
MODULE_AUTHOR("Bo Svangård, FLIR Systems AB");
