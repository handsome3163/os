/*++

Copyright (c) 2014 Minoca Corp. All rights reserved.

Module Name:

    sdomap4.c

Abstract:

    This module implements the SD/MMC driver for TI OMAP4 SoCs.

Author:

    Evan Green 16-Mar-2014

Environment:

    Kernel

--*/

//
// ------------------------------------------------------------------- Includes
//

#include <minoca/driver.h>
#include "sdomap4.h"

//
// --------------------------------------------------------------------- Macros
//

//
// These macros read and write SD controller registers.
//

#define SD_OMAP4_READ_REGISTER(_Device, _Register) \
    HlReadRegister32((_Device)->ControllerBase + (_Register))

#define SD_OMAP4_WRITE_REGISTER(_Device, _Register, _Value) \
    HlWriteRegister32((_Device)->ControllerBase + (_Register), (_Value))

//
// ---------------------------------------------------------------- Definitions
//

//
// ------------------------------------------------------ Data Type Definitions
//

//
// ----------------------------------------------- Internal Function Prototypes
//

KSTATUS
SdOmap4AddDevice (
    PVOID Driver,
    PSTR DeviceId,
    PSTR ClassId,
    PSTR CompatibleIds,
    PVOID DeviceToken
    );

VOID
SdOmap4DispatchStateChange (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    );

VOID
SdOmap4DispatchOpen (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    );

VOID
SdOmap4DispatchClose (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    );

VOID
SdOmap4DispatchIo (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    );

VOID
SdOmap4DispatchSystemControl (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    );

VOID
SdOmap4ParentDispatchStateChange (
    PIRP Irp,
    PSD_OMAP4_CONTEXT Context,
    PVOID IrpContext
    );

VOID
SdOmap4ChildDispatchStateChange (
    PIRP Irp,
    PSD_OMAP4_CHILD Child,
    PVOID IrpContext
    );

KSTATUS
SdOmap4ParentProcessResourceRequirements (
    PIRP Irp,
    PSD_OMAP4_CONTEXT Device
    );

KSTATUS
SdOmap4ParentStartDevice (
    PIRP Irp,
    PSD_OMAP4_CONTEXT Device
    );

KSTATUS
SdOmap4ParentQueryChildren (
    PIRP Irp,
    PSD_OMAP4_CONTEXT Device
    );

KSTATUS
SdOmap4ResetController (
    PSD_OMAP4_CONTEXT Device
    );

INTERRUPT_STATUS
SdOmap4InterruptService (
    PVOID Context
    );

INTERRUPT_STATUS
SdOmap4InterruptServiceDispatch (
    PVOID Context
    );

VOID
SdOmap4DmaCompletion (
    PSD_CONTROLLER Controller,
    PVOID Context,
    UINTN BytesTransferred,
    KSTATUS Status
    );

PSD_OMAP4_CHILD
SdOmap4pCreateChild (
    PSD_OMAP4_CONTEXT Device
    );

VOID
SdOmap4pDestroyChild (
    PSD_OMAP4_CHILD Child
    );

VOID
SdOmap4pChildAddReference (
    PSD_OMAP4_CHILD Child
    );

VOID
SdOmap4pChildReleaseReference (
    PSD_OMAP4_CHILD Child
    );

KSTATUS
SdOmap4ChildBlockIoReset (
    PVOID DiskToken
    );

KSTATUS
SdOmap4ChildBlockIoRead (
    PVOID DiskToken,
    PIO_BUFFER IoBuffer,
    ULONGLONG BlockAddress,
    UINTN BlockCount,
    PUINTN BlocksCompleted
    );

KSTATUS
SdOmap4ChildBlockIoWrite (
    PVOID DiskToken,
    PIO_BUFFER IoBuffer,
    ULONGLONG BlockAddress,
    UINTN BlockCount,
    PUINTN BlocksCompleted
    );

KSTATUS
SdOmap4PerformIoPolled (
    PIRP_READ_WRITE IrpReadWrite,
    PSD_OMAP4_CHILD Child,
    BOOL Write,
    BOOL LockRequired
    );

KSTATUS
SdOmap4GetSetBusWidth (
    PSD_CONTROLLER Controller,
    PVOID Context,
    BOOL Set
    );

//
// -------------------------------------------------------------------- Globals
//

PDRIVER SdOmap4Driver = NULL;
UUID SdOmap4DiskInterfaceUuid = UUID_DISK_INTERFACE;

DISK_INTERFACE SdOmap4DiskInterfaceTemplate = {
    DISK_INTERFACE_VERSION,
    NULL,
    0,
    0,
    NULL,
    SdOmap4ChildBlockIoReset,
    SdOmap4ChildBlockIoRead,
    SdOmap4ChildBlockIoWrite
};

//
// ------------------------------------------------------------------ Functions
//

KSTATUS
DriverEntry (
    PDRIVER Driver
    )

/*++

Routine Description:

    This routine is the entry point for the SD/MMC driver. It registers its
    other dispatch functions, and performs driver-wide initialization.

Arguments:

    Driver - Supplies a pointer to the driver object.

Return Value:

    STATUS_SUCCESS on success.

    Failure code on error.

--*/

{

    DRIVER_FUNCTION_TABLE FunctionTable;
    KSTATUS Status;

    SdOmap4Driver = Driver;
    RtlZeroMemory(&FunctionTable, sizeof(DRIVER_FUNCTION_TABLE));
    FunctionTable.Version = DRIVER_FUNCTION_TABLE_VERSION;
    FunctionTable.AddDevice = SdOmap4AddDevice;
    FunctionTable.DispatchStateChange = SdOmap4DispatchStateChange;
    FunctionTable.DispatchOpen = SdOmap4DispatchOpen;
    FunctionTable.DispatchClose = SdOmap4DispatchClose;
    FunctionTable.DispatchIo = SdOmap4DispatchIo;
    FunctionTable.DispatchSystemControl = SdOmap4DispatchSystemControl;
    Status = IoRegisterDriverFunctions(Driver, &FunctionTable);
    return Status;
}

KSTATUS
SdOmap4AddDevice (
    PVOID Driver,
    PSTR DeviceId,
    PSTR ClassId,
    PSTR CompatibleIds,
    PVOID DeviceToken
    )

/*++

Routine Description:

    This routine is called when a device is detected for which the SD/MMC driver
    acts as the function driver. The driver will attach itself to the stack.

Arguments:

    Driver - Supplies a pointer to the driver being called.

    DeviceId - Supplies a pointer to a string with the device ID.

    ClassId - Supplies a pointer to a string containing the device's class ID.

    CompatibleIds - Supplies a pointer to a string containing device IDs
        that would be compatible with this device.

    DeviceToken - Supplies an opaque token that the driver can use to identify
        the device in the system. This token should be used when attaching to
        the stack.

Return Value:

    STATUS_SUCCESS on success.

    Failure code if the driver was unsuccessful in attaching itself.

--*/

{

    PSD_OMAP4_CONTEXT Context;
    KSTATUS Status;

    //
    // Allocate non-paged pool because this device could be the paging device.
    //

    Context = MmAllocateNonPagedPool(sizeof(SD_OMAP4_CONTEXT),
                                     SD_ALLOCATION_TAG);

    if (Context == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto AddDeviceEnd;
    }

    RtlZeroMemory(Context, sizeof(SD_OMAP4_CONTEXT));
    Context->Type = SdOmap4Parent;
    Context->Flags = SD_OMAP4_DEVICE_FLAG_INSERTION_PENDING;
    Context->InterruptHandle = INVALID_HANDLE;
    Context->Lock = KeCreateQueuedLock();
    if (Context->Lock == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto AddDeviceEnd;
    }

    if (IoAreDeviceIdsEqual(DeviceId, SD_OMAP4_DEVICE_ID) != 0) {
        Context->Soc = SdTiSocOmap4;

    } else if (IoAreDeviceIdsEqual(DeviceId, SD_AM335_DEVICE_ID) != 0) {
        Context->Soc = SdTiSocAm335;

    } else {

        ASSERT(FALSE);

        Status = STATUS_NO_ELIGIBLE_DEVICES;
        goto AddDeviceEnd;
    }

    Status = IoAttachDriverToDevice(Driver, DeviceToken, Context);
    if (!KSUCCESS(Status)) {
        goto AddDeviceEnd;
    }

AddDeviceEnd:
    if (!KSUCCESS(Status)) {
        if (Context != NULL) {
            MmFreeNonPagedPool(Context);
        }
    }

    return Status;
}

VOID
SdOmap4DispatchStateChange (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    )

/*++

Routine Description:

    This routine handles State Change IRPs.

Arguments:

    Irp - Supplies a pointer to the I/O request packet.

    DeviceContext - Supplies the context pointer supplied by the driver when it
        attached itself to the driver stack. Presumably this pointer contains
        driver-specific device context.

    IrpContext - Supplies the context pointer supplied by the driver when
        the IRP was created.

Return Value:

    None.

--*/

{

    PSD_OMAP4_CONTEXT Context;

    ASSERT(Irp->MajorCode == IrpMajorStateChange);

    Context = DeviceContext;
    switch (Context->Type) {
    case SdOmap4Parent:
        SdOmap4ParentDispatchStateChange(Irp, Context, IrpContext);
        break;

    case SdOmap4Child:
        SdOmap4ChildDispatchStateChange(Irp,
                                        (PSD_OMAP4_CHILD)Context,
                                        IrpContext);

        break;

    default:

        ASSERT(FALSE);

        break;
    }

    return;
}

VOID
SdOmap4DispatchOpen (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    )

/*++

Routine Description:

    This routine handles Open IRPs.

Arguments:

    Irp - Supplies a pointer to the I/O request packet.

    DeviceContext - Supplies the context pointer supplied by the driver when it
        attached itself to the driver stack. Presumably this pointer contains
        driver-specific device context.

    IrpContext - Supplies the context pointer supplied by the driver when
        the IRP was created.

Return Value:

    None.

--*/

{

    PSD_OMAP4_CHILD Child;

    Child = DeviceContext;

    //
    // Only the child can be opened or closed.
    //

    if (Child->Type != SdOmap4Child) {
        return;
    }

    SdOmap4pChildAddReference(Child);
    IoCompleteIrp(SdOmap4Driver, Irp, STATUS_SUCCESS);
    return;
}

VOID
SdOmap4DispatchClose (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    )

/*++

Routine Description:

    This routine handles Close IRPs.

Arguments:

    Irp - Supplies a pointer to the I/O request packet.

    DeviceContext - Supplies the context pointer supplied by the driver when it
        attached itself to the driver stack. Presumably this pointer contains
        driver-specific device context.

    IrpContext - Supplies the context pointer supplied by the driver when
        the IRP was created.

Return Value:

    None.

--*/

{

    PSD_OMAP4_CHILD Child;

    Child = DeviceContext;
    if (Child->Type != SdOmap4Child) {
        return;
    }

    SdOmap4pChildReleaseReference(Child);
    IoCompleteIrp(SdOmap4Driver, Irp, STATUS_SUCCESS);
    return;
}

VOID
SdOmap4DispatchIo (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    )

/*++

Routine Description:

    This routine handles I/O IRPs.

Arguments:

    Irp - Supplies a pointer to the I/O request packet.

    DeviceContext - Supplies the context pointer supplied by the driver when it
        attached itself to the driver stack. Presumably this pointer contains
        driver-specific device context.

    IrpContext - Supplies the context pointer supplied by the driver when
        the IRP was created.

Return Value:

    None.

--*/

{

    UINTN BlockCount;
    ULONGLONG BlockOffset;
    UINTN BytesToComplete;
    PSD_OMAP4_CHILD Child;
    BOOL CompleteIrp;
    ULONGLONG IoOffset;
    ULONG IrpReadWriteFlags;
    KSTATUS Status;
    BOOL Write;

    ASSERT(KeGetRunLevel() == RunLevelLow);

    Child = DeviceContext;
    if (Child->Type != SdOmap4Child) {

        ASSERT(FALSE);

        return;
    }

    CompleteIrp = TRUE;
    Write = FALSE;
    if (Irp->MinorCode == IrpMinorIoWrite) {
        Write = TRUE;
    }

    //
    // Polled I/O is shared by a few code paths and prepares the IRP for I/O
    // further down the stack. It should also only be hit in the down direction
    // path as it always completes the IRP.
    //

    if ((Child->Flags & SD_OMAP4_CHILD_FLAG_DMA_SUPPORTED) == 0) {

        ASSERT(Irp->Direction == IrpDown);

        Status = SdOmap4PerformIoPolled(&(Irp->U.ReadWrite),
                                        Child,
                                        Write,
                                        TRUE);

        goto DispatchIoEnd;
    }

    //
    // Set the IRP read/write flags for the preparation and completion steps.
    //

    IrpReadWriteFlags = IRP_READ_WRITE_FLAG_DMA;
    if (Write != FALSE) {
        IrpReadWriteFlags |= IRP_READ_WRITE_FLAG_WRITE;
    }

    //
    // If the IRP is on the way up, then clean up after the DMA as this IRP is
    // still sitting in the channel. An IRP going up is already complete.
    //

    if (Irp->Direction == IrpUp) {
        CompleteIrp = FALSE;

        ASSERT(Irp == Child->Irp);

        Child->Irp = NULL;
        KeReleaseQueuedLock(Child->ControllerLock);
        Status = IoCompleteReadWriteIrp(&(Irp->U.ReadWrite), IrpReadWriteFlags);
        if (!KSUCCESS(Status)) {
            IoUpdateIrpStatus(Irp, Status);
        }

    //
    // Start the DMA on the way down.
    //

    } else {
        BytesToComplete = Irp->U.ReadWrite.IoSizeInBytes;
        IoOffset = Irp->U.ReadWrite.IoOffset;
        Irp->U.ReadWrite.IoBytesCompleted = 0;

        ASSERT(Irp->U.ReadWrite.IoBuffer != NULL);
        ASSERT((Child->BlockCount != 0) && (Child->BlockShift != 0));
        ASSERT(IS_ALIGNED(IoOffset, 1 << Child->BlockShift) != FALSE);
        ASSERT(IS_ALIGNED(BytesToComplete, 1 << Child->BlockShift) != FALSE);

        //
        // Before acquiring the controller's lock and starting the DMA, prepare
        // the I/O context for SD (i.e. it must use physical addresses that
        // are less than 4GB and be sector size aligned).
        //

        Status = IoPrepareReadWriteIrp(&(Irp->U.ReadWrite),
                                       1 << Child->BlockShift,
                                       0,
                                       MAX_ULONG,
                                       IrpReadWriteFlags);

        if (!KSUCCESS(Status)) {
            goto DispatchIoEnd;
        }

        //
        // Lock the controller to serialize access to the hardware.
        //

        KeAcquireQueuedLock(Child->ControllerLock);
        if ((Child->Flags & SD_OMAP4_CHILD_FLAG_MEDIA_PRESENT) == 0) {
            KeReleaseQueuedLock(Child->ControllerLock);
            IoCompleteReadWriteIrp(&(Irp->U.ReadWrite), IrpReadWriteFlags);
            Status = STATUS_NO_MEDIA;
            goto DispatchIoEnd;
        }

        //
        // If it's DMA, just send it on through.
        //

        Irp->U.ReadWrite.NewIoOffset = IoOffset;
        Child->Irp = Irp;
        BlockOffset = IoOffset >> Child->BlockShift;
        BlockCount = BytesToComplete >> Child->BlockShift;
        CompleteIrp = FALSE;
        IoPendIrp(SdOmap4Driver, Irp);

        //
        // Make sure the system isn't trying to do I/O off the end of the
        // disk.
        //

        ASSERT(BlockOffset < Child->BlockCount);
        ASSERT(BlockCount >= 1);

        SdStandardBlockIoDma(Child->Controller,
                             BlockOffset,
                             BlockCount,
                             Irp->U.ReadWrite.IoBuffer,
                             0,
                             Write,
                             SdOmap4DmaCompletion,
                             Child);

        //
        // DMA transfers are self perpetuating, so after kicking off this
        // first transfer, return. This returns with the lock held because
        // I/O is still in progress.
        //

        ASSERT(KeIsQueuedLockHeld(Child->ControllerLock) != FALSE);
        ASSERT(CompleteIrp == FALSE);
    }

DispatchIoEnd:
    if (CompleteIrp != FALSE) {
        IoCompleteIrp(SdOmap4Driver, Irp, Status);
    }

    return;
}

VOID
SdOmap4DispatchSystemControl (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    )

/*++

Routine Description:

    This routine handles System Control IRPs.

Arguments:

    Irp - Supplies a pointer to the I/O request packet.

    DeviceContext - Supplies the context pointer supplied by the driver when it
        attached itself to the driver stack. Presumably this pointer contains
        driver-specific device context.

    IrpContext - Supplies the context pointer supplied by the driver when
        the IRP was created.

Return Value:

    None.

--*/

{

    PSD_OMAP4_CHILD Child;
    PVOID Context;
    PSYSTEM_CONTROL_FILE_OPERATION FileOperation;
    PSYSTEM_CONTROL_LOOKUP Lookup;
    PFILE_PROPERTIES Properties;
    ULONGLONG PropertiesFileSize;
    KSTATUS Status;

    Context = Irp->U.SystemControl.SystemContext;
    Child = DeviceContext;

    //
    // Only child devices are supported.
    //

    if (Child->Type != SdOmap4Child) {

        ASSERT(Child->Type == SdOmap4Parent);

        return;
    }

    switch (Irp->MinorCode) {
    case IrpMinorSystemControlLookup:
        Lookup = (PSYSTEM_CONTROL_LOOKUP)Context;
        Status = STATUS_PATH_NOT_FOUND;
        if (Lookup->Root != FALSE) {

            //
            // Enable opening of the root as a single file.
            //

            Properties = &(Lookup->Properties);
            Properties->FileId = 0;
            Properties->Type = IoObjectBlockDevice;
            Properties->HardLinkCount = 1;
            Properties->BlockCount = Child->BlockCount;
            Properties->BlockSize = 1 << Child->BlockShift;
            WRITE_INT64_SYNC(&(Properties->FileSize),
                             Child->BlockCount << Child->BlockShift);

            Status = STATUS_SUCCESS;
        }

        IoCompleteIrp(SdOmap4Driver, Irp, Status);
        break;

    //
    // Writes to the disk's properties are not allowed. Fail if the data
    // has changed.
    //

    case IrpMinorSystemControlWriteFileProperties:
        FileOperation = (PSYSTEM_CONTROL_FILE_OPERATION)Context;
        Properties = FileOperation->FileProperties;
        READ_INT64_SYNC(&(Properties->FileSize), &PropertiesFileSize);
        if ((Properties->FileId != 0) ||
            (Properties->Type != IoObjectBlockDevice) ||
            (Properties->HardLinkCount != 1) ||
            (Properties->BlockSize != (1 << Child->BlockShift)) ||
            (Properties->BlockCount != Child->BlockCount) ||
            (PropertiesFileSize != (Child->BlockCount << Child->BlockShift))) {

            Status = STATUS_NOT_SUPPORTED;

        } else {
            Status = STATUS_SUCCESS;
        }

        IoCompleteIrp(SdOmap4Driver, Irp, Status);
        break;

    //
    // Do not support hard disk device truncation.
    //

    case IrpMinorSystemControlTruncate:
        IoCompleteIrp(SdOmap4Driver, Irp, STATUS_NOT_SUPPORTED);
        break;

    //
    // Gather and return device information.
    //

    case IrpMinorSystemControlDeviceInformation:
        break;

    case IrpMinorSystemControlSynchronize:
        IoCompleteIrp(SdOmap4Driver, Irp, STATUS_SUCCESS);
        break;

    //
    // Ignore everything unrecognized.
    //

    default:

        ASSERT(FALSE);

        break;
    }

    return;
}

VOID
SdOmap4ParentDispatchStateChange (
    PIRP Irp,
    PSD_OMAP4_CONTEXT Context,
    PVOID IrpContext
    )

/*++

Routine Description:

    This routine handles State Change IRPs for a parent device.

Arguments:

    Irp - Supplies a pointer to the I/O request packet.

    Context - Supplies a pointer to the controller information.

    IrpContext - Supplies the context pointer supplied by the driver when
        the IRP was created.

Return Value:

    None.

--*/

{

    KSTATUS Status;

    if (Irp->Direction == IrpUp) {
        if (!KSUCCESS(IoGetIrpStatus(Irp))) {
            return;
        }

        switch (Irp->MinorCode) {
        case IrpMinorQueryResources:
            Status = SdOmap4ParentProcessResourceRequirements(Irp, Context);
            if (!KSUCCESS(Status)) {
                IoCompleteIrp(SdOmap4Driver, Irp, Status);
            }

            break;

        case IrpMinorStartDevice:
            Status = SdOmap4ParentStartDevice(Irp, Context);
            if (!KSUCCESS(Status)) {
                IoCompleteIrp(SdOmap4Driver, Irp, Status);
            }

            break;

        case IrpMinorQueryChildren:
            Status = SdOmap4ParentQueryChildren(Irp, Context);
            if (!KSUCCESS(Status)) {
                IoCompleteIrp(SdOmap4Driver, Irp, Status);
            }

            break;

        default:
            break;
        }
    }

    return;
}

VOID
SdOmap4ChildDispatchStateChange (
    PIRP Irp,
    PSD_OMAP4_CHILD Child,
    PVOID IrpContext
    )

/*++

Routine Description:

    This routine handles State Change IRPs for a parent device.

Arguments:

    Irp - Supplies a pointer to the I/O request packet.

    Child - Supplies a pointer to the child device information.

    IrpContext - Supplies the context pointer supplied by the driver when
        the IRP was created.

Return Value:

    None.

--*/

{

    BOOL CompleteIrp;
    KSTATUS Status;

    ASSERT(Irp->MajorCode == IrpMajorStateChange);

    //
    // The IRP is on its way down the stack. Do most processing here.
    //

    if (Irp->Direction == IrpDown) {
        Status = STATUS_NOT_SUPPORTED;
        CompleteIrp = TRUE;
        switch (Irp->MinorCode) {
        case IrpMinorQueryResources:
            Status = STATUS_SUCCESS;
            break;

        case IrpMinorStartDevice:
            Status = STATUS_SUCCESS;
            if (Child->DiskInterface.DiskToken == NULL) {
                RtlCopyMemory(&(Child->DiskInterface),
                              &SdOmap4DiskInterfaceTemplate,
                              sizeof(DISK_INTERFACE));

                Child->DiskInterface.BlockSize = 1 << Child->BlockShift;
                Child->DiskInterface.BlockCount = Child->BlockCount;
                Child->DiskInterface.DiskToken = Child;
                Status = IoCreateInterface(&SdOmap4DiskInterfaceUuid,
                                           Child->Device,
                                           &(Child->DiskInterface),
                                           sizeof(DISK_INTERFACE));

                if (!KSUCCESS(Status)) {
                    Child->DiskInterface.DiskToken = NULL;
                }
            }

            break;

        case IrpMinorQueryChildren:
            Irp->U.QueryChildren.Children = NULL;
            Irp->U.QueryChildren.ChildCount = 0;
            Status = STATUS_SUCCESS;
            break;

        case IrpMinorQueryInterface:
            break;

        case IrpMinorRemoveDevice:
            if (Child->DiskInterface.DiskToken != NULL) {
                Status = IoDestroyInterface(&SdOmap4DiskInterfaceUuid,
                                            Child->Device,
                                            &(Child->DiskInterface));

                ASSERT(KSUCCESS(Status));

                Child->DiskInterface.DiskToken = NULL;
            }

            SdOmap4pChildReleaseReference(Child);
            Status = STATUS_SUCCESS;
            break;

        //
        // Pass all other IRPs down.
        //

        default:
            CompleteIrp = FALSE;
            break;
        }

        //
        // Complete the IRP unless there's a reason not to.
        //

        if (CompleteIrp != FALSE) {
            IoCompleteIrp(SdOmap4Driver, Irp, Status);
        }

    //
    // The IRP is completed and is on its way back up.
    //

    } else {

        ASSERT(Irp->Direction == IrpUp);
    }

    return;
}

KSTATUS
SdOmap4ParentProcessResourceRequirements (
    PIRP Irp,
    PSD_OMAP4_CONTEXT Device
    )

/*++

Routine Description:

    This routine filters through the resource requirements presented by the
    bus for a SD OMAP4 Host controller. It adds an interrupt vector requirement
    for any interrupt line requested.

Arguments:

    Irp - Supplies a pointer to the I/O request packet.

    Device - Supplies a pointer to this device.

Return Value:

    Status code.

--*/

{

    PRESOURCE_CONFIGURATION_LIST Requirements;
    KSTATUS Status;
    RESOURCE_REQUIREMENT VectorRequirement;

    ASSERT((Irp->MajorCode == IrpMajorStateChange) &&
           (Irp->MinorCode == IrpMinorQueryResources));

    //
    // Initialize a nice interrupt vector requirement in preparation.
    //

    RtlZeroMemory(&VectorRequirement, sizeof(RESOURCE_REQUIREMENT));
    VectorRequirement.Type = ResourceTypeInterruptVector;
    VectorRequirement.Minimum = 0;
    VectorRequirement.Maximum = -1;
    VectorRequirement.Length = 1;

    //
    // Loop through all configuration lists, creating a vector for each line.
    //

    Requirements = Irp->U.QueryResources.ResourceRequirements;
    Status = IoCreateAndAddInterruptVectorsForLines(Requirements,
                                                    &VectorRequirement);

    if (!KSUCCESS(Status)) {
        goto ProcessResourceRequirementsEnd;
    }

ProcessResourceRequirementsEnd:
    return Status;
}

KSTATUS
SdOmap4ParentStartDevice (
    PIRP Irp,
    PSD_OMAP4_CONTEXT Device
    )

/*++

Routine Description:

    This routine starts up the OMAP4 SD controller.

Arguments:

    Irp - Supplies a pointer to the I/O request packet.

    Device - Supplies a pointer to this SD OMAP4 device.

Return Value:

    Status code.

--*/

{

    PRESOURCE_ALLOCATION Allocation;
    PRESOURCE_ALLOCATION_LIST AllocationList;
    IO_CONNECT_INTERRUPT_PARAMETERS Connect;
    PRESOURCE_ALLOCATION ControllerBase;
    PRESOURCE_ALLOCATION LineAllocation;
    SD_INITIALIZATION_BLOCK Parameters;
    KSTATUS Status;

    ControllerBase = NULL;

    //
    // Loop through the allocated resources to get the controller base and the
    // interrupt.
    //

    AllocationList = Irp->U.StartDevice.ProcessorLocalResources;
    Allocation = IoGetNextResourceAllocation(AllocationList, NULL);
    while (Allocation != NULL) {

        //
        // If the resource is an interrupt vector, then it should have an
        // owning interrupt line allocation.
        //

        if (Allocation->Type == ResourceTypeInterruptVector) {

            //
            // Currently only one interrupt resource is expected.
            //

            ASSERT((Device->Flags &
                    SD_OMAP4_DEVICE_FLAG_INTERRUPT_RESOURCES_FOUND) == 0);

            ASSERT(Allocation->OwningAllocation != NULL);

            //
            // Save the line and vector number.
            //

            LineAllocation = Allocation->OwningAllocation;
            Device->InterruptLine = LineAllocation->Allocation;
            Device->InterruptVector = Allocation->Allocation;
            RtlAtomicOr32(&(Device->Flags),
                          SD_OMAP4_DEVICE_FLAG_INTERRUPT_RESOURCES_FOUND);

        } else if (Allocation->Type == ResourceTypePhysicalAddressSpace) {

            ASSERT(ControllerBase == NULL);

            ControllerBase = Allocation;
        }

        //
        // Get the next allocation in the list.
        //

        Allocation = IoGetNextResourceAllocation(AllocationList, Allocation);
    }

    //
    // Fail to start if the controller base was not found.
    //

    if ((ControllerBase == NULL) ||
        (ControllerBase->Length < SD_OMAP4_CONTROLLER_LENGTH)) {

        Status = STATUS_INVALID_CONFIGURATION;
        goto StartDeviceEnd;
    }

    //
    // Initialize OMAP4 specific stuff.
    //

    if (Device->ControllerBase == NULL) {
        Device->ControllerBase = MmMapPhysicalAddress(
                                                    ControllerBase->Allocation,
                                                    ControllerBase->Length,
                                                    TRUE,
                                                    FALSE,
                                                    TRUE);

        if (Device->ControllerBase == NULL) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto StartDeviceEnd;
        }
    }

    if (Device->Soc == SdTiSocOmap4) {
        OmapI2cInitialize();
        Status = Omap4Twl6030InitializeMmcPower();
        if (!KSUCCESS(Status)) {

            ASSERT(FALSE);

            goto StartDeviceEnd;
        }
    }

    Status = SdOmap4ResetController(Device);
    if (Status == STATUS_NO_MEDIA) {
        Status = STATUS_SUCCESS;
        goto StartDeviceEnd;

    } else if (!KSUCCESS(Status)) {
        RtlDebugPrint("SdOmap4ResetController Failed: %x\n", Status);
        goto StartDeviceEnd;
    }

    //
    // Initialize the standard SD controller.
    //

    if (Device->Controller == NULL) {
        RtlZeroMemory(&Parameters, sizeof(SD_INITIALIZATION_BLOCK));
        Parameters.StandardControllerBase =
               Device->ControllerBase + SD_OMAP4_CONTROLLER_SD_REGISTER_OFFSET;

        Parameters.Voltages = SD_VOLTAGE_29_30 |
                              SD_VOLTAGE_30_31 |
                              SD_VOLTAGE_165_195;

        Parameters.HostCapabilities = SD_MODE_4BIT |
                                      SD_MODE_8BIT |
                                      SD_MODE_HIGH_SPEED |
                                      SD_MODE_AUTO_CMD12;

        Parameters.FundamentalClock = SD_OMAP4_FUNDAMENTAL_CLOCK_SPEED;
        Parameters.FunctionTable.GetSetBusWidth = SdOmap4GetSetBusWidth;
        Parameters.ConsumerContext = Device;
        Device->Controller = SdCreateController(&Parameters);
        if (Device->Controller == NULL) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto StartDeviceEnd;
        }
    }

    //
    // Attempt to connect the interrupt before initializing the controller. The
    // initialization process may trigger some interrupts.
    //

    if (Device->InterruptHandle == INVALID_HANDLE) {
        RtlZeroMemory(&Connect, sizeof(IO_CONNECT_INTERRUPT_PARAMETERS));
        Connect.Version = IO_CONNECT_INTERRUPT_PARAMETERS_VERSION;
        Connect.Device = Irp->Device;
        Connect.LineNumber = Device->InterruptLine;
        Connect.Vector = Device->InterruptVector;
        Connect.InterruptServiceRoutine = SdOmap4InterruptService;
        Connect.DispatchServiceRoutine = SdOmap4InterruptServiceDispatch;
        Connect.Context = Device;
        Connect.Interrupt = &(Device->InterruptHandle);
        Status = IoConnectInterrupt(&Connect);
        if (!KSUCCESS(Status)) {
            goto StartDeviceEnd;
        }

        Device->Controller->InterruptHandle = Device->InterruptHandle;
    }

    Status = STATUS_SUCCESS;

StartDeviceEnd:
    if (!KSUCCESS(Status)) {
        if (Device->InterruptHandle != INVALID_HANDLE) {
            IoDisconnectInterrupt(Device->InterruptHandle);
            Device->InterruptHandle = INVALID_HANDLE;
        }

        if (Device->Controller != NULL) {
            SdDestroyController(Device->Controller);
            Device->Controller = NULL;
        }
    }

    return Status;
}

KSTATUS
SdOmap4ParentQueryChildren (
    PIRP Irp,
    PSD_OMAP4_CONTEXT Device
    )

/*++

Routine Description:

    This routine potentially enumerates the disk device for the SD OMAP4
    controller.

Arguments:

    Irp - Supplies a pointer to the I/O request packet.

    Device - Supplies a pointer to this device.

Return Value:

    Status code.

--*/

{

    ULONG BlockSize;
    ULONG FlagsMask;
    PSD_OMAP4_CHILD NewChild;
    ULONG OldFlags;
    KSTATUS Status;

    NewChild = NULL;

    //
    // Check to see if any changes to the children are pending.
    //

    FlagsMask = ~(SD_OMAP4_DEVICE_FLAG_INSERTION_PENDING |
                  SD_OMAP4_DEVICE_FLAG_REMOVAL_PENDING);

    OldFlags = RtlAtomicAnd32(&(Device->Flags), FlagsMask);

    //
    // If either a removal or insertion is pending, clean out the old child.
    // In practice, not all removals interrupt, meaning that two insertions can
    // arrive in a row.
    //

    FlagsMask = SD_OMAP4_DEVICE_FLAG_INSERTION_PENDING |
                SD_OMAP4_DEVICE_FLAG_REMOVAL_PENDING;

    if ((OldFlags & FlagsMask) != 0) {
        if (Device->Child != NULL) {
            KeAcquireQueuedLock(Device->Lock);
            Device->Child->Flags &= ~SD_OMAP4_CHILD_FLAG_MEDIA_PRESENT;
            KeReleaseQueuedLock(Device->Lock);
            Device->Child = NULL;
        }
    }

    //
    // If an insertion is pending, try to enumerate the child.
    //

    if ((OldFlags & SD_OMAP4_DEVICE_FLAG_INSERTION_PENDING) != 0) {

        ASSERT(Device->Child == NULL);

        Status = SdInitializeController(Device->Controller, FALSE);
        if (!KSUCCESS(Status)) {
            if (Status == STATUS_TIMEOUT) {
                Status = STATUS_SUCCESS;

            } else {
                RtlDebugPrint("SdInitializeController failed: %x\n", Status);
            }

            goto ParentQueryChildrenEnd;
        }

        NewChild = SdOmap4pCreateChild(Device);
        if (NewChild == NULL) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto ParentQueryChildrenEnd;
        }

        BlockSize = 0;
        Status = SdGetMediaParameters(NewChild->Controller,
                                      &(NewChild->BlockCount),
                                      &BlockSize);

        if (!KSUCCESS(Status)) {
            if (Status == STATUS_NO_MEDIA) {
                Status = STATUS_SUCCESS;
            }

            goto ParentQueryChildrenEnd;
        }

        ASSERT(POWER_OF_2(BlockSize) != FALSE);

        NewChild->BlockShift = RtlCountTrailingZeros32(BlockSize);

        //
        // Try to enable DMA, but it's okay if it doesn't succeed.
        //

        Status = SdStandardInitializeDma(Device->Controller);
        if (KSUCCESS(Status)) {
            NewChild->Flags |= SD_OMAP4_CHILD_FLAG_DMA_SUPPORTED;

        } else if (Status == STATUS_NO_MEDIA) {
            Status = STATUS_SUCCESS;
            goto ParentQueryChildrenEnd;
        }

        NewChild->Flags |= SD_OMAP4_CHILD_FLAG_MEDIA_PRESENT;
        Status = IoCreateDevice(SdOmap4Driver,
                                NewChild,
                                Irp->Device,
                                "SdCard",
                                DISK_CLASS_ID,
                                NULL,
                                &(NewChild->Device));

        if (!KSUCCESS(Status)) {
            return Status;
        }

        Device->Child = NewChild;
        NewChild = NULL;
    }

    //
    // If there's no child present, don't enumerate it.
    //

    if (Device->Child == NULL) {
        return STATUS_SUCCESS;
    }

    ASSERT((Device->Child != NULL) && (Device->Child->Device != NULL));

    //
    // Enumerate the one child.
    //

    Status = IoMergeChildArrays(Irp,
                                &(Device->Child->Device),
                                1,
                                SD_ALLOCATION_TAG);

ParentQueryChildrenEnd:
    if (NewChild != NULL) {

        ASSERT(NewChild->Device == NULL);

        SdOmap4pChildReleaseReference(NewChild);
    }

    return Status;
}

KSTATUS
SdOmap4ResetController (
    PSD_OMAP4_CONTEXT Device
    )

/*++

Routine Description:

    This routine resets the OMAP4 SD controller and card.

Arguments:

    Device - Supplies a pointer to this SD OMAP4 device.

Return Value:

    Status code.

--*/

{

    ULONG ClockControl;
    ULONG Divisor;
    ULONGLONG Frequency;
    ULONG Register;
    KSTATUS Status;
    ULONGLONG Timeout;
    ULONG Value;

    Frequency = HlQueryTimeCounterFrequency();

    //
    // Perform a soft reset on the HSMMC part.
    //

    SD_OMAP4_WRITE_REGISTER(Device,
                            SD_OMAP4_SYSCONFIG_REGISTER,
                            SD_OMAP4_SYSCONFIG_SOFT_RESET);

    Status = STATUS_TIMEOUT;
    Timeout = KeGetRecentTimeCounter() + (Frequency * SD_OMAP4_TIMEOUT);
    do {
        if ((SD_OMAP4_READ_REGISTER(Device, SD_OMAP4_SYSSTATUS_REGISTER) &
             SD_OMAP4_SYSSTATUS_RESET_DONE) != 0) {

            Status = STATUS_SUCCESS;
            break;
        }

    } while (KeGetRecentTimeCounter() <= Timeout);

    if (!KSUCCESS(Status)) {
        return Status;
    }

    //
    // Perform a reset on the SD controller.
    //

    Register = SD_OMAP4_CONTROLLER_SD_REGISTER_OFFSET + SdRegisterClockControl;
    Value = SD_OMAP4_READ_REGISTER(Device, Register);
    Value |= SD_CLOCK_CONTROL_RESET_ALL;
    Status = STATUS_TIMEOUT;
    Timeout = KeGetRecentTimeCounter() + (Frequency * SD_OMAP4_TIMEOUT);
    do {
        if ((SD_OMAP4_READ_REGISTER(Device, Register) &
             SD_CLOCK_CONTROL_RESET_ALL) == 0) {

            Status = STATUS_SUCCESS;
            break;
        }

    } while (KeGetRecentTimeCounter() <= Timeout);

    if (!KSUCCESS(Status)) {
        return Status;
    }

    Register = SD_OMAP4_CONTROLLER_SD_REGISTER_OFFSET +
               SdRegisterInterruptStatus;

    SD_OMAP4_WRITE_REGISTER(Device, Register, 0xFFFFFFFF);

    //
    // Set up the host control register for 3 Volts.
    //

    Register = SD_OMAP4_CONTROLLER_SD_REGISTER_OFFSET + SdRegisterHostControl;
    Value = SD_HOST_CONTROL_POWER_3V0;
    SD_OMAP4_WRITE_REGISTER(Device, Register, Value);

    //
    // Add the 3.0V and 1.8V capabilities to the capability register.
    //

    Register = SD_OMAP4_CONTROLLER_SD_REGISTER_OFFSET + SdRegisterCapabilities;
    Value = SD_OMAP4_READ_REGISTER(Device, Register);
    Value |= SD_CAPABILITY_VOLTAGE_3V0 | SD_CAPABILITY_VOLTAGE_1V8;
    SD_OMAP4_WRITE_REGISTER(Device, Register, Value);

    //
    // Initialize the HSMMC control register.
    //

    Register = SD_OMAP4_CON_REGISTER;
    Value = SD_OMAP4_READ_REGISTER(Device, Register) &
            SD_OMAP4_CON_DEBOUNCE_MASK;

    SD_OMAP4_WRITE_REGISTER(Device, Register, Value);

    //
    // Set up the clock control register for 400kHz in preparation for sending
    // CMD0 with INIT held.
    //

    Register = SD_OMAP4_CONTROLLER_SD_REGISTER_OFFSET + SdRegisterClockControl;
    ClockControl = SD_CLOCK_CONTROL_DEFAULT_TIMEOUT <<
                   SD_CLOCK_CONTROL_TIMEOUT_SHIFT;

    SD_OMAP4_WRITE_REGISTER(Device, Register, ClockControl);
    Divisor = SD_OMAP4_INITIAL_DIVISOR;
    ClockControl |= (Divisor & SD_CLOCK_CONTROL_DIVISOR_MASK) <<
                    SD_CLOCK_CONTROL_DIVISOR_SHIFT;

    ClockControl |= (Divisor & SD_CLOCK_CONTROL_DIVISOR_HIGH_MASK) >>
                    SD_CLOCK_CONTROL_DIVISOR_HIGH_SHIFT;

    ClockControl |= SD_CLOCK_CONTROL_INTERNAL_CLOCK_ENABLE;
    SD_OMAP4_WRITE_REGISTER(Device, Register, ClockControl);
    SD_OMAP4_WRITE_REGISTER(Device, Register, ClockControl);
    Status = STATUS_TIMEOUT;
    Timeout = KeGetRecentTimeCounter() + (Frequency * SD_OMAP4_TIMEOUT);
    do {
        Value = SD_OMAP4_READ_REGISTER(Device, Register);
        if ((Value & SD_CLOCK_CONTROL_CLOCK_STABLE) != 0) {
            Status = STATUS_SUCCESS;
            break;
        }

    } while (KeGetRecentTimeCounter() <= Timeout);

    if (!KSUCCESS(Status)) {
        return Status;
    }

    ClockControl |= SD_CLOCK_CONTROL_SD_CLOCK_ENABLE;
    SD_OMAP4_WRITE_REGISTER(Device, Register, ClockControl);
    Register = SD_OMAP4_CONTROLLER_SD_REGISTER_OFFSET + SdRegisterHostControl;
    Value = SD_OMAP4_READ_REGISTER(Device, Register);
    Value |= SD_HOST_CONTROL_POWER_ENABLE;
    SD_OMAP4_WRITE_REGISTER(Device, Register, Value);
    Register = SD_OMAP4_CONTROLLER_SD_REGISTER_OFFSET +
               SdRegisterInterruptStatusEnable;

    Value = SD_INTERRUPT_STATUS_ENABLE_DEFAULT_MASK;
    SD_OMAP4_WRITE_REGISTER(Device, Register, Value);

    //
    // Reset the card by setting the init flag and issuing the card reset (go
    // idle, command 0) command.
    //

    Register = SD_OMAP4_CON_REGISTER;
    Value = SD_OMAP4_READ_REGISTER(Device, Register) | SD_OMAP4_CON_INIT |
            SD_OMAP4_CON_DMA_MASTER;

    SD_OMAP4_WRITE_REGISTER(Device, Register, Value);

    //
    // Write a 0 to the command register to issue the command.
    //

    Register = SD_OMAP4_CONTROLLER_SD_REGISTER_OFFSET + SdRegisterCommand;
    SD_OMAP4_WRITE_REGISTER(Device, Register, 0);

    //
    // Wait for the command to complete.
    //

    Register = SD_OMAP4_CONTROLLER_SD_REGISTER_OFFSET +
               SdRegisterInterruptStatus;

    Status = STATUS_TIMEOUT;
    Timeout = KeGetRecentTimeCounter() + (Frequency * SD_OMAP4_TIMEOUT);
    do {
        Value = SD_OMAP4_READ_REGISTER(Device, Register);
        if (Value != 0) {
            if ((Value & SD_INTERRUPT_STATUS_COMMAND_COMPLETE) != 0) {
                Status = STATUS_SUCCESS;

            } else if ((Value &
                        SD_INTERRUPT_STATUS_COMMAND_TIMEOUT_ERROR) != 0) {

                Status = STATUS_NO_MEDIA;

            } else {
                Status = STATUS_DEVICE_IO_ERROR;
            }

            SD_OMAP4_WRITE_REGISTER(Device, Register, Value);
            break;
        }

    } while (KeGetRecentTimeCounter() <= Timeout);

    //
    // Disable the INIT line.
    //

    Register = SD_OMAP4_CON_REGISTER;
    Value = SD_OMAP4_READ_REGISTER(Device, Register) & (~SD_OMAP4_CON_INIT);
    SD_OMAP4_WRITE_REGISTER(Device, Register, Value);
    if (!KSUCCESS(Status)) {
        return Status;
    }

    return STATUS_SUCCESS;
}

INTERRUPT_STATUS
SdOmap4InterruptService (
    PVOID Context
    )

/*++

Routine Description:

    This routine implements the OMAP4 SD interrupt service routine.

Arguments:

    Context - Supplies the context pointer given to the system when the
        interrupt was connected. In this case, this points to the OMAP4 SD
        controller.

Return Value:

    Interrupt status.

--*/

{

    PSD_OMAP4_CONTEXT Device;

    Device = Context;
    return SdStandardInterruptService(Device->Controller);
}

INTERRUPT_STATUS
SdOmap4InterruptServiceDispatch (
    PVOID Context
    )

/*++

Routine Description:

    This routine implements the dispatch level OMAP4 SD interrupt service
    routine.

Arguments:

    Context - Supplies the context pointer given to the system when the
        interrupt was connected. In this case, this points to the OMAP4 SD
        controller.

Return Value:

    Interrupt status.

--*/

{

    PSD_OMAP4_CONTEXT Device;

    Device = Context;
    return SdStandardInterruptServiceDispatch(Device->Controller);
}

VOID
SdOmap4DmaCompletion (
    PSD_CONTROLLER Controller,
    PVOID Context,
    UINTN BytesTransferred,
    KSTATUS Status
    )

/*++

Routine Description:

    This routine is called by the SD library when a DMA transfer completes.
    This routine is called from a DPC and, as a result, can get called back
    at dispatch level.

Arguments:

    Controller - Supplies a pointer to the controller.

    Context - Supplies a context pointer passed to the library when the DMA
        request was issued.

    BytesTransferred - Supplies the number of bytes transferred in the request.

    Status - Supplies the status code representing the completion of the I/O.

Return Value:

    None.

--*/

{

    UINTN BlockCount;
    ULONGLONG BlockOffset;
    PSD_OMAP4_CHILD Child;
    ULONGLONG IoOffset;
    UINTN IoSize;
    PIRP Irp;
    BOOL Write;

    Child = Context;
    Irp = Child->Irp;

    ASSERT(Irp != NULL);

    if (!KSUCCESS(Status)) {
        RtlDebugPrint("SD OMAP4 Failed: %x\n", Status);
        IoCompleteIrp(SdOmap4Driver, Irp, Status);
        return;
    }

    Irp->U.ReadWrite.IoBytesCompleted += BytesTransferred;
    Irp->U.ReadWrite.NewIoOffset += BytesTransferred;

    //
    // If this transfer's over, unlock and complete the IRP.
    //

    if (Irp->U.ReadWrite.IoBytesCompleted ==
        Irp->U.ReadWrite.IoSizeInBytes) {

        IoCompleteIrp(SdOmap4Driver, Irp, Status);
        return;
    }

    IoOffset = Irp->U.ReadWrite.IoOffset + Irp->U.ReadWrite.IoBytesCompleted;
    BlockOffset = IoOffset >> Child->BlockShift;
    IoSize = Irp->U.ReadWrite.IoSizeInBytes -
             Irp->U.ReadWrite.IoBytesCompleted;

    BlockCount = IoSize >> Child->BlockShift;
    Write = FALSE;
    if (Irp->MinorCode == IrpMinorIoWrite) {
        Write = TRUE;
    }

    SdStandardBlockIoDma(Child->Controller,
                         BlockOffset,
                         BlockCount,
                         Irp->U.ReadWrite.IoBuffer,
                         Irp->U.ReadWrite.IoBytesCompleted,
                         Write,
                         SdOmap4DmaCompletion,
                         Child);

    return;
}

PSD_OMAP4_CHILD
SdOmap4pCreateChild (
    PSD_OMAP4_CONTEXT Device
    )

/*++

Routine Description:

    This routine creates an SD child context.

Arguments:

    Device - Supplies a pointer to the parent device to which the child belongs.

Return Value:

    Returns a pointer to the new child on success or NULL on failure.

--*/

{

    PSD_OMAP4_CHILD Child;

    Child = MmAllocateNonPagedPool(sizeof(SD_OMAP4_CHILD), SD_ALLOCATION_TAG);
    if (Child == NULL) {
        return NULL;
    }

    RtlZeroMemory(Child, sizeof(SD_OMAP4_CHILD));
    Child->Type = SdOmap4Child;
    Child->Parent = Device;
    Child->Controller = Device->Controller;
    Child->ControllerLock = Device->Lock;
    Child->ReferenceCount = 1;
    return Child;
}

VOID
SdOmap4pDestroyChild (
    PSD_OMAP4_CHILD Child
    )

/*++

Routine Description:

    This routine destroys the given SD child device.

Arguments:

    Child - Supplies a pointer to the SD child device to destroy.

Return Value:

    None.

--*/

{

    ASSERT(((Child->Flags & SD_OMAP4_CHILD_FLAG_MEDIA_PRESENT) == 0) ||
           (Child->Device == NULL));

    ASSERT(Child->DiskInterface.DiskToken == NULL);
    ASSERT(Child->Irp == NULL);

    MmFreeNonPagedPool(Child);
    return;
}

VOID
SdOmap4pChildAddReference (
    PSD_OMAP4_CHILD Child
    )

/*++

Routine Description:

    This routine adds a reference to SD child device.

Arguments:

    Child - Supplies a pointer to the SD child device.

Return Value:

    None.

--*/

{

    ULONG OldReferenceCount;

    OldReferenceCount = RtlAtomicAdd32(&(Child->ReferenceCount), 1);

    ASSERT((OldReferenceCount != 0) && (OldReferenceCount < 0x10000000));

    return;
}

VOID
SdOmap4pChildReleaseReference (
    PSD_OMAP4_CHILD Child
    )

/*++

Routine Description:

    This routine releases a reference from the SD child.

Arguments:

    Child - Supplies a pointer to the SD child.

Return Value:

    None.

--*/

{

    ULONG OldReferenceCount;

    OldReferenceCount = RtlAtomicAdd32(&(Child->ReferenceCount), (ULONG)-1);

    ASSERT((OldReferenceCount != 0) && (OldReferenceCount < 0x10000000));

    if (OldReferenceCount == 1) {
        SdOmap4pDestroyChild(Child);
    }

    return;
}

KSTATUS
SdOmap4ChildBlockIoReset (
    PVOID DiskToken
    )

/*++

Routine Description:

    This routine must be called immediately before using the block read and
    write routines in order to allow the disk to reset any I/O channels in
    preparation for imminent block I/O. This routine is called at high run
    level.

Arguments:

    DiskToken - Supplies an opaque token for the disk. The appropriate token is
        retrieved by querying the disk device information.

Return Value:

    Status code.

--*/

{

    PSD_OMAP4_CHILD Child;
    KSTATUS Status;

    ASSERT(KeGetRunLevel() == RunLevelHigh);

    Child = (PSD_OMAP4_CHILD)DiskToken;

    //
    // Put the SD controller into critical execution mode.
    //

    SdSetCriticalMode(Child->Controller, TRUE);

    //
    // Abort any current transaction that might have been left incomplete
    // when the crash occurred.
    //

    Status = SdAbortTransaction(Child->Controller, FALSE);
    return Status;
}

KSTATUS
SdOmap4ChildBlockIoRead (
    PVOID DiskToken,
    PIO_BUFFER IoBuffer,
    ULONGLONG BlockAddress,
    UINTN BlockCount,
    PUINTN BlocksCompleted
    )

/*++

Routine Description:

    This routine reads the block contents from the disk into the given I/O
    buffer using polled I/O. It does so without acquiring any locks or
    allocating any resources, as this routine is used for crash dump support
    when the system is in a very fragile state. This routine must be called at
    high level.

Arguments:

    DiskToken - Supplies an opaque token for the disk. The appropriate token is
        retrieved by querying the disk device information.

    IoBuffer - Supplies a pointer to the I/O buffer where the data will be read.

    BlockAddress - Supplies the block index to read (for physical disk, this is
        the LBA).

    BlockCount - Supplies the number of blocks to read.

    BlocksCompleted - Supplies a pointer that receives the total number of
        blocks read.

Return Value:

    Status code.

--*/

{

    PSD_OMAP4_CHILD Child;
    IRP_READ_WRITE IrpReadWrite;
    KSTATUS Status;

    ASSERT(KeGetRunLevel() == RunLevelHigh);

    Child = (PSD_OMAP4_CHILD)DiskToken;
    IrpReadWrite.IoBuffer = IoBuffer;
    IrpReadWrite.IoOffset = BlockAddress << Child->BlockShift;
    IrpReadWrite.IoSizeInBytes = BlockCount << Child->BlockShift;

    //
    // As this read routine is meant for critical code paths (crash dump),
    // indicate that the channel should not be locked when performing the I/O.
    // It may be that some other thread holds the lock, which would cause a
    // dead lock as all other processors and threads are likely frozen.
    //

    Status = SdOmap4PerformIoPolled(&IrpReadWrite, Child, FALSE, FALSE);
    *BlocksCompleted = IrpReadWrite.IoBytesCompleted >> Child->BlockShift;
    return Status;
}

KSTATUS
SdOmap4ChildBlockIoWrite (
    PVOID DiskToken,
    PIO_BUFFER IoBuffer,
    ULONGLONG BlockAddress,
    UINTN BlockCount,
    PUINTN BlocksCompleted
    )

/*++

Routine Description:

    This routine writes the contents of the given I/O buffer to the disk using
    polled I/O. It does so without acquiring any locks or allocating any
    resources, as this routine is used for crash dump support when the system
    is in a very fragile state. This routine must be called at high level.

Arguments:

    DiskToken - Supplies an opaque token for the disk. The appropriate token is
        retrieved by querying the disk device information.

    IoBuffer - Supplies a pointer to the I/O buffer containing the data to
        write.

    BlockAddress - Supplies the block index to write to (for physical disk,
        this is the LBA).

    BlockCount - Supplies the number of blocks to write.

    BlocksCompleted - Supplies a pointer that receives the total number of
        blocks written.

Return Value:

    Status code.

--*/

{

    PSD_OMAP4_CHILD Child;
    IRP_READ_WRITE IrpReadWrite;
    KSTATUS Status;

    ASSERT(KeGetRunLevel() == RunLevelHigh);

    Child = (PSD_OMAP4_CHILD)DiskToken;
    IrpReadWrite.IoBuffer = IoBuffer;
    IrpReadWrite.IoOffset = BlockAddress << Child->BlockShift;
    IrpReadWrite.IoSizeInBytes = BlockCount << Child->BlockShift;

    //
    // As this write routine is meant for critical code paths (crash dump),
    // indicate that the channel should not be locked when performing the I/O.
    // It may be that some other thread holds the lock, which would cause a
    // dead lock as all other processors and threads are likely frozen.
    //

    Status = SdOmap4PerformIoPolled(&IrpReadWrite, Child, TRUE, FALSE);
    *BlocksCompleted = IrpReadWrite.IoBytesCompleted >> Child->BlockShift;
    return Status;
}

KSTATUS
SdOmap4PerformIoPolled (
    PIRP_READ_WRITE IrpReadWrite,
    PSD_OMAP4_CHILD Child,
    BOOL Write,
    BOOL LockRequired
    )

/*++

Routine Description:

    This routine performs polled I/O data transfers.

Arguments:

    IrpReadWrite - Supplies a pointer to the IRP's read/write context.

    Child - Supplies a pointer to the SD child device.

    Write - Supplies a boolean indicating if this is a read operation (TRUE) or
        a write operation (FALSE).

    LockRequired - Supplies a boolean indicating if this operation requires the
        child's controller lock to be acquired (TRUE) or not (FALSE).

Return Value:

    None.

--*/

{

    UINTN BlockCount;
    ULONGLONG BlockOffset;
    UINTN BytesRemaining;
    UINTN BytesThisRound;
    KSTATUS CompletionStatus;
    PIO_BUFFER_FRAGMENT Fragment;
    UINTN FragmentIndex;
    UINTN FragmentOffset;
    PIO_BUFFER IoBuffer;
    UINTN IoBufferOffset;
    ULONG IrpReadWriteFlags;
    BOOL LockHeld;
    BOOL ReadWriteIrpPrepared;
    KSTATUS Status;
    PVOID VirtualAddress;

    IrpReadWrite->IoBytesCompleted = 0;
    LockHeld = FALSE;
    ReadWriteIrpPrepared = FALSE;

    ASSERT(IrpReadWrite->IoBuffer != NULL);
    ASSERT(Child->Type == SdOmap4Child);
    ASSERT((Child->BlockCount != 0) && (Child->BlockShift != 0));

    //
    // Validate the supplied I/O buffer is aligned and big enough.
    //

    IrpReadWriteFlags = IRP_READ_WRITE_FLAG_POLLED;
    if (Write != FALSE) {
        IrpReadWriteFlags |= IRP_READ_WRITE_FLAG_WRITE;
    }

    Status = IoPrepareReadWriteIrp(IrpReadWrite,
                                   1 << Child->BlockShift,
                                   0,
                                   MAX_ULONGLONG,
                                   IrpReadWriteFlags);

    if (!KSUCCESS(Status)) {
        goto PerformIoPolledEnd;
    }

    ReadWriteIrpPrepared = TRUE;

    //
    // Make sure the I/O buffer is mapped before use. SD depends on the buffer
    // being mapped.
    //

    IoBuffer = IrpReadWrite->IoBuffer;
    Status = MmMapIoBuffer(IoBuffer, FALSE, FALSE, FALSE);
    if (!KSUCCESS(Status)) {
        goto PerformIoPolledEnd;
    }

    //
    // Find the starting fragment based on the current offset.
    //

    IoBufferOffset = MmGetIoBufferCurrentOffset(IoBuffer);
    FragmentIndex = 0;
    FragmentOffset = 0;
    while (IoBufferOffset != 0) {

        ASSERT(FragmentIndex < IoBuffer->FragmentCount);

        Fragment = &(IoBuffer->Fragment[FragmentIndex]);
        if (IoBufferOffset < Fragment->Size) {
            FragmentOffset = IoBufferOffset;
            break;
        }

        IoBufferOffset -= Fragment->Size;
        FragmentIndex += 1;
    }

    if (LockRequired != FALSE) {
        KeAcquireQueuedLock(Child->ControllerLock);
        LockHeld = TRUE;
    }

    if ((Child->Flags & SD_OMAP4_CHILD_FLAG_MEDIA_PRESENT) == 0) {
        Status = STATUS_NO_MEDIA;
        goto PerformIoPolledEnd;
    }

    //
    // Loop reading in or writing out each fragment in the I/O buffer.
    //

    BytesRemaining = IrpReadWrite->IoSizeInBytes;

    ASSERT(IS_ALIGNED(BytesRemaining, 1 << Child->BlockShift) != FALSE);
    ASSERT(IS_ALIGNED(IrpReadWrite->IoOffset, 1 << Child->BlockShift) != FALSE);

    BlockOffset = IrpReadWrite->IoOffset >> Child->BlockShift;
    while (BytesRemaining != 0) {

        ASSERT(FragmentIndex < IoBuffer->FragmentCount);

        Fragment = &(IoBuffer->Fragment[FragmentIndex]);
        VirtualAddress = Fragment->VirtualAddress + FragmentOffset;
        BytesThisRound = Fragment->Size - FragmentOffset;
        if (BytesRemaining < BytesThisRound) {
            BytesThisRound = BytesRemaining;
        }

        ASSERT(IS_ALIGNED(BytesThisRound, (1 << Child->BlockShift)) != FALSE);

        BlockCount = BytesThisRound >> Child->BlockShift;

        //
        // Make sure the system isn't trying to do I/O off the end of the disk.
        //

        ASSERT(BlockOffset < Child->BlockCount);
        ASSERT(BlockCount >= 1);

        Status = SdBlockIoPolled(Child->Controller,
                                 BlockOffset,
                                 BlockCount,
                                 VirtualAddress,
                                 Write);

        if (!KSUCCESS(Status)) {
            goto PerformIoPolledEnd;
        }

        BlockOffset += BlockCount;
        BytesRemaining -= BytesThisRound;
        IrpReadWrite->IoBytesCompleted += BytesThisRound;
        FragmentOffset += BytesThisRound;
        if (FragmentOffset >= Fragment->Size) {
            FragmentIndex += 1;
            FragmentOffset = 0;
        }
    }

    Status = STATUS_SUCCESS;

PerformIoPolledEnd:
    if (LockHeld != FALSE) {
        KeReleaseQueuedLock(Child->ControllerLock);
    }

    if (ReadWriteIrpPrepared != FALSE) {
        CompletionStatus = IoCompleteReadWriteIrp(IrpReadWrite,
                                                  IrpReadWriteFlags);

        if (!KSUCCESS(CompletionStatus) && KSUCCESS(Status)) {
            Status = CompletionStatus;
        }
    }

    IrpReadWrite->NewIoOffset = IrpReadWrite->IoOffset +
                                IrpReadWrite->IoBytesCompleted;

    return Status;
}

KSTATUS
SdOmap4GetSetBusWidth (
    PSD_CONTROLLER Controller,
    PVOID Context,
    BOOL Set
    )

/*++

Routine Description:

    This routine gets or sets the controller's bus width. The bus width is
    stored in the controller structure.

Arguments:

    Controller - Supplies a pointer to the controller.

    Context - Supplies a context pointer passed to the SD/MMC library upon
        creation of the controller.

    Set - Supplies a boolean indicating whether the bus width should be queried
        or set.

Return Value:

    Status code.

--*/

{

    PSD_OMAP4_CONTEXT Device;
    KSTATUS Status;
    ULONG Value;

    Device = Context;
    Status = SdStandardGetSetBusWidth(Controller, Context, Set);
    if (!KSUCCESS(Status)) {
        return Status;
    }

    Value = SD_OMAP4_READ_REGISTER(Device, SD_OMAP4_CON_REGISTER);
    switch (Controller->BusWidth) {
    case 1:
    case 4:
        Value &= ~SD_OMAP4_CON_8BIT;
        break;

    case 8:
        Value |= SD_OMAP4_CON_8BIT;
        break;

    default:

        ASSERT(FALSE);

        return STATUS_INVALID_PARAMETER;
    }

    SD_OMAP4_WRITE_REGISTER(Device, SD_OMAP4_CON_REGISTER, Value);
    return STATUS_SUCCESS;
}
