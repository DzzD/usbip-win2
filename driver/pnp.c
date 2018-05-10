#include "busenum.h"
#include <wdmsec.h> // for IoCreateDeviceSecure


#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, Bus_AddDevice)
#pragma alloc_text (PAGE, Bus_PnP)
#pragma alloc_text (PAGE, bus_get_ports_status)
#pragma alloc_text (PAGE, bus_init_pdo)
#pragma alloc_text (PAGE, bus_unplug_dev)
#pragma alloc_text (PAGE, Bus_DestroyPdo)
#pragma alloc_text (PAGE, Bus_RemoveFdo)
#pragma alloc_text (PAGE, Bus_FDO_PnP)
#pragma alloc_text (PAGE, Bus_StartFdo)
#pragma alloc_text (PAGE, Bus_SendIrpSynchronously)
#pragma alloc_text (PAGE, Bus_EjectDevice)
#endif

NTSTATUS
Bus_AddDevice(
    __in PDRIVER_OBJECT DriverObject,
    __in PDEVICE_OBJECT PhysicalDeviceObject
    )
/*++
Routine Description.

    Our USBIP bus has been found.  Attach our FDO to it.
    Allocate any required resources.  Set things up.
    And be prepared for the ``start device''

Arguments:

    DriverObject - pointer to driver object.

    PhysicalDeviceObject  - Device object representing the bus to which we
                            will attach a new FDO.

--*/
{
    NTSTATUS            status;
    PDEVICE_OBJECT      deviceObject = NULL;
    PFDO_DEVICE_DATA    deviceData = NULL;
    PWCHAR              deviceName = NULL;
    ULONG               nameLength;

    PAGED_CODE ();

    DBGI(DBG_GENERAL | DBG_PNP, "Add Device: 0x%p\n", PhysicalDeviceObject);

    status = IoCreateDevice (
                    DriverObject,               // our driver object
                    sizeof (FDO_DEVICE_DATA),   // device object extension size
                    NULL,                       // FDOs do not have names
                    FILE_DEVICE_BUS_EXTENDER,   // We are a bus
                    FILE_DEVICE_SECURE_OPEN,    //
                    TRUE,                       // our FDO is exclusive
                    &deviceObject);             // The device object created

    if (!NT_SUCCESS (status))
    {
        goto End;
    }

    deviceData = (PFDO_DEVICE_DATA) deviceObject->DeviceExtension;
    RtlZeroMemory (deviceData, sizeof (FDO_DEVICE_DATA));

    //
    // Set the initial state of the FDO
    //

    INITIALIZE_PNP_STATE(deviceData);

    deviceData->common.IsFDO = TRUE;
    deviceData->common.Self = deviceObject;

    ExInitializeFastMutex (&deviceData->Mutex);

    InitializeListHead (&deviceData->ListOfPDOs);

    // Set the PDO for use with PlugPlay functions

    deviceData->UnderlyingPDO = PhysicalDeviceObject;

    //
    // Set the initial powerstate of the FDO
    //

    deviceData->common.DevicePowerState = PowerDeviceUnspecified;
    deviceData->common.SystemPowerState = PowerSystemWorking;


    //
    // Biased to 1. Transition to zero during remove device
    // means IO is finished. Transition to 1 means the device
    // can be stopped.
    //

    deviceData->OutstandingIO = 1;

    //
    // Initialize the remove event to Not-Signaled.  This event
    // will be set when the OutstandingIO will become 0.
    //

    KeInitializeEvent(&deviceData->RemoveEvent,
                  SynchronizationEvent,
                  FALSE);
    //
    // Initialize the stop event to Signaled:
    // there are no Irps that prevent the device from being
    // stopped. This event will be set when the OutstandingIO
    // will become 0.
    //

    KeInitializeEvent(&deviceData->StopEvent,
                      SynchronizationEvent,
                      TRUE);

    deviceObject->Flags |= DO_POWER_PAGABLE|DO_BUFFERED_IO;

    //
    // Tell the Plug & Play system that this device will need a
    // device interface.
    //

    status = IoRegisterDeviceInterface (
                PhysicalDeviceObject,
                (LPGUID) &GUID_DEVINTERFACE_BUSENUM_USBIP,
                NULL,
                &deviceData->InterfaceName);

    if (!NT_SUCCESS (status)) {
	    DBGE(DBG_PNP, "AddDevice: IoRegisterDeviceInterface failed (%x)", status);
	    goto End;
    }

    //
    // Attach our FDO to the device stack.
    // The return value of IoAttachDeviceToDeviceStack is the top of the
    // attachment chain.  This is where all the IRPs should be routed.
    //

    deviceData->NextLowerDriver = IoAttachDeviceToDeviceStack (
                                    deviceObject,
                                    PhysicalDeviceObject);

    if (NULL == deviceData->NextLowerDriver) {

        status = STATUS_NO_SUCH_DEVICE;
        goto End;
    }


#if DBG
    //
    // We will demonstrate here the step to retrieve the name of the PDO
    //

    status = IoGetDeviceProperty (PhysicalDeviceObject,
                                  DevicePropertyPhysicalDeviceObjectName,
                                  0,
                                  NULL,
                                  &nameLength);

    if (status != STATUS_BUFFER_TOO_SMALL)
    {
	    DBGE(DBG_PNP, "AddDevice:IoGDP failed (0x%x)\n", status);
	    goto End;
    }

    deviceName = ExAllocatePoolWithTag (NonPagedPool,
                            nameLength, BUSENUM_POOL_TAG);

    if (NULL == deviceName) {
	    DBGE(DBG_PNP, "AddDevice: no memory to alloc for deviceName(0x%x)\n", nameLength);
	    status =  STATUS_INSUFFICIENT_RESOURCES;
	    goto End;
    }

    status = IoGetDeviceProperty (PhysicalDeviceObject,
                         DevicePropertyPhysicalDeviceObjectName,
                         nameLength,
                         deviceName,
                         &nameLength);

    if (!NT_SUCCESS (status)) {
	    DBGE(DBG_PNP, "AddDevice:IoGDP(2) failed (0x%x)", status);
	    goto End;
    }

    DBGI(DBG_PNP, "AddDevice: %p to %p->%p (%ws) \n", deviceObject, deviceData->NextLowerDriver, PhysicalDeviceObject, deviceName);

#endif

    //
    // We are done with initializing, so let's indicate that and return.
    // This should be the final step in the AddDevice process.
    //
    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

End:
    if (deviceName){
        ExFreePool(deviceName);
    }

    if (!NT_SUCCESS(status) && deviceObject){
        if (deviceData && deviceData->NextLowerDriver){
            IoDetachDevice (deviceData->NextLowerDriver);
        }
        IoDeleteDevice (deviceObject);
    }

    return status;

}

NTSTATUS
Bus_PnP(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp)
{
	PIO_STACK_LOCATION      irpStack;
	NTSTATUS                status;
	PCOMMON_DEVICE_DATA     commonData;

	PAGED_CODE ();

	DBGI(DBG_GENERAL | DBG_PNP, "Bus_PnP: Enter\n");

	irpStack = IoGetCurrentIrpStackLocation(Irp);
	ASSERT (IRP_MJ_PNP == irpStack->MajorFunction);

	commonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;

	//
	// If the device has been removed, the driver should
	// not pass the IRP down to the next lower driver.
	//
	if (commonData->DevicePnPState == Deleted) {
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE ;
		IoCompleteRequest (Irp, IO_NO_INCREMENT);
		return status;
	}

	if (commonData->IsFDO) {
		DBGI(DBG_PNP, "FDO: minor: %s, IRP:0x%p\n", PnPMinorFunctionString(irpStack->MinorFunction), Irp);
		//
		// Request is for the bus FDO
		//
		status = Bus_FDO_PnP(DeviceObject, Irp, irpStack, (PFDO_DEVICE_DATA) commonData);
	} else {
		DBGI(DBG_PNP, "PDO: minor: %s, IRP: 0x%p\n", PnPMinorFunctionString(irpStack->MinorFunction), Irp);
		//
		// Request is for the child PDO.
		//
		status = Bus_PDO_PnP(DeviceObject, Irp, irpStack, (PPDO_DEVICE_DATA) commonData);
	}

	DBGI(DBG_GENERAL | DBG_PNP, "Bus_PnP: Leave\n");

	return status;
}

NTSTATUS
Bus_FDO_PnP (
    __in PDEVICE_OBJECT       DeviceObject,
    __in PIRP                 Irp,
    __in PIO_STACK_LOCATION   IrpStack,
    __in PFDO_DEVICE_DATA     DeviceData
    )
/*++
Routine Description:

    Handle requests from the Plug & Play system for the BUS itself

--*/
{
    NTSTATUS            status;
    ULONG               length, prevcount, numPdosPresent;
    PLIST_ENTRY         entry, listHead, nextEntry;
    PPDO_DEVICE_DATA    pdoData;
    PDEVICE_RELATIONS   relations, oldRelations;

    PAGED_CODE ();

    Bus_IncIoCount (DeviceData);

    switch (IrpStack->MinorFunction) {

    case IRP_MN_START_DEVICE:

        //
        // Send the Irp down and wait for it to come back.
        // Do not touch the hardware until then.
        //
        status = Bus_SendIrpSynchronously (DeviceData->NextLowerDriver, Irp);

        if (NT_SUCCESS(status)) {

            //
            // Initialize your device with the resources provided
            // by the PnP manager to your device.
            //
            status = Bus_StartFdo (DeviceData, Irp);
        }

        //
        // We must now complete the IRP, since we stopped it in the
        // completion routine with MORE_PROCESSING_REQUIRED.
        //

        Irp->IoStatus.Status = status;
        IoCompleteRequest (Irp, IO_NO_INCREMENT);
        Bus_DecIoCount (DeviceData);

        return status;

    case IRP_MN_QUERY_STOP_DEVICE:

        //
        // The PnP manager is trying to stop the device
        // for resource rebalancing. Fail this now if you
        // cannot stop the device in response to STOP_DEVICE.
        //
        SET_NEW_PNP_STATE(DeviceData, StopPending);
        Irp->IoStatus.Status = STATUS_SUCCESS; // You must not fail the IRP.
        break;

    case IRP_MN_CANCEL_STOP_DEVICE:

        //
        // The PnP Manager sends this IRP, at some point after an
        // IRP_MN_QUERY_STOP_DEVICE, to inform the drivers for a
        // device that the device will not be stopped for
        // resource reconfiguration.
        //
        //
        // First check to see whether you have received cancel-stop
        // without first receiving a query-stop. This could happen if
        //  someone above us fails a query-stop and passes down the subsequent
        // cancel-stop.
        //

        if (StopPending == DeviceData->common.DevicePnPState)
        {
            //
            // We did receive a query-stop, so restore.
            //
            RESTORE_PREVIOUS_PNP_STATE(DeviceData);
            ASSERT(DeviceData->common.DevicePnPState == Started);
        }
        Irp->IoStatus.Status = STATUS_SUCCESS; // We must not fail the IRP.
        break;

    case IRP_MN_STOP_DEVICE:

        //
        // Stop device means that the resources given during Start device
        // are now revoked. Note: You must not fail this Irp.
        // But before you relieve resources make sure there are no I/O in
        // progress. Wait for the existing ones to be finished.
        // To do that, first we will decrement this very operation.
        // When the counter goes to 1, Stop event is set.
        //

        Bus_DecIoCount(DeviceData);

        KeWaitForSingleObject(
                   &DeviceData->StopEvent,
                   Executive, // Waiting reason of a driver
                   KernelMode, // Waiting in kernel mode
                   FALSE, // No allert
                   NULL); // No timeout

        //
        // Increment the counter back because this IRP has to
        // be sent down to the lower stack.
        //

        Bus_IncIoCount (DeviceData);

        //
        // Free resources given by start device.
        //

        SET_NEW_PNP_STATE(DeviceData, Stopped);

        //
        // We don't need a completion routine so fire and forget.
        //
        // Set the current stack location to the next stack location and
        // call the next device object.
        //

        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_QUERY_REMOVE_DEVICE:

        //
        // If we were to fail this call then we would need to complete the
        // IRP here.  Since we are not, set the status to SUCCESS and
        // call the next driver.
        //

        SET_NEW_PNP_STATE(DeviceData, RemovePending);

        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_CANCEL_REMOVE_DEVICE:

        //
        // If we were to fail this call then we would need to complete the
        // IRP here.  Since we are not, set the status to SUCCESS and
        // call the next driver.
        //

        //
        // First check to see whether you have received cancel-remove
        // without first receiving a query-remove. This could happen if
        // someone above us fails a query-remove and passes down the
        // subsequent cancel-remove.
        //

        if (RemovePending == DeviceData->common.DevicePnPState)
        {
            //
            // We did receive a query-remove, so restore.
            //
            RESTORE_PREVIOUS_PNP_STATE(DeviceData);
        }
        Irp->IoStatus.Status = STATUS_SUCCESS;// You must not fail the IRP.
        break;

    case IRP_MN_SURPRISE_REMOVAL:

        //
        // The device has been unexpectedly removed from the machine
        // and is no longer available for I/O. Bus_RemoveFdo clears
        // all the resources, frees the interface and de-registers
        // with WMI, but it doesn't delete the FDO. That's done
        // later in Remove device query.
        //

        SET_NEW_PNP_STATE(DeviceData, SurpriseRemovePending);
        Bus_RemoveFdo(DeviceData);

        ExAcquireFastMutex (&DeviceData->Mutex);

        listHead = &DeviceData->ListOfPDOs;

        for(entry = listHead->Flink,nextEntry = entry->Flink;
            entry != listHead;
            entry = nextEntry,nextEntry = entry->Flink) {

            pdoData = CONTAINING_RECORD (entry, PDO_DEVICE_DATA, Link);
            RemoveEntryList (&pdoData->Link);
            InitializeListHead (&pdoData->Link);
            pdoData->ParentFdo  = NULL;
            pdoData->ReportedMissing = TRUE;
        }

        ExReleaseFastMutex (&DeviceData->Mutex);

        Irp->IoStatus.Status = STATUS_SUCCESS; // You must not fail the IRP.
        break;

    case IRP_MN_REMOVE_DEVICE:

        //
        // The Plug & Play system has dictated the removal of this device.
        // We have no choice but to detach and delete the device object.
        //

        //
        // Check the state flag to see whether you are surprise removed
        //

        if (DeviceData->common.DevicePnPState != SurpriseRemovePending)
        {
            Bus_RemoveFdo(DeviceData);
        }

        SET_NEW_PNP_STATE(DeviceData, Deleted);

        //
        // Wait for all outstanding requests to complete.
        // We need two decrements here, one for the increment in
        // the beginning of this function, the other for the 1-biased value of
        // OutstandingIO.
        //

        Bus_DecIoCount (DeviceData);

        //
        // The requestCount is at least one here (is 1-biased)
        //

        Bus_DecIoCount (DeviceData);

        KeWaitForSingleObject (
                &DeviceData->RemoveEvent,
                Executive,
                KernelMode,
                FALSE,
                NULL);

        //
        // Typically the system removes all the  children before
        // removing the parent FDO. If for any reason child Pdos are
        // still present we will destroy them explicitly, with one exception -
        // we will not delete the PDOs that are in SurpriseRemovePending state.
        //

        ExAcquireFastMutex (&DeviceData->Mutex);

        listHead = &DeviceData->ListOfPDOs;

        for(entry = listHead->Flink,nextEntry = entry->Flink;
            entry != listHead;
            entry = nextEntry,nextEntry = entry->Flink) {

            pdoData = CONTAINING_RECORD (entry, PDO_DEVICE_DATA, Link);
            RemoveEntryList (&pdoData->Link);
            if (SurpriseRemovePending == pdoData->common.DevicePnPState)
            {
                //
                // We will reinitialize the list head so that we
                // wouldn't barf when we try to delink this PDO from
                // the parent's PDOs list, when the system finally
                // removes the PDO. Let's also not forget to set the
                // ReportedMissing flag to cause the deletion of the PDO.
                //
                DBGI(DBG_PNP, "\tFound a surprise removed device: 0x%p\n", pdoData->common.Self);
                InitializeListHead (&pdoData->Link);
                pdoData->ParentFdo  = NULL;
                pdoData->ReportedMissing = TRUE;
                continue;
            }
            DeviceData->NumPDOs--;
            Bus_DestroyPdo (pdoData->common.Self, pdoData);
        }

        ExReleaseFastMutex (&DeviceData->Mutex);

        //
        // We need to send the remove down the stack before we detach,
        // but we don't need to wait for the completion of this operation
        // (and to register a completion routine).
        //

        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoSkipCurrentIrpStackLocation (Irp);
        status = IoCallDriver (DeviceData->NextLowerDriver, Irp);
        //
        // Detach from the underlying devices.
        //

        IoDetachDevice (DeviceData->NextLowerDriver);

        DBGI(DBG_PNP, "\tDeleting FDO: 0x%p\n", DeviceObject);

        IoDeleteDevice (DeviceObject);

        return status;

    case IRP_MN_QUERY_DEVICE_RELATIONS:

        DBGI(DBG_PNP, "\tQueryDeviceRelation Type: %s\n",
                    DbgDeviceRelationString(IrpStack->Parameters.QueryDeviceRelations.Type));

        if (BusRelations != IrpStack->Parameters.QueryDeviceRelations.Type) {
            //
            // We don't support any other Device Relations
            //
            break;
        }

        //
        // Tell the plug and play system about all the PDOs.
        //
        // There might also be device relations below and above this FDO,
        // so, be sure to propagate the relations from the upper drivers.
        //
        // No Completion routine is needed so long as the status is preset
        // to success.  (PDOs complete plug and play irps with the current
        // IoStatus.Status and IoStatus.Information as the default.)
        //

        ExAcquireFastMutex (&DeviceData->Mutex);

        oldRelations = (PDEVICE_RELATIONS) Irp->IoStatus.Information;
        if (oldRelations) {
            prevcount = oldRelations->Count;
            if (!DeviceData->NumPDOs) {
                //
                // There is a device relations struct already present and we have
                // nothing to add to it, so just call IoSkip and IoCall
                //
                ExReleaseFastMutex (&DeviceData->Mutex);
                break;
            }
        }
        else  {
            prevcount = 0;
        }

        //
        // Calculate the number of PDOs actually present on the bus
        //
        numPdosPresent = 0;
        for (entry = DeviceData->ListOfPDOs.Flink;
             entry != &DeviceData->ListOfPDOs;
             entry = entry->Flink) {
            pdoData = CONTAINING_RECORD (entry, PDO_DEVICE_DATA, Link);
            if (pdoData->Present)
                numPdosPresent++;
        }

        //
        // Need to allocate a new relations structure and add our
        // PDOs to it.
        //

        length = sizeof(DEVICE_RELATIONS) +
                ((numPdosPresent + prevcount) * sizeof (PDEVICE_OBJECT)) -1;

        relations = (PDEVICE_RELATIONS) ExAllocatePoolWithTag (PagedPool,
                                        length, BUSENUM_POOL_TAG);

        if (NULL == relations) {
            //
            // Fail the IRP
            //
            ExReleaseFastMutex (&DeviceData->Mutex);
            Irp->IoStatus.Status = status = STATUS_INSUFFICIENT_RESOURCES;
            IoCompleteRequest (Irp, IO_NO_INCREMENT);
            Bus_DecIoCount (DeviceData);
            return status;

        }

        //
        // Copy in the device objects so far
        //
        if (prevcount) {
            RtlCopyMemory (relations->Objects, oldRelations->Objects,
                                      prevcount * sizeof (PDEVICE_OBJECT));
        }

        relations->Count = prevcount + numPdosPresent;

        //
        // For each PDO present on this bus add a pointer to the device relations
        // buffer, being sure to take out a reference to that object.
        // The Plug & Play system will dereference the object when it is done
        // with it and free the device relations buffer.
        //

        for (entry = DeviceData->ListOfPDOs.Flink;
             entry != &DeviceData->ListOfPDOs;
             entry = entry->Flink) {

            pdoData = CONTAINING_RECORD (entry, PDO_DEVICE_DATA, Link);
            if (pdoData->Present) {
                relations->Objects[prevcount] = pdoData->common.Self;
                ObReferenceObject (pdoData->common.Self);
                prevcount++;
            } else {
                pdoData->ReportedMissing = TRUE;
            }
        }

        DBGI(DBG_PNP, "# of PDOS: present: %d, reported: %d\n", DeviceData->NumPDOs, relations->Count);

        //
        // Replace the relations structure in the IRP with the new
        // one.
        //
        if (oldRelations) {
            ExFreePool (oldRelations);
        }
        Irp->IoStatus.Information = (ULONG_PTR) relations;

        ExReleaseFastMutex (&DeviceData->Mutex);

        //
        // Set up and pass the IRP further down the stack
        //
        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    default:

        //
        // In the default case we merely call the next driver.
        // We must not modify Irp->IoStatus.Status or complete the IRP.
        //

        break;
    }

    IoSkipCurrentIrpStackLocation (Irp);
    status = IoCallDriver (DeviceData->NextLowerDriver, Irp);
    Bus_DecIoCount (DeviceData);
    return status;
}

NTSTATUS
Bus_StartFdo (
    __in  PFDO_DEVICE_DATA            FdoData,
    __in  PIRP   Irp )
/*++

Routine Description:

    Initialize and start the bus controller. Get the resources
    by parsing the list and map them if required.

Arguments:

    DeviceData - Pointer to the FDO's device extension.
    Irp          - Pointer to the irp.

Return Value:

    NT Status is returned.

--*/
{
    NTSTATUS status;
    POWER_STATE powerState;

    PAGED_CODE ();

	UNREFERENCED_PARAMETER(Irp);

    //
    // Check the function driver source to learn
    // about parsing resource list.
    //

    //
    // Enable device interface. If the return status is
    // STATUS_OBJECT_NAME_EXISTS means we are enabling the interface
    // that was already enabled, which could happen if the device
    // is stopped and restarted for resource rebalancing.
    //

    status = IoSetDeviceInterfaceState(&FdoData->InterfaceName, TRUE);
    if (!NT_SUCCESS (status)) {
	    DBGE(DBG_PNP, "IoSetDeviceInterfaceState failed: 0x%x\n", status);
	    return status;
    }

    //
    // Set the device power state to fully on. Also if this Start
    // is due to resource rebalance, you should restore the device
    // to the state it was before you stopped the device and relinquished
    // resources.
    //

    FdoData->common.DevicePowerState = PowerDeviceD0;
    powerState.DeviceState = PowerDeviceD0;
    PoSetPowerState ( FdoData->common.Self, DevicePowerState, powerState );

    SET_NEW_PNP_STATE(FdoData, Started);

    //
    // Register with WMI
    //
    status = Bus_WmiRegistration(FdoData);
    if (!NT_SUCCESS (status)) {
	    DBGE(DBG_PNP, "StartFdo: Bus_WmiRegistration failed (%x)\n", status);
    }

    return status;
}

void
Bus_RemoveFdo (
    __in PFDO_DEVICE_DATA FdoData
    )
/*++
Routine Description:

    Frees any memory allocated by the FDO and unmap any IO mapped as well.
    This function does not the delete the deviceobject.

Arguments:

    DeviceData - Pointer to the FDO's device extension.

Return Value:

    NT Status is returned.

--*/
{
    PAGED_CODE ();

    //
    // Stop all access to the device, fail any outstanding I/O to the device,
    // and free all the resources associated with the device.
    //

    //
    // Disable the device interface and free the buffer
    //
    if (FdoData->InterfaceName.Buffer != NULL) {

        IoSetDeviceInterfaceState (&FdoData->InterfaceName, FALSE);

        ExFreePool (FdoData->InterfaceName.Buffer);
        RtlZeroMemory (&FdoData->InterfaceName,
                       sizeof (UNICODE_STRING));
    }

    //
    // Inform WMI to remove this DeviceObject from its
    // list of providers.
    //

    Bus_WmiDeRegistration(FdoData);

}

NTSTATUS
Bus_SendIrpSynchronously (
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp
    )
/*++
Routine Description:

    Sends the Irp down to lower driver and waits for it to
    come back by setting a completion routine.

Arguments:
    DeviceObject - Pointer to deviceobject
    Irp          - Pointer to a PnP Irp.

Return Value:

    NT Status is returned.

--*/
{
    KEVENT   event;
    NTSTATUS status;

    PAGED_CODE();

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(Irp);

    IoSetCompletionRoutine(Irp,
                           Bus_CompletionRoutine,
                           &event,
                           TRUE,
                           TRUE,
                           TRUE
                           );

    status = IoCallDriver(DeviceObject, Irp);

    //
    // Wait for lower drivers to be done with the Irp.
    // Important thing to note here is when you allocate
    // the memory for an event in the stack you must do a
    // KernelMode wait instead of UserMode to prevent
    // the stack from getting paged out.
    //

    if (status == STATUS_PENDING) {
       KeWaitForSingleObject(&event,
                             Executive,
                             KernelMode,
                             FALSE,
                             NULL
                             );
       status = Irp->IoStatus.Status;
    }

    return status;
}

NTSTATUS
Bus_CompletionRoutine(
    __in PDEVICE_OBJECT   DeviceObject,
    __in PIRP             Irp,
    __in PVOID            Context
    )
/*++
Routine Description:
    A completion routine for use when calling the lower device objects to
    which our bus (FDO) is attached.

Arguments:

    DeviceObject - Pointer to deviceobject
    Irp          - Pointer to a PnP Irp.
    Context      - Pointer to an event object
Return Value:

    NT Status is returned.

--*/
{
    UNREFERENCED_PARAMETER (DeviceObject);

    //
    // If the lower driver didn't return STATUS_PENDING, we don't need to
    // set the event because we won't be waiting on it.
    // This optimization avoids grabbing the dispatcher lock and improves perf.
    //
    if (Irp->PendingReturned == TRUE) {

        KeSetEvent ((PKEVENT) Context, IO_NO_INCREMENT, FALSE);
    }
    return STATUS_MORE_PROCESSING_REQUIRED; // Keep this IRP
}

void complete_pending_read_irp(PPDO_DEVICE_DATA pdodata)
{
	KIRQL oldirql;
	PIRP irp;

	KeAcquireSpinLock(&pdodata->q_lock, &oldirql);
	irp=pdodata->pending_read_irp;
	pdodata->pending_read_irp=NULL;
	KeReleaseSpinLock(&pdodata->q_lock, oldirql);
	if(NULL==irp)
		return;
	irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
	IoSetCancelRoutine(irp, NULL);
	KeRaiseIrql(DISPATCH_LEVEL, &oldirql);
	IoCompleteRequest (irp, IO_NO_INCREMENT);
	KeLowerIrql(oldirql);
	return;
}

void
complete_pending_irp(PPDO_DEVICE_DATA pdodata)
{
    PIRP irp;
    struct urb_req * urb_r = NULL;
    PLIST_ENTRY le;
    KIRQL oldirql;
    KIRQL oldirql2;
    int count=0;
	LARGE_INTEGER interval;

    //FIXME
    DBGI(DBG_PNP, "finish pending irp\n");
    KeRaiseIrql(DISPATCH_LEVEL, &oldirql);
    do {
	irp=NULL;
	le=NULL;
	KeAcquireSpinLockAtDpcLevel(&pdodata->q_lock);
	if (!IsListEmpty(&pdodata->ioctl_q))
		le = RemoveHeadList(&pdodata->ioctl_q);
	if(le){
		urb_r = CONTAINING_RECORD(le, struct urb_req, list);
		/* FIMXE event */
		irp = urb_r->irp;
	}
	if(irp==NULL){
		KeReleaseSpinLock(&pdodata->q_lock, oldirql);
		break;
	}
	if(count>2){
		KeReleaseSpinLock(&pdodata->q_lock, oldirql);
		DBGI(DBG_PNP, "sleep 50ms, let pnp manager send irp");
		interval.QuadPart=-500000;
		KeDelayExecutionThread(KernelMode, FALSE, &interval);
		KeRaiseIrql(DISPATCH_LEVEL, &oldirql);
	} else {
		KeReleaseSpinLock(&pdodata->q_lock, DISPATCH_LEVEL);
	}
	ExFreeToNPagedLookasideList(&g_lookaside, urb_r);
	irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
	IoSetCancelRoutine(irp, NULL);
	KeRaiseIrql(DISPATCH_LEVEL, &oldirql2);
	IoCompleteRequest (irp, IO_NO_INCREMENT);
	KeLowerIrql(oldirql2);
	count++;
    }while(1);
}

NTSTATUS
Bus_DestroyPdo (
    PDEVICE_OBJECT      Device,
    __in PPDO_DEVICE_DATA    PdoData
    )
/*++
Routine Description:
    The Plug & Play subsystem has instructed that this PDO should be removed.

    We should therefore
    - Complete any requests queued in the driver
    - If the device is still attached to the system,
    - then complete the request and return.
    - Otherwise, cleanup device specific allocations, memory, events...
    - Call IoDeleteDevice
    - Return from the dispatch routine.

    Note that if the device is still connected to the bus (IE in this case
    the enum application has not yet told us that the USBIP device has disappeared)
    then the PDO must remain around, and must be returned during any
    query Device relations IRPS.

--*/

{
    PAGED_CODE ();

    //
    // BusEnum does not queue any irps at this time so we have nothing to do.
    //
    //
    // Free any resources.
    //

    if (PdoData->HardwareIDs) {
        ExFreePool (PdoData->HardwareIDs);
        PdoData->HardwareIDs = NULL;
    }

    if (PdoData->compatible_ids) {
        ExFreePool (PdoData->compatible_ids);
        PdoData->compatible_ids = NULL;
    }
    //FIXME
    if (PdoData->fo){
	    PdoData->fo->FsContext = NULL;
	    PdoData->fo = NULL;
    }
    DBGI(DBG_PNP, "\tDeleting PDO: 0x%p\n", Device);
    IoDeleteDevice (Device);
    return STATUS_SUCCESS;
}

void
bus_init_pdo (
    __out PDEVICE_OBJECT      pdo,
    PFDO_DEVICE_DATA    fdodata
    )
{
    PPDO_DEVICE_DATA pdodata;

    PAGED_CODE ();

    pdodata = (PPDO_DEVICE_DATA)  pdo->DeviceExtension;

    DBGI(DBG_PNP, "pdo 0x%p, extension 0x%p\n", pdo, pdodata);

    //
    // Initialize the rest
    //
    pdodata->common.IsFDO = FALSE;
    pdodata->common.Self = pdo;
 
    pdodata->ParentFdo = fdodata->common.Self;

    pdodata->Present = TRUE; // attached to the bus
    pdodata->ReportedMissing = FALSE; // not yet reported missing

    INITIALIZE_PNP_STATE(pdodata);

    //
    // PDO's usually start their life at D3
    //

    pdodata->common.DevicePowerState = PowerDeviceD3;
    pdodata->common.SystemPowerState = PowerSystemWorking;

    InitializeListHead(&pdodata->ioctl_q);
    KeInitializeSpinLock(&pdodata->q_lock);

    pdo->Flags |= DO_POWER_PAGABLE|DO_DIRECT_IO;

    ExAcquireFastMutex (&fdodata->Mutex);
    InsertTailList(&fdodata->ListOfPDOs, &pdodata->Link);
    fdodata->NumPDOs++;
    ExReleaseFastMutex (&fdodata->Mutex);
    // This should be the last step in initialization.
    pdo->Flags &= ~DO_DEVICE_INITIALIZING;
}

NTSTATUS bus_get_ports_status(ioctl_usbvbus_get_ports_status * st,
		PFDO_DEVICE_DATA  fdodata, ULONG *info)
{
    PPDO_DEVICE_DATA    pdodata;
    PLIST_ENTRY         entry;

    PAGED_CODE ();

    DBGI(DBG_PNP, "get ports status\n");

    RtlZeroMemory(st, sizeof(*st));
    ExAcquireFastMutex (&fdodata->Mutex);

    for (entry = fdodata->ListOfPDOs.Flink;
         entry != &fdodata->ListOfPDOs;
         entry = entry->Flink) {

        pdodata = CONTAINING_RECORD (entry, PDO_DEVICE_DATA, Link);
		if (pdodata->SerialNo > 127 || pdodata->SerialNo == 0){
			DBGE(DBG_PNP, "strange error");
		}
		if(st->u.max_used_port < (char)pdodata->SerialNo)
			st->u.max_used_port = (char)pdodata->SerialNo;
		st->u.port_status[pdodata->SerialNo]=1;
    }
    ExReleaseFastMutex (&fdodata->Mutex);
    *info=sizeof(*st);
    return STATUS_SUCCESS;
}



NTSTATUS
bus_unplug_dev (
    int addr,
    PFDO_DEVICE_DATA            fdodata
    )
{
    PLIST_ENTRY         entry;
    PPDO_DEVICE_DATA    pdodata;
    int found=0, all;

    PAGED_CODE ();

    if(addr<0||addr>127)
	return STATUS_INVALID_PARAMETER;

    all = (0 == addr);

    ExAcquireFastMutex (&fdodata->Mutex);

    if (all) {
	    DBGI(DBG_PNP, "Plugging out all the devices!\n");
    } else {
	    DBGI(DBG_PNP, "Plugging out %d\n", addr);
    }

    if (fdodata->NumPDOs == 0) {
        //
        // We got a 2nd plugout...somebody in user space isn't playing nice!!!
        //
	    DBGW(DBG_PNP, "BAD BAD BAD...2 removes!!! Send only one!\n");
	    ExReleaseFastMutex (&fdodata->Mutex);
	    return STATUS_NO_SUCH_DEVICE;
    }

    for (entry = fdodata->ListOfPDOs.Flink;
         entry != &fdodata->ListOfPDOs;
         entry = entry->Flink) {

        pdodata = CONTAINING_RECORD (entry, PDO_DEVICE_DATA, Link);

        DBGI(DBG_PNP, "found device %d\n", pdodata->SerialNo);

        if (all || addr == (int)pdodata->SerialNo) {
		DBGI(DBG_PNP, "Plugging out %d\n", pdodata->SerialNo);
		pdodata->Present = FALSE;
		complete_pending_read_irp(pdodata);
		found = 1;
            if (!all) {
                break;
            }
        }
    }

    ExReleaseFastMutex (&fdodata->Mutex);

    if (found) {
        IoInvalidateDeviceRelations (fdodata->UnderlyingPDO, BusRelations);

		ExAcquireFastMutex (&fdodata->Mutex);

		for (entry = fdodata->ListOfPDOs.Flink;
			 entry != &fdodata->ListOfPDOs;
			 entry = entry->Flink) {

			pdodata = CONTAINING_RECORD (entry, PDO_DEVICE_DATA, Link);

			if( pdodata->Present ==FALSE){
				complete_pending_irp(pdodata);
				SET_NEW_PNP_STATE(pdodata,PNP_DEVICE_REMOVED);
				IoInvalidateDeviceState(pdodata->common.Self);
			}
		}
		ExReleaseFastMutex (&fdodata->Mutex);

		DBGI(DBG_PNP, "Device %d plug out finished\n", addr);
		return  STATUS_SUCCESS;
    }


    return STATUS_INVALID_PARAMETER;
}

NTSTATUS
Bus_EjectDevice (
    PBUSENUM_EJECT_HARDWARE     Eject,
    PFDO_DEVICE_DATA            FdoData
    )
/*++
Routine Description:
    The user application has told us to eject the device from the bus.
    In a real situation the driver gets notified by an interrupt when the
    user presses the Eject button on the device.

Arguments:

    Eject   - pointer to Eject hardware structure.
    FdoData - contains the list to iterate over

Returns:

    STATUS_SUCCESS upon successful removal from the list
    STATUS_INVALID_PARAMETER if the removal was unsuccessful

--*/
{
    PLIST_ENTRY         entry;
    PPDO_DEVICE_DATA    pdoData;
    BOOLEAN             found = FALSE, ejectAll;

    PAGED_CODE ();

    ejectAll = (0 == Eject->SerialNo);

    ExAcquireFastMutex (&FdoData->Mutex);

    if (ejectAll) {
	    DBGI(DBG_PNP, "Ejecting all the pdos!\n");
    } else {
	    DBGI(DBG_PNP, "Ejecting %d\n", Eject->SerialNo);
    }

    if (FdoData->NumPDOs == 0) {
        //
        // Somebody in user space isn't playing nice!!!
        //
        DBGW(DBG_PNP, "No devices to eject!\n");
        ExReleaseFastMutex (&FdoData->Mutex);
        return STATUS_NO_SUCH_DEVICE;
    }

    //
    // Scan the list to find matching PDOs
    //
    for (entry = FdoData->ListOfPDOs.Flink;
         entry != &FdoData->ListOfPDOs;
         entry = entry->Flink) {

        pdoData = CONTAINING_RECORD (entry, PDO_DEVICE_DATA, Link);

        DBGI(DBG_PNP, "found device %d\n", pdoData->SerialNo);

        if (ejectAll || Eject->SerialNo == pdoData->SerialNo) {
		DBGI(DBG_PNP, "Ejected %d\n", pdoData->SerialNo);
		found = TRUE;
		IoRequestDeviceEject(pdoData->common.Self);
            if (!ejectAll) {
                break;
            }
        }
    }
    ExReleaseFastMutex (&FdoData->Mutex);

    if (found) {
        return STATUS_SUCCESS;
    }

    DBGW(DBG_PNP, "Device %d is not present\n", Eject->SerialNo);

    return STATUS_INVALID_PARAMETER;
}

#if DBG

PCHAR
PnPMinorFunctionString (
    UCHAR MinorFunction
)
{
    switch (MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            return "IRP_MN_START_DEVICE";
        case IRP_MN_QUERY_REMOVE_DEVICE:
            return "IRP_MN_QUERY_REMOVE_DEVICE";
        case IRP_MN_REMOVE_DEVICE:
            return "IRP_MN_REMOVE_DEVICE";
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            return "IRP_MN_CANCEL_REMOVE_DEVICE";
        case IRP_MN_STOP_DEVICE:
            return "IRP_MN_STOP_DEVICE";
        case IRP_MN_QUERY_STOP_DEVICE:
            return "IRP_MN_QUERY_STOP_DEVICE";
        case IRP_MN_CANCEL_STOP_DEVICE:
            return "IRP_MN_CANCEL_STOP_DEVICE";
        case IRP_MN_QUERY_DEVICE_RELATIONS:
            return "IRP_MN_QUERY_DEVICE_RELATIONS";
        case IRP_MN_QUERY_INTERFACE:
            return "IRP_MN_QUERY_INTERFACE";
        case IRP_MN_QUERY_CAPABILITIES:
            return "IRP_MN_QUERY_CAPABILITIES";
        case IRP_MN_QUERY_RESOURCES:
            return "IRP_MN_QUERY_RESOURCES";
        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            return "IRP_MN_QUERY_RESOURCE_REQUIREMENTS";
        case IRP_MN_QUERY_DEVICE_TEXT:
            return "IRP_MN_QUERY_DEVICE_TEXT";
        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            return "IRP_MN_FILTER_RESOURCE_REQUIREMENTS";
        case IRP_MN_READ_CONFIG:
            return "IRP_MN_READ_CONFIG";
        case IRP_MN_WRITE_CONFIG:
            return "IRP_MN_WRITE_CONFIG";
        case IRP_MN_EJECT:
            return "IRP_MN_EJECT";
        case IRP_MN_SET_LOCK:
            return "IRP_MN_SET_LOCK";
        case IRP_MN_QUERY_ID:
            return "IRP_MN_QUERY_ID";
        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            return "IRP_MN_QUERY_PNP_DEVICE_STATE";
        case IRP_MN_QUERY_BUS_INFORMATION:
            return "IRP_MN_QUERY_BUS_INFORMATION";
        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
            return "IRP_MN_DEVICE_USAGE_NOTIFICATION";
        case IRP_MN_SURPRISE_REMOVAL:
            return "IRP_MN_SURPRISE_REMOVAL";
        case IRP_MN_QUERY_LEGACY_BUS_INFORMATION:
            return "IRP_MN_QUERY_LEGACY_BUS_INFORMATION";
        default:
            return "Unknown PNP IRP";
    }
}

PCHAR
DbgDeviceRelationString(
    __in DEVICE_RELATION_TYPE Type
    )
{
    switch (Type)
    {
        case BusRelations:
            return "BusRelations";
        case PowerRelations:
            return "PowerRelations";
        case EjectionRelations:
            return "EjectionRelations";
        case RemovalRelations:
            return "RemovalRelations";
        case TargetDeviceRelation:
            return "TargetDeviceRelation";
        default:
            return "UnKnown Relation";
    }
}

PCHAR
DbgDeviceIDString(
    BUS_QUERY_ID_TYPE Type
    )
{
    switch (Type)
    {
        case BusQueryDeviceID:
            return "BusQueryDeviceID";
        case BusQueryHardwareIDs:
            return "BusQueryHardwareIDs";
        case BusQueryCompatibleIDs:
            return "BusQueryCompatibleIDs";
        case BusQueryInstanceID:
            return "BusQueryInstanceID";
        case BusQueryDeviceSerialNumber:
            return "BusQueryDeviceSerialNumber";
		case BusQueryContainerID:
            return "BusQueryContainerID";
        default:
            return "UnKnown ID";
    }
}

#endif


