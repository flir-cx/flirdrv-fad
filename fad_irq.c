/***********************************************************************
*                                                                     
* Project: Balthazar
* $Date$
* $Author$
*
* $Id$
*
* Description of file:
*    FLIR Application Driver (FAD) handling of input interrupt pins.
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
#include <linux/irq.h>
#include <linux/platform_device.h>

// Internal function prototypes
irqreturn_t fadLaserIST(int irq, void *dev_id);
irqreturn_t fadHdmiIST(int irq, void *dev_id);
irqreturn_t fadDigIN1IST(int irq, void *dev_id);
irqreturn_t fadDigIN2IST(int irq, void *dev_id);
irqreturn_t fadDigIN3IST(int irq, void *dev_id);

// Code

BOOL InitLaserIrq(PFAD_HW_INDEP_INFO pInfo)
{
	DWORD ret;

	ret = request_irq(gpio_to_irq(LASER_ON), fadLaserIST,
					  IRQF_TRIGGER_HIGH | IRQF_ONESHOT, "LaserON", pInfo);

	if (ret != 0)
        pr_err("Failed to request Laser IRQ (%ld)\n", ret);
	else
        pr_err("Successfully requested Laser IRQ\n");

    return TRUE;
}

BOOL InitHdmiIrq(PFAD_HW_INDEP_INFO pInfo)
{
	DWORD ret;

	pInfo->hdmiIrqQueue = create_singlethread_workqueue("HdmiQueue");

	INIT_WORK(&pInfo->hdmiWork, HandleHdmiInterrupt);

	ret = request_irq(gpio_to_irq(I2C2_HDMI_INT), fadHdmiIST,
			IRQF_TRIGGER_FALLING, "HdmiInt", pInfo);

	if (ret != 0)
        pr_err("Failed to request HDMI IRQ (%ld)\n", ret);
	else
        pr_err("Successfully requested HDMI IRQ\n");

    return TRUE;
}

BOOL InitDigitalIOIrq(PFAD_HW_INDEP_INFO pInfo)
{
#ifdef NOT_YET
    DWORD	dwIrq;
	HANDLE	hIST;

    // Create interrupt events
    pInfo->hDigINEvent1 = CreateEvent(0,FALSE,FALSE,NULL);
    if (!pInfo->hDigINEvent1)
	{
        DEBUGMSG(FAD_ZONE_INIT | FAD_ZONE_ERROR, (TEXT("FAD_Init: Error creating DigIN event 1\r\n")));
        return FALSE;
    }

    pInfo->hDigINEvent2 = CreateEvent(0,FALSE,FALSE,NULL);
    if (!pInfo->hDigINEvent2)
	{
        DEBUGMSG(FAD_ZONE_INIT | FAD_ZONE_ERROR, (TEXT("FAD_Init: Error creating DigIN event 2\r\n")));
        return FALSE;
    }

    pInfo->hDigINEvent3 = CreateEvent(0,FALSE,FALSE,NULL);
    if (!pInfo->hDigINEvent3)
	{
        DEBUGMSG(FAD_ZONE_INIT | FAD_ZONE_ERROR, (TEXT("FAD_Init: Error creating DigIN event 3\r\n")));
        return FALSE;
    }

	// Request Sysintr
    dwIrq = IRQ_DIGIN_1;
    if (!KernelIoControl(IOCTL_HAL_REQUEST_SYSINTR, &dwIrq, sizeof(UINT32),
                         &pInfo->dwDigIn1intr, sizeof(UINT32), NULL))
    {
        DEBUGMSG(FAD_ZONE_INIT | FAD_ZONE_ERROR, (TEXT("FAD: Failed to get sysintr\r\n")));
		return FALSE;
    }
	
    dwIrq = IRQ_DIGIN_2;
    if (!KernelIoControl(IOCTL_HAL_REQUEST_SYSINTR, &dwIrq, sizeof(UINT32),
                         &pInfo->dwDigIn2intr, sizeof(UINT32), NULL))
    {
        DEBUGMSG(FAD_ZONE_INIT | FAD_ZONE_ERROR, (TEXT("FAD: Failed to get sysintr\r\n")));
		return FALSE;
    }

    dwIrq = IRQ_DIGIN_3;
    if (!KernelIoControl(IOCTL_HAL_REQUEST_SYSINTR, &dwIrq, sizeof(UINT32),
                         &pInfo->dwDigIn3intr, sizeof(UINT32), NULL))
    {
        DEBUGMSG(FAD_ZONE_INIT | FAD_ZONE_ERROR, (TEXT("FAD: Failed to get sysintr\r\n")));
		return FALSE;
    }

	// Initialize interrupts
	if (!InterruptInitialize(pInfo->dwDigIn1intr, pInfo->hDigINEvent1, NULL, 0)) 
	{
        DEBUGMSG(FAD_ZONE_INIT | FAD_ZONE_ERROR, (TEXT("Error initializing DigIN1 interrupt\r\n")));
		return FALSE;
    }

	// Initialize interrupts
	if (!InterruptInitialize(pInfo->dwDigIn2intr, pInfo->hDigINEvent2, NULL, 0)) 
	{
        DEBUGMSG(FAD_ZONE_INIT | FAD_ZONE_ERROR, (TEXT("Error initializing DigIN2 interrupt\r\n")));
		return FALSE;
    }

	// Initialize interrupts
	if (!InterruptInitialize(pInfo->dwDigIn3intr, pInfo->hDigINEvent3, NULL, 0)) 
	{
        DEBUGMSG(FAD_ZONE_INIT | FAD_ZONE_ERROR, (TEXT("Error initializing DigIN3 interrupt\r\n")));
		return FALSE;
    }

	// Create Interrupt Threads
    hIST = CreateThread(NULL, 0, fadDigIN1IST, pInfo, 0,NULL);
    if ( hIST == NULL ) 
	{
		DEBUGMSG(FAD_ZONE_INIT | FAD_ZONE_ERROR, (TEXT("Error creating DigIN1 IST (%d)\n\r"), GetLastError()));
        return FALSE;
    }

    hIST = CreateThread(NULL, 0, fadDigIN2IST, pInfo, 0,NULL);
    if ( hIST == NULL ) 
	{
		DEBUGMSG(FAD_ZONE_INIT | FAD_ZONE_ERROR, (TEXT("Error creating DigIN2 IST (%d)\n\r"), GetLastError()));
        return FALSE;
    }

    hIST = CreateThread(NULL, 0, fadDigIN3IST, pInfo, 0,NULL);
    if ( hIST == NULL ) 
	{
		DEBUGMSG(FAD_ZONE_INIT | FAD_ZONE_ERROR, (TEXT("Error creating DigIN3 IST (%d)\n\r"), GetLastError()));
        return FALSE;
    }
#endif

	return TRUE;
}

DWORD ApplicationEvent(PFAD_HW_INDEP_INFO pInfo)
{
	kobject_uevent(&pInfo->pLinuxDevice->dev.kobj, KOBJ_ADD);

	return ERROR_SUCCESS;
}

irqreturn_t fadHdmiIST(int irq, void *dev_id)
{
    PFAD_HW_INDEP_INFO	pInfo = (PFAD_HW_INDEP_INFO)dev_id;

    pr_err("fadHdmiIST\n");

    queue_work(pInfo->hdmiIrqQueue, &pInfo->hdmiWork);

    return IRQ_HANDLED;
}

irqreturn_t fadDigIN1IST(int irq, void *dev_id)
{
    PFAD_HW_INDEP_INFO	pInfo = (PFAD_HW_INDEP_INFO)dev_id;
    static BOOL         bWaitForNeg = FALSE;

    ApplicationEvent(pInfo);
	if (bWaitForNeg)
	{
//		SetInterrupt(PORT_DIGIO, PIN_DIGIN_1, TRUE);
		bWaitForNeg = FALSE;
	}
	else
	{
//		SetInterrupt(PORT_DIGIO, PIN_DIGIN_1, FALSE);
		bWaitForNeg = TRUE;
	}

	return IRQ_HANDLED;
}

irqreturn_t fadDigIN2IST(int irq, void *dev_id)
{
    PFAD_HW_INDEP_INFO	pInfo = (PFAD_HW_INDEP_INFO)dev_id;
    static BOOL         bWaitForNeg = FALSE;

    ApplicationEvent(pInfo);
	if (bWaitForNeg)
	{
//		SetInterrupt(PORT_DIGIO, PIN_DIGIN_2, TRUE);
		bWaitForNeg = FALSE;
	}
	else
	{
//		SetInterrupt(PORT_DIGIO, PIN_DIGIN_2, FALSE);
		bWaitForNeg = TRUE;
	}

	return IRQ_HANDLED;
}

irqreturn_t fadDigIN3IST(int irq, void *dev_id)
{
    PFAD_HW_INDEP_INFO	pInfo = (PFAD_HW_INDEP_INFO)dev_id;
    static BOOL         bWaitForNeg = FALSE;

    ApplicationEvent(pInfo);
	if (bWaitForNeg)
	{
//		SetInterrupt(PORT_DIGIO, PIN_DIGIN_3, TRUE);
		bWaitForNeg = FALSE;
	}
	else
	{
//		SetInterrupt(PORT_DIGIO, PIN_DIGIN_3, FALSE);
		bWaitForNeg = TRUE;
	}

	return IRQ_HANDLED;
}

irqreturn_t fadLaserIST(int irq, void *dev_id)
{
    PFAD_HW_INDEP_INFO	pInfo = (PFAD_HW_INDEP_INFO)dev_id;
    static BOOL         bWaitForNeg;

    ApplicationEvent(pInfo);
	if (bWaitForNeg)
	{
		set_irq_type(IOMUX_TO_IRQ(LASER_ON), IRQF_TRIGGER_HIGH | IRQF_ONESHOT);
		bWaitForNeg = FALSE;
	}
	else
	{
		set_irq_type(IOMUX_TO_IRQ(LASER_ON), IRQF_TRIGGER_LOW | IRQF_ONESHOT);
		bWaitForNeg = TRUE;
	}

	pr_err("fadLaserIST\n");

    return IRQ_HANDLED;
}

