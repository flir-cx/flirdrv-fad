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

#include "../fvd/flir_kernel_os.h"
#include "faddev.h"
#include "i2cdev.h"
#include "fad_internal.h"
#include <linux/platform_device.h>
#include <linux/i2c.h>

DWORD g_RestartReason = RESTART_REASON_NOT_SET;

// Function prototypes
static int FAD_IOControl(struct inode *inode, struct file *filep,
		unsigned int cmd, unsigned long arg);
static ssize_t dummyWrite (struct file *filp, const char __user  *buf, size_t count, loff_t *f_pos);
static ssize_t dummyRead (struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
static int dummyRelease (struct inode *inode, struct file *filp);
static int dummyOpen (struct inode *inode, struct file *filp);

static PFAD_HW_INDEP_INFO gpDev;

static struct file_operations fad_fops =
{
		.owner = THIS_MODULE,
		.ioctl = FAD_IOControl,
		.write = dummyWrite,
		.read = dummyRead,
		.open = dummyOpen,
		.release = dummyRelease,
};

static struct resource fad_resources[] = {
	{
		.start = MXC_INT_UART5,
		.flags = IORESOURCE_IRQ,
	},
};

// Code
static int __init FAD_Init(void)
{
	int i;

    pr_err("FAD_Init\n");

    // Check that we are not already initiated
    if (gpDev) {
    	pr_err("FAD already initialized\n");
        return 0;
    }

    // Allocate (and zero-initiate) our control structure.
    gpDev = (PFAD_HW_INDEP_INFO)kmalloc(sizeof(FAD_HW_INDEP_INFO), GFP_KERNEL);
    if ( !gpDev ) {
    	pr_err("Error allocating memory for pDev, FAD_Init failed\n");
        return -2;
    }

    // Reset all data
    memset (gpDev, 0, sizeof(*gpDev));

    // Register linux driver
    alloc_chrdev_region(&gpDev->fad_dev, 0, 1, "fad");
    cdev_init(&gpDev->fad_cdev, &fad_fops);
    gpDev->fad_cdev.owner = THIS_MODULE;
    gpDev->fad_cdev.ops = &fad_fops;
    i = cdev_add(&gpDev->fad_cdev, gpDev->fad_dev, 1);
    if (i)
    {
    	pr_err("Error adding device driver\n");
        return -3;
    }
    gpDev->pLinuxDevice = platform_device_alloc("fad", 1);
    if (gpDev->pLinuxDevice == NULL)
    {
    	pr_err("Error adding allocating device\n");
        return -4;
    }
    platform_device_add_resources(gpDev->pLinuxDevice, fad_resources,
    							  ARRAY_SIZE(fad_resources));
    platform_device_add(gpDev->pLinuxDevice);
	pr_err("FAD driver device id %d.%d added\n", MAJOR(gpDev->fad_dev), MINOR(gpDev->fad_dev));

	// initialize this device instance
    sema_init(&gpDev->semDevice, 1);
    sema_init(&gpDev->semIOport, 1);

    gpDev->hI2C1 = i2c_get_adapter(0);
    gpDev->hI2C2 = i2c_get_adapter(1);

    pr_err("I2C drivers %p and %p\n", gpDev->hI2C1, gpDev->hI2C2);

	// Init hardware
	initHW(gpDev);

    // Set up Laser IRQ
    if (BspHasLaser())
    {
        if (InitLaserIrq(gpDev) == FALSE)
        {
            return -6;
        }
    }

    // Set up Digital I/O IRQ
	if (BspHasDigitalIO())
    {
        if (InitDigitalIOIrq(gpDev) == FALSE)
        {
            return -8;
        }
    }

	// Set up HDMI Active IRQ
    if (BspHasHdmi())
    {
        if (InitHdmiIrq(gpDev) == FALSE)
        {
            return -9;
        }
    }

	return 0;
}

static void __devexit FAD_Deinit(void)
{
    pr_err("FAD_Deinit\n");

    // make sure this is a valid context
    // if the device is running, stop it
    if (gpDev != NULL)
    {
        unregister_chrdev_region(gpDev->fad_dev, 1);
    	platform_device_unregister(gpDev->pLinuxDevice);
       	kfree(gpDev);
		gpDev = NULL;
    }
}

#ifdef NOT_YET
static DWORD WatchdogThread(LPVOID lpParameter)
{
    PFAD_HW_INDEP_INFO pInfo = lpParameter;

    while (TRUE)
    {
    	msleep(10000);
        WdogService(pInfo);
    }
    return -1;
}

static void StartWatchdogThread(PFAD_HW_INDEP_INFO pInfo)
{
    static HANDLE hThread = INVALID_HANDLE_VALUE;
    if (hThread == INVALID_HANDLE_VALUE)
    {
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)WatchdogThread, pInfo, 0, NULL);
    }
}
#endif

static DWORD DoIOControl(PFAD_HW_INDEP_INFO pInfo,
                         DWORD  Ioctl,
                         PUCHAR pBuf,
                         PUCHAR pUserBuf)
{
    DWORD  dwErr = ERROR_INVALID_PARAMETER;
//    static ULONG ulWdogTime = 5000;    // 5 seconds
//    static BOOL bGPSEnable = FALSE;

    switch (Ioctl) 
	{
		case IOCTL_FAD_SET_LASER_STATUS:
            if (!BspHasLaser())
                dwErr = ERROR_NOT_SUPPORTED;
            else
            {
			    LOCK(pInfo);
                pInfo->bLaserEnable = ((PFADDEVIOCTLLASER)pBuf)->bLaserPowerEnabled;
                setLaserStatus(pInfo, pInfo->bLaserEnable);
    			dwErr = ERROR_SUCCESS;
    			UNLOCK(pInfo);
			}
            break;

		case IOCTL_FAD_GET_LASER_STATUS:   
            if (!BspHasLaser())
                dwErr = ERROR_NOT_SUPPORTED;
			else
            {
			    LOCK(pInfo);
				getLaserStatus(pInfo, (PFADDEVIOCTLLASER)pBuf);
				dwErr = ERROR_SUCCESS;
    			UNLOCK(pInfo);
            }
            break;

        case IOCTL_SET_APP_EVENT:
			dwErr = ERROR_SUCCESS;
            break;

#ifdef NOT_YET
		case IOCTL_FAD_SET_TORCH_STATUS:
            dwErr = ERROR_NOT_SUPPORTED;
            break;

        case IOCTL_FAD_GET_TORCH_STATUS:   
            dwErr = ERROR_NOT_SUPPORTED;
            break;

        // Old IOCTLs moved to FPGA (FVD driver)
        case IOCTL_FAD_GET_SHUTTER_STATUS:
        case IOCTL_FAD_SET_SHUTTER_STATUS:
			dwErr = ERROR_NOT_SUPPORTED;
            break;

        // Code intended for Temp ADUC but used for Cooler ADUC
		case IOCTL_FAD_RESET_TEMPCPU:
            dwErr = ERROR_NOT_SUPPORTED;
            break;

        // Code intended for Temp ADUC but used for Cooler ADUC
        case IOCTL_FAD_RESTART_TEMPCPU:
            dwErr = ERROR_NOT_SUPPORTED;
            break;

        case IOCTL_FAD_AQUIRE_FILE_ACCESS:   
            dwErr = ERROR_NOT_SUPPORTED;
            break;

		case IOCTL_FAD_RELEASE_FILE_ACCESS: 
            dwErr = ERROR_NOT_SUPPORTED;
			break;
#endif

        case IOCTL_FAD_BUZZER:
            if (!BspHasBuzzer())
                dwErr = ERROR_NOT_SUPPORTED;
            else
			{
                FADDEVIOCTLBUZZER *pBuzzerData = (FADDEVIOCTLBUZZER *)pBuf;
                LOCK(pInfo);
				if ((pBuzzerData->eState == BUZZER_ON) ||
					(pBuzzerData->eState == BUZZER_TIME))
				{
					// Activate sound
					SetBuzzerFrequency(pBuzzerData->usFreq, pBuzzerData->ucPWM);
				}
				if (pBuzzerData->eState == BUZZER_TIME)
				{
					UNLOCK(pInfo);
					msleep(pBuzzerData->usTime);
					LOCK(pInfo);
				}
				if ((pBuzzerData->eState == BUZZER_OFF) ||
					(pBuzzerData->eState == BUZZER_TIME))
				{
					// Switch off sound
					SetBuzzerFrequency(0, 0);
				}
    			UNLOCK(pInfo);
                dwErr = ERROR_SUCCESS;
            }
            break;

#ifdef NOT_YET
        case IOCTL_FAD_7173_GET_MODE:
            dwErr = ERROR_NOT_SUPPORTED;
            break;

        case IOCTL_FAD_7173_SET_MODE:
            dwErr = ERROR_NOT_SUPPORTED;
            break;

        case IOCTL_FAD_GET_LCD_STATUS:
            dwErr = ERROR_NOT_SUPPORTED;
            break;

        case IOCTL_FAD_TRIG_OPTICS:
            dwErr = ERROR_NOT_SUPPORTED;
            break;

        // IrDA no longer used
        case IOCTL_FAD_DISABLE_IRDA:
        case IOCTL_FAD_ENABLE_IRDA:
			dwErr = ERROR_NOT_SUPPORTED;
            break;

        case IOCTL_FAD_GET_COMPASS:
			dwErr = ERROR_NOT_SUPPORTED;
            break;
#endif

#ifdef NOT_YET
        // A-camera generic IO
        case IOCTL_FAD_GET_DIG_IO_STATUS:
            if (!BspHasDigitalIO())
                dwErr = ERROR_NOT_SUPPORTED;
			else if (pOutBuf != NULL 
				&& OutBufLen == sizeof(FADDEVIOCTLDIGIO))
            {
			    LOCK(pInfo);
				__try 
				{
    				getDigitalStatus((PFADDEVIOCTLDIGIO)pOutBuf);
    				dwErr = ERROR_SUCCESS;
				}
				__except(EXCEPTION_EXECUTE_HANDLER) 
				{
            		ASSERT(FALSE);
                    dwErr = ERROR_EXCEPTION_IN_SERVICE;
				}
    			UNLOCK(pInfo);
                if (pdwBytesTransferred != NULL)
                    *pdwBytesTransferred = sizeof(FADDEVIOCTLDIGIO);
            }
            break;

		case IOCTL_FAD_SET_DIG_IO_STATUS:
			dwErr = ERROR_NOT_SUPPORTED;
            break;

		case IOCTL_FAD_ENABLE_WATCHDOG:
			if (pInBuf != NULL 
				&& InBufLen == sizeof(FADDEVIOCTLWDOG))
			{
				__try 
				{
                    FADDEVIOCTLWDOG *pWdogData = (FADDEVIOCTLWDOG *)pInBuf;
                    if (pWdogData->bEnable)
                    {
                        if (pWdogData->usTimeMs)
                            ulWdogTime = pWdogData->usTimeMs / 500;     // Convert to 2 Hz
                        WdogInit(pInfo, ulWdogTime);
                    }
                    else
                    {
                        WdogInit(pInfo, 255);   // Longest possible time is 127.5 seconds
                        StartWatchdogThread(pInfo);
                    }
    		        dwErr = ERROR_SUCCESS;
                }
				__except(EXCEPTION_EXECUTE_HANDLER) 
				{
            		ASSERT(FALSE);
                    dwErr = ERROR_EXCEPTION_IN_SERVICE;
				}
            }
            break;

		case IOCTL_FAD_TRIG_WATCHDOG:
            WdogService(pInfo);
   		    dwErr = ERROR_SUCCESS;
            break;

        // Pleora GigE board power enable
        case IOCTL_FAD_ENABLE_PT1000:
            dwErr = ERROR_NOT_SUPPORTED;
            break;

		case IOCTL_FAD_GET_LED:
            dwErr = ERROR_NOT_SUPPORTED;
            break;

        case IOCTL_FAD_SET_LED:
            dwErr = ERROR_NOT_SUPPORTED;
            break;

		case IOCTL_FAD_GET_KAKA_LED:
            dwErr = ERROR_NOT_SUPPORTED;
            break;

        case IOCTL_FAD_SET_KAKA_LED:
            dwErr = ERROR_NOT_SUPPORTED;
            break;

	    case IOCTL_FAD_SET_GPS_ENABLE:
            if (!BspHasGPS())
                dwErr = ERROR_NOT_SUPPORTED;
		    else if (pInBuf != NULL 
			    && InBufLen == sizeof(FADDEVIOCTLGPS))
            {
		        LOCK(pInfo);
				__try 
				{
    			    setGPSEnable(((PFADDEVIOCTLGPS) pInBuf)->bGPSEnabled);
	    		    bGPSEnable = ((PFADDEVIOCTLGPS) pInBuf)->bGPSEnabled;
    			    dwErr = ERROR_SUCCESS;
                }
				__except(EXCEPTION_EXECUTE_HANDLER) 
				{
            		ASSERT(FALSE);
                    dwErr = ERROR_EXCEPTION_IN_SERVICE;
				}
				UNLOCK(pInfo);
		    }
            break;

	    case IOCTL_FAD_GET_GPS_ENABLE:   
            if (!BspHasGPS())
                dwErr = ERROR_NOT_SUPPORTED;
		    else if (pOutBuf != NULL 
			    && OutBufLen == sizeof(FADDEVIOCTLGPS))
            {
		        LOCK(pInfo);
				__try 
				{
    			    getGPSEnable(&(((PFADDEVIOCTLGPS) pOutBuf)->bGPSEnabled));
                    if (pdwBytesTransferred != NULL)
                        *pdwBytesTransferred = sizeof(FADDEVIOCTLGPS);
	    		    dwErr = ERROR_SUCCESS;
                }
				__except(EXCEPTION_EXECUTE_HANDLER) 
				{
            		ASSERT(FALSE);
                    dwErr = ERROR_EXCEPTION_IN_SERVICE;
				}
			    UNLOCK(pInfo);
            }
            break;
#endif

        case IOCTL_FAD_SET_LASER_ACTIVE:
            if (!BspHasLaser())
                dwErr = ERROR_NOT_SUPPORTED;
            else
            {
			    LOCK(pInfo);
				SetLaserActive(pInfo, ((FADDEVIOCTLLASERACTIVE *)pBuf)->bLaserActive == TRUE);
				dwErr = ERROR_SUCCESS;
    			UNLOCK(pInfo);
			}
            break;

        case IOCTL_FAD_GET_LASER_ACTIVE:
            if (!BspHasLaser())
                dwErr = ERROR_NOT_SUPPORTED;
			else
            {
			    LOCK(pInfo);
				((FADDEVIOCTLLASERACTIVE *)pBuf)->bLaserActive = GetLaserActive(pInfo);
				dwErr = ERROR_SUCCESS;
    			UNLOCK(pInfo);
            }
            break;

        case IOCTL_FAD_GET_HDMI_STATUS:
            if (!BspHasHdmi())
                dwErr = ERROR_NOT_SUPPORTED;
		    else
		    {
			    LOCK(pInfo);
				getHdmiStatus(pInfo, (PFADDEVIOCTLHDMI)pBuf);
				dwErr = ERROR_SUCCESS;
    		    UNLOCK(pInfo);
		    }
            break;

#ifdef NOT_YET
		case IOCTL_FAD_SET_COOLER_STATE:
            dwErr = ERROR_NOT_SUPPORTED;
            break;

        case IOCTL_FAD_GET_COOLER_STATE:   
            dwErr = ERROR_NOT_SUPPORTED;
            break;

        case IOCTL_FAD_GET_MODE_WHEEL_POS:   
            dwErr = ERROR_NOT_SUPPORTED;
            break;

#endif
        case IOCTL_FAD_SET_HDMI_ACCESS:
            if (!BspHasHdmi())
                dwErr = ERROR_NOT_SUPPORTED;
		    else
		    {
			    LOCK(pInfo);
				setHdmiI2cState (* (DWORD*) pBuf);
				dwErr = ERROR_SUCCESS;
    		    UNLOCK(pInfo);
		    }
            break;

		case IOCTL_FAD_GET_KP_BACKLIGHT:
            if (!BspHasKpBacklight())
                dwErr = ERROR_NOT_SUPPORTED;
            else
			{
       		    dwErr = GetKeypadBacklight((FADDEVIOCTLBACKLIGHT *)pBuf);
            }
            break;

        case IOCTL_FAD_SET_KP_BACKLIGHT:
            if (!BspHasKpBacklight())
                dwErr = ERROR_NOT_SUPPORTED;
            else
			{
       		    dwErr = SetKeypadBacklight((FADDEVIOCTLBACKLIGHT *)pBuf);
            }
            break;

		case IOCTL_FAD_GET_KP_SUBJ_BACKLIGHT:
            if (!BspHasKpBacklight())
                dwErr = ERROR_NOT_SUPPORTED;
            else
			{
       		    dwErr = GetKeypadSubjBacklight(pInfo, (FADDEVIOCTLSUBJBACKLIGHT *)pBuf);
            }
            break;

        case IOCTL_FAD_SET_KP_SUBJ_BACKLIGHT:
            if (!BspHasKpBacklight())
                dwErr = ERROR_NOT_SUPPORTED;
            else
			{
       		    dwErr = SetKeypadSubjBacklight(pInfo, (FADDEVIOCTLSUBJBACKLIGHT *)pBuf);
            }
            break;

        case IOCTL_FAD_GET_START_REASON:
            memcpy(pBuf, &g_RestartReason, sizeof(DWORD));
   		    dwErr = ERROR_SUCCESS;
            break;
            
#ifdef NOT_YET
        case IOCTL_FAD_GET_TC_STATE: 
            dwErr = ERROR_NOT_SUPPORTED;
            break;

        case IOCTL_FAD_IS_WDOG_DISABLE_SUPP:
            // There is a need to indicated if a true watchdog disable is supported
            dwErr = ERROR_SUCCESS;
            break;
#endif

		default:
			pr_err("FAD: Unsupported IOCTL code %lX\n", Ioctl);
			dwErr = ERROR_NOT_SUPPORTED;
			break;
    }
	
	// pass back appropriate response codes
    return dwErr;
}

////////////////////////////////////////////////////////
//
// FAD_IOControl
//
////////////////////////////////////////////////////////
static int FAD_IOControl(struct inode *inode, struct file *filep,
		unsigned int cmd, unsigned long arg)
{
    DWORD dwErr = ERROR_SUCCESS;
    char *tmp;

    tmp = kzalloc(_IOC_SIZE(cmd), GFP_KERNEL);
    if (_IOC_DIR(cmd) & _IOC_WRITE)
    {
		pr_err("FAD Ioctl %X copy from user: %d\n", cmd, _IOC_SIZE(cmd));
    	dwErr = copy_from_user(tmp, (void *)arg, _IOC_SIZE(cmd));
    	if (dwErr)
    		pr_err("FAD Copy from user failed: %lu\n", dwErr);
    }

    if (dwErr == ERROR_SUCCESS)
    {
#ifdef DEBUG_FAD_IOCTL
		pr_err("FAD Ioctl %X\n", cmd);
#endif
    	dwErr = DoIOControl(gpDev, cmd, tmp, (PUCHAR)arg);
    	if (dwErr)
    		pr_err("FAD Ioctl failed: %X %ld %d\n", cmd, dwErr, _IOC_NR(cmd));
    }

    if ((dwErr == ERROR_SUCCESS) && (_IOC_DIR(cmd) & _IOC_READ))
    {
		pr_err("FAD Ioctl %X copy to user: %u\n", cmd, _IOC_SIZE(cmd));
    	dwErr = copy_to_user((void *)arg, tmp, _IOC_SIZE(cmd));
    	if (dwErr)
    		pr_err("FAD Copy to user failed: %ld\n", dwErr);
    }
    kfree(tmp);

    return dwErr;
}

static ssize_t dummyWrite (struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	return count;
}

static ssize_t dummyRead (struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	return count;
}

static int dummyRelease (struct inode *inode, struct file *filp)
{
	return 0;
}

static int dummyOpen (struct inode *inode, struct file *filp)
{
	return 0;
}

module_init (FAD_Init);
module_exit (FAD_Deinit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FLIR Application Driver");
MODULE_AUTHOR("Peter Fitger, FLIR Systems AB");

