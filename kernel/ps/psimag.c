/*++

Copyright (c) 2012 Minoca Corp. All Rights Reserved

Module Name:

    psimag.c

Abstract:

    This module implements the underlying support routines for the image
    library to be run in the kernel.

Author:

    Evan Green 14-Oct-2012

Environment:

    Kernel

--*/

//
// ------------------------------------------------------------------- Includes
//

#include <minoca/kernel.h>
#include <minoca/kdebug.h>
#include "processp.h"

//
// ---------------------------------------------------------------- Definitions
//

#define PROCESS_USER_MODULE_MAX_NAME 100
#define PROCESS_USER_MODULE_MAX_COUNT 200

//
// ------------------------------------------------------ Data Type Definitions
//

/*++

Structure Description:

    This structure stores associations between images in two processes during
    a clone operation.

Members:

    SourceImage - Stores a pointer to the loaded image the source process.

    DestinationImage - Stores a pointer to the loaded image in the destination
        process.

--*/

typedef struct _IMAGE_ASSOCIATION {
    PLOADED_IMAGE SourceImage;
    PLOADED_IMAGE DestinationImage;
} IMAGE_ASSOCIATION, *PIMAGE_ASSOCIATION;

//
// ----------------------------------------------- Internal Function Prototypes
//

PVOID
PspImAllocateMemory (
    ULONG Size,
    ULONG Tag
    );

VOID
PspImFreeMemory (
    PVOID Allocation
    );

KSTATUS
PspImOpenFile (
    PVOID SystemContext,
    PSTR BinaryName,
    PIMAGE_FILE_INFORMATION File
    );

VOID
PspImCloseFile (
    PIMAGE_FILE_INFORMATION File
    );

KSTATUS
PspImLoadFile (
    PIMAGE_FILE_INFORMATION File,
    PIMAGE_BUFFER Buffer
    );

KSTATUS
PspImReadFile (
    PIMAGE_FILE_INFORMATION File,
    ULONGLONG Offset,
    UINTN Size,
    PIMAGE_BUFFER Buffer
    );

VOID
PspImUnloadBuffer (
    PIMAGE_FILE_INFORMATION File,
    PIMAGE_BUFFER Buffer
    );

KSTATUS
PspImAllocateAddressSpace (
    PVOID SystemContext,
    PIMAGE_FILE_INFORMATION File,
    ULONG Size,
    HANDLE *Handle,
    PVOID *Address,
    PVOID *AccessibleAddress
    );

VOID
PspImFreeAddressSpace (
    HANDLE Handle,
    PVOID Address,
    UINTN Size
    );

KSTATUS
PspImMapImageSegment (
    HANDLE AddressSpaceHandle,
    PVOID AddressSpaceAllocation,
    PIMAGE_FILE_INFORMATION File,
    ULONGLONG FileOffset,
    PIMAGE_SEGMENT Segment,
    PIMAGE_SEGMENT PreviousSegment
    );

VOID
PspImUnmapImageSegment (
    HANDLE AddressSpaceHandle,
    PIMAGE_SEGMENT Segment
    );

KSTATUS
PspImNotifyImageLoad (
    PLOADED_IMAGE Image
    );

VOID
PspImNotifyImageUnload (
    PLOADED_IMAGE Image
    );

VOID
PspImInvalidateInstructionCacheRegion (
    PVOID Address,
    ULONG Size
    );

PSTR
PspImGetEnvironmentVariable (
    PSTR Variable
    );

KSTATUS
PspImFinalizeSegments (
    HANDLE AddressSpaceHandle,
    PIMAGE_SEGMENT Segments,
    UINTN SegmentCount
    );

KSTATUS
PspImCloneImage (
    PKPROCESS Source,
    PKPROCESS Destination,
    PLOADED_IMAGE SourceImage,
    PLOADED_IMAGE *NewDestinationImage
    );

PLOADED_IMAGE
PspImGetAssociatedImage (
    PLOADED_IMAGE QueryImage,
    PIMAGE_ASSOCIATION AssociationMapping,
    ULONG AssociationCount
    );

KSTATUS
PspLoadProcessImageIntoKernelDebugger (
    PKPROCESS Process,
    PLOADED_IMAGE Image
    );

//
// -------------------------------------------------------------------- Globals
//

//
// Set this global to always load all user mode images into the kernel mode
// debugger. Setting this is great for debugging as all usermode symbols are
// always visible. It's not on by default however because it's wasteful (as
// it costs lots of non-paged pool allocations) and adds buckets of symbols to
// the debugger.
//

BOOL PsKdLoadAllImages = FALSE;

//
// Store a handle to the OS base library.
//

PIO_HANDLE PsOsBaseLibrary;

//
// Store the image library function table.
//

IM_IMPORT_TABLE PsImFunctionTable = {
    PspImAllocateMemory,
    PspImFreeMemory,
    PspImOpenFile,
    PspImCloseFile,
    PspImLoadFile,
    PspImReadFile,
    PspImUnloadBuffer,
    PspImAllocateAddressSpace,
    PspImFreeAddressSpace,
    PspImMapImageSegment,
    PspImUnmapImageSegment,
    PspImNotifyImageLoad,
    PspImNotifyImageUnload,
    PspImInvalidateInstructionCacheRegion,
    PspImGetEnvironmentVariable,
    PspImFinalizeSegments
};

//
// ------------------------------------------------------------------ Functions
//

KSTATUS
PspInitializeImageSupport (
    PVOID KernelLowestAddress,
    PLIST_ENTRY ListHead
    )

/*++

Routine Description:

    This routine initializes the image library for use in the kernel.

Arguments:

    KernelLowestAddress - Supplies the lowest address of the kernel's image.
        This is used to avoid loading the kernel image in the debugger twice.

    ListHead - Supplies a pointer to the head of the list of loaded images.

Return Value:

    Status code.

--*/

{

    PLIST_ENTRY CurrentEntry;
    PLOADED_IMAGE Image;
    IMAGE_BUFFER ImageBuffer;
    PKPROCESS KernelProcess;
    PLOADED_IMAGE NewImage;
    KSTATUS Status;

    RtlZeroMemory(&ImageBuffer, sizeof(IMAGE_BUFFER));
    Status = ImInitialize(&PsImFunctionTable);
    if (!KSUCCESS(Status)) {
        goto InitializeImageSupportEnd;
    }

    KernelProcess = PsGetKernelProcess();
    CurrentEntry = ListHead->Next;
    while (CurrentEntry != ListHead) {
        Image = LIST_VALUE(CurrentEntry, LOADED_IMAGE, ListEntry);
        CurrentEntry = CurrentEntry->Next;
        ImageBuffer.Data = Image->LoadedLowestAddress;
        ImageBuffer.Size = Image->Size;
        Status = ImAddImage(Image->BinaryName, &ImageBuffer, &NewImage);
        if (!KSUCCESS(Status)) {

            ASSERT(FALSE);

            goto InitializeImageSupportEnd;
        }

        NewImage->Flags = Image->Flags | IMAGE_FLAG_INITIALIZED |
                          IMAGE_FLAG_RELOCATED | IMAGE_FLAG_IMPORTS_LOADED;

        NewImage->LoadFlags = Image->LoadFlags;
        NewImage->ImportDepth = Image->ImportDepth;
        NewImage->File.ModificationDate = Image->File.ModificationDate;
        NewImage->File.Size = Image->File.Size;
        NewImage->Size = Image->Size;
        INSERT_BEFORE(&(NewImage->ListEntry), &(KernelProcess->ImageListHead));
        KernelProcess->ImageCount += 1;
        KernelProcess->ImageListSignature +=
                                       NewImage->File.ModificationDate +
                                       (UINTN)(NewImage->LoadedLowestAddress);

        //
        // Load this image into the kernel debugger, but skip the kernel
        // image as that was already loaded.
        //

        if (NewImage->LoadedLowestAddress != KernelLowestAddress) {
            Status = PspLoadProcessImageIntoKernelDebugger(KernelProcess,
                                                           NewImage);

            if (!KSUCCESS(Status)) {
                goto InitializeImageSupportEnd;
            }
        }
    }

    Status = STATUS_SUCCESS;

InitializeImageSupportEnd:
    return Status;
}

KSTATUS
PspImCloneProcessImages (
    PKPROCESS Source,
    PKPROCESS Destination
    )

/*++

Routine Description:

    This routine makes a copy of the given process' image list.

Arguments:

    Source - Supplies a pointer to the source process.

    Destination - Supplies a pointer to the destination process.

Return Value:

    Status code.

--*/

{

    PIMAGE_ASSOCIATION Association;
    ULONG AssociationIndex;
    PLIST_ENTRY CurrentEntry;
    ULONG ImageCount;
    ULONG ImportIndex;
    PLOADED_IMAGE NewImage;
    PLOADED_IMAGE SourceImage;
    KSTATUS Status;

    ASSERT(KeGetRunLevel() == RunLevelLow);

    PsAcquireImageListLock(Source);
    ImageCount = Source->ImageCount;

    //
    // Allocate space for the association mapping.
    //

    Association = MmAllocatePagedPool(sizeof(IMAGE_ASSOCIATION) * ImageCount,
                                      PS_ALLOCATION_TAG);

    if (Association == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto ImCloneProcessImagesEnd;
    }

    //
    // Loop through copying images.
    //

    AssociationIndex = 0;
    CurrentEntry = Source->ImageListHead.Next;
    while (CurrentEntry != &(Source->ImageListHead)) {
        SourceImage = LIST_VALUE(CurrentEntry, LOADED_IMAGE, ListEntry);
        CurrentEntry = CurrentEntry->Next;

        ASSERT(AssociationIndex < ImageCount);

        //
        // Clone the image.
        //

        Status = PspImCloneImage(Source, Destination, SourceImage, &NewImage);
        if (!KSUCCESS(Status)) {
            goto ImCloneProcessImagesEnd;
        }

        //
        // Remember the association between source and destination image.
        //

        Association[AssociationIndex].SourceImage = SourceImage;
        Association[AssociationIndex].DestinationImage = NewImage;
        AssociationIndex += 1;
    }

    //
    // Now loop through the new process image list and restore all the import
    // relationships.
    //

    CurrentEntry = Source->ImageListHead.Next;
    while (CurrentEntry != &(Source->ImageListHead)) {
        NewImage = LIST_VALUE(CurrentEntry, LOADED_IMAGE, ListEntry);
        CurrentEntry = CurrentEntry->Next;
        if (NewImage->ImportCount == 0) {
            continue;
        }

        //
        // Get the source image associated with this destination image.
        //

        SourceImage = PspImGetAssociatedImage(NewImage,
                                              Association,
                                              ImageCount);

        ASSERT(SourceImage != NULL);
        ASSERT(SourceImage->ImportCount == NewImage->ImportCount);

        //
        // Loop through and match up every import in the source with its
        // corresponding image in the destination.
        //

        for (ImportIndex = 0;
             ImportIndex < NewImage->ImportCount;
             ImportIndex += 1) {

            NewImage->Imports[ImportIndex] =
                     PspImGetAssociatedImage(SourceImage->Imports[ImportIndex],
                                             Association,
                                             ImageCount);

            ASSERT(NewImage->Imports[ImportIndex] != NULL);
        }
    }

    Status = STATUS_SUCCESS;

ImCloneProcessImagesEnd:
    PsReleaseImageListLock(Source);
    if (Association != NULL) {
        MmFreePagedPool(Association);
    }

    return Status;
}

VOID
PspImUnloadAllImages (
    PKPROCESS Process
    )

/*++

Routine Description:

    This routine unloads all images in the given process.

Arguments:

    Process - Supplies a pointer to the process whose images should be unloaded.

Return Value:

    None.

--*/

{

    PLIST_ENTRY CurrentEntry;
    PLOADED_IMAGE Image;

    //
    // Unload all images. Be careful traversing this list as it will shift
    // as images and their imports are unloaded.
    //

    PsAcquireImageListLock(Process);
    while (LIST_EMPTY(&(Process->ImageListHead)) == FALSE) {
        CurrentEntry = Process->ImageListHead.Next;
        while (CurrentEntry != &(Process->ImageListHead)) {
            Image = LIST_VALUE(CurrentEntry, LOADED_IMAGE, ListEntry);
            if (Image->ImportDepth == 0) {

                //
                // Mark the image as having unload called on it, and then
                // unload the image.
                //

                Image->ImportDepth = -1;
                ImImageReleaseReference(Image);
                break;
            }

            CurrentEntry = CurrentEntry->Next;
        }

        //
        // If the image list is not empty but no images were found with a
        // depth of zero, then a reference counting problem has occurred.
        // Decrementing the reference count on all images with a depth
        // of zero should cause a domino effect that unloads all images.
        //

        ASSERT(CurrentEntry != &(Process->ImageListHead));
    }

    PsReleaseImageListLock(Process);
    return;
}

KSTATUS
PspProcessUserModeModuleChange (
    PPROCESS_DEBUG_MODULE_CHANGE ModuleChangeUser
    )

/*++

Routine Description:

    This routine handles module change notifications from user mode.

Arguments:

    ModuleChangeUser - Supplies the module change notification from user mode.

Return Value:

    None.

--*/

{

    UINTN AllocationSize;
    PROCESS_DEBUG_MODULE_CHANGE Change;
    PLIST_ENTRY CurrentEntry;
    PLOADED_IMAGE CurrentImage;
    PLOADED_IMAGE ExistingImage;
    LOADED_IMAGE Image;
    BOOL LockHeld;
    PLOADED_IMAGE NewImage;
    PKPROCESS Process;
    KSTATUS Status;

    LockHeld = FALSE;
    NewImage = NULL;
    Process = PsGetCurrentProcess();
    Status = MmCopyFromUserMode(&Change,
                                ModuleChangeUser,
                                sizeof(PROCESS_DEBUG_MODULE_CHANGE));

    if (!KSUCCESS(Status)) {
        goto ProcessUserModeModuleChangeEnd;
    }

    if (Change.Version < PROCESS_DEBUG_MODULE_CHANGE_VERSION) {
        Status = STATUS_NOT_SUPPORTED;
        goto ProcessUserModeModuleChangeEnd;
    }

    Status = MmCopyFromUserMode(&Image, Change.Image, sizeof(LOADED_IMAGE));
    if (!KSUCCESS(Status)) {
        goto ProcessUserModeModuleChangeEnd;
    }

    if (Image.Format != ImageElf32) {

        ASSERT(FALSE);

        Status = STATUS_INVALID_PARAMETER;
        goto ProcessUserModeModuleChangeEnd;
    }

    //
    // Try to find a module matching this base address.
    //

    PsAcquireImageListLock(Process);
    LockHeld = TRUE;
    CurrentEntry = Process->ImageListHead.Next;
    ExistingImage = NULL;
    while (CurrentEntry != &(Process->ImageListHead)) {
        CurrentImage = LIST_VALUE(CurrentEntry, LOADED_IMAGE, ListEntry);
        CurrentEntry = CurrentEntry->Next;
        if (CurrentImage->LoadedLowestAddress == Image.LoadedLowestAddress) {
            ExistingImage = CurrentImage;
            break;
        }
    }

    //
    // Handle an unload.
    //

    if (Change.Load == FALSE) {
        if (ExistingImage == NULL) {
            Status = STATUS_NOT_FOUND;
            goto ProcessUserModeModuleChangeEnd;
        }

        if ((ExistingImage->LoadFlags & IMAGE_LOAD_FLAG_PLACEHOLDER) == 0) {
            Status = STATUS_INVALID_PARAMETER;
            goto ProcessUserModeModuleChangeEnd;
        }

        PspImNotifyImageUnload(ExistingImage);
        LIST_REMOVE(&(ExistingImage->ListEntry));
        PspImFreeMemory(ExistingImage);
        Status = STATUS_SUCCESS;
        goto ProcessUserModeModuleChangeEnd;
    }

    //
    // This is a load. Handle shenanigans.
    //

    if (ExistingImage != NULL) {
        Status = STATUS_RESOURCE_IN_USE;
        goto ProcessUserModeModuleChangeEnd;
    }

    if (Process->ImageCount >= PROCESS_USER_MODULE_MAX_COUNT) {
        Status = STATUS_TOO_MANY_HANDLES;
        goto ProcessUserModeModuleChangeEnd;
    }

    if (Change.BinaryNameSize > PROCESS_USER_MODULE_MAX_NAME) {
        Status = STATUS_NAME_TOO_LONG;
        goto ProcessUserModeModuleChangeEnd;
    }

    if (Change.BinaryNameSize == 0) {
        Status = STATUS_INVALID_PARAMETER;
        goto ProcessUserModeModuleChangeEnd;
    }

    //
    // Create a faked up image.
    //

    AllocationSize = sizeof(LOADED_IMAGE) + Change.BinaryNameSize;
    NewImage = PspImAllocateMemory(AllocationSize, PS_IMAGE_ALLOCATION_TAG);
    if (NewImage == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto ProcessUserModeModuleChangeEnd;
    }

    RtlZeroMemory(NewImage, sizeof(LOADED_IMAGE));
    NewImage->BinaryName = (PSTR)(NewImage + 1);
    Status = MmCopyFromUserMode(NewImage->BinaryName,
                                Image.BinaryName,
                                Change.BinaryNameSize);

    if (!KSUCCESS(Status)) {
        goto ProcessUserModeModuleChangeEnd;
    }

    NewImage->BinaryName[Change.BinaryNameSize - 1] = '\0';
    NewImage->File.Handle = INVALID_HANDLE;
    NewImage->AllocatorHandle = INVALID_HANDLE;
    NewImage->Format = Image.Format;
    NewImage->Machine = Image.Machine;
    NewImage->Size = Image.Size;
    NewImage->DeclaredBase = Image.DeclaredBase;
    NewImage->PreferredLowestAddress = Image.PreferredLowestAddress;
    NewImage->LoadedLowestAddress = Image.LoadedLowestAddress;
    NewImage->EntryPoint = Image.EntryPoint;
    NewImage->ReferenceCount = 1;
    NewImage->LoadFlags = IMAGE_LOAD_FLAG_PLACEHOLDER;
    INSERT_BEFORE(&(NewImage->ListEntry), &(Process->ImageListHead));
    Status = PspImNotifyImageLoad(NewImage);
    if (!KSUCCESS(Status)) {
        LIST_REMOVE(&(NewImage->ListEntry));
        goto ProcessUserModeModuleChangeEnd;
    }

    Status = STATUS_SUCCESS;

ProcessUserModeModuleChangeEnd:
    if (LockHeld != FALSE) {
        PsReleaseImageListLock(Process);
    }

    if (!KSUCCESS(Status)) {
        if (NewImage != NULL) {
            PspImFreeMemory(NewImage);
        }
    }

    return Status;
}

KSTATUS
PspLoadProcessImagesIntoKernelDebugger (
    PKPROCESS Process
    )

/*++

Routine Description:

    This routine loads the images in the given process into the kernel debugger.

Arguments:

    Process - Supplies a pointer to the process.

Return Value:

    Status code.

--*/

{

    PLIST_ENTRY CurrentEntry;
    PLOADED_IMAGE Image;
    KSTATUS Status;
    KSTATUS TotalStatus;

    ASSERT((KeGetRunLevel() == RunLevelLow) &&
           (Process != PsGetKernelProcess()));

    PsAcquireImageListLock(Process);
    TotalStatus = STATUS_SUCCESS;
    CurrentEntry = Process->ImageListHead.Next;
    while (CurrentEntry != &(Process->ImageListHead)) {
        Image = LIST_VALUE(CurrentEntry, LOADED_IMAGE, ListEntry);
        CurrentEntry = CurrentEntry->Next;
        Status = PspLoadProcessImageIntoKernelDebugger(Process, Image);
        if (!KSUCCESS(Status)) {
            TotalStatus = Status;
        }
    }

    PsReleaseImageListLock(Process);
    return TotalStatus;
}

//
// --------------------------------------------------------- Internal Functions
//

PVOID
PspImAllocateMemory (
    ULONG Size,
    ULONG Tag
    )

/*++

Routine Description:

    This routine allocates memory from the kernel for the image library.

Arguments:

    Size - Supplies the number of bytes required for the memory allocation.

    Tag - Supplies a 32-bit ASCII identifier used to tag the memroy allocation.

Return Value:

    Returns a pointer to the memory allocation on success.

    NULL on failure.

--*/

{

    return MmAllocatePagedPool(Size, Tag);
}

VOID
PspImFreeMemory (
    PVOID Allocation
    )

/*++

Routine Description:

    This routine frees memory to the kernel allocated by the image library.

Arguments:

    Allocation - Supplies a pointer the allocation to free.

Return Value:

    None.

--*/

{

    MmFreePagedPool(Allocation);
    return;
}

KSTATUS
PspImOpenFile (
    PVOID SystemContext,
    PSTR BinaryName,
    PIMAGE_FILE_INFORMATION File
    )

/*++

Routine Description:

    This routine opens a file.

Arguments:

    SystemContext - Supplies the context pointer passed to the load executable
        function.

    BinaryName - Supplies the name of the executable image to open.

    File - Supplies a pointer where the information for the file including its
        open handle will be returned.

Return Value:

    Status code.

--*/

{

    FILE_PROPERTIES FileProperties;
    BOOL FromKernelMode;
    ULONGLONG LocalFileSize;
    ULONG NameLength;
    PIO_HANDLE OutputHandle;
    PKPROCESS Process;
    KSTATUS Status;

    OutputHandle = INVALID_HANDLE;
    Process = (PKPROCESS)SystemContext;
    NameLength = RtlStringLength(BinaryName) + 1;

    //
    // If this is for the kernel process, then a driver is being loaded.
    // Always use the path directly as the kernel processes current working
    // directory should aways be the drivers directory on the system partition.
    //

    if (Process == PsGetKernelProcess()) {
        if (Process != PsGetCurrentProcess()) {
            FromKernelMode = FALSE;

        } else {
            FromKernelMode = TRUE;
        }

        Status = IoOpen(FromKernelMode,
                        NULL,
                        BinaryName,
                        NameLength,
                        IO_ACCESS_READ | IO_ACCESS_EXECUTE,
                        0,
                        FILE_PERMISSION_NONE,
                        &OutputHandle);

        goto ImOpenFileEnd;

    } else {

        //
        // If this is the first image being opened in a user mode app, then
        // it's always the OS base library.
        //

        if (Process->ImageCount == 0) {

            ASSERT(RtlAreStringsEqual(BinaryName,
                                      OS_BASE_LIBRARY,
                                      NameLength) != FALSE);

            IoIoHandleAddReference(PsOsBaseLibrary);
            OutputHandle = PsOsBaseLibrary;
            Status = STATUS_SUCCESS;

        } else {
            Status = IoOpen(FALSE,
                            NULL,
                            BinaryName,
                            NameLength,
                            IO_ACCESS_READ | IO_ACCESS_EXECUTE,
                            0,
                            FILE_PERMISSION_NONE,
                            &OutputHandle);

            if (KSUCCESS(Status)) {
                goto ImOpenFileEnd;
            }
        }
    }

ImOpenFileEnd:
    if (KSUCCESS(Status)) {
        Status = IoGetFileInformation(OutputHandle, &FileProperties);
        if (KSUCCESS(Status)) {
            READ_INT64_SYNC(&(FileProperties.FileSize), &LocalFileSize);
            File->Size = LocalFileSize;
            File->ModificationDate = FileProperties.ModifiedTime.Seconds;
        }

    } else {
        OutputHandle = INVALID_HANDLE;
    }

    File->Handle = OutputHandle;
    return Status;
}

VOID
PspImCloseFile (
    PIMAGE_FILE_INFORMATION File
    )

/*++

Routine Description:

    This routine closes an open file, invalidating any memory mappings to it.

Arguments:

    File - Supplies a pointer to the file information.

Return Value:

    None.

--*/

{

    if (File->Handle == INVALID_HANDLE) {
        return;
    }

    IoClose(File->Handle);
    return;
}

KSTATUS
PspImLoadFile (
    PIMAGE_FILE_INFORMATION File,
    PIMAGE_BUFFER Buffer
    )

/*++

Routine Description:

    This routine loads an entire file into memory so the image library can
    access it.

Arguments:

    File - Supplies a pointer to the file information.

    Buffer - Supplies a pointer where the buffer will be returned on success.

Return Value:

    Status code.

--*/

{

    ULONGLONG AlignedSize;
    UINTN PageSize;
    KSTATUS Status;

    PageSize = MmPageSize();
    AlignedSize = ALIGN_RANGE_UP(File->Size, PageSize);
    if (AlignedSize > MAX_UINTN) {
        return STATUS_NOT_SUPPORTED;;
    }

    Status = MmMapFileSection(File->Handle,
                              0,
                              (UINTN)AlignedSize,
                              IMAGE_SECTION_READABLE | IMAGE_SECTION_WRITABLE,
                              TRUE,
                              NULL,
                              AllocationStrategyAnyAddress,
                              &(Buffer->Data));

    if (!KSUCCESS(Status)) {
        return Status;
    }

    Buffer->Size = File->Size;
    return STATUS_SUCCESS;
}

KSTATUS
PspImReadFile (
    PIMAGE_FILE_INFORMATION File,
    ULONGLONG Offset,
    UINTN Size,
    PIMAGE_BUFFER Buffer
    )

/*++

Routine Description:

    This routine reads a portion of the given file into a buffer, allocated by
    this function.

Arguments:

    File - Supplies a pointer to the file information.

    Offset - Supplies the file offset to read from in bytes.

    Size - Supplies the size to read, in bytes.

    Buffer - Supplies a pointer where the buffer will be returned on success.

Return Value:

    Status code.

--*/

{

    UINTN AlignedSize;
    UINTN BytesComplete;
    PIO_BUFFER IoBuffer;
    UINTN PageSize;
    KSTATUS Status;

    PageSize = MmPageSize();
    AlignedSize = ALIGN_RANGE_UP(Size, PageSize);
    IoBuffer = MmAllocateUninitializedIoBuffer(AlignedSize, 0);
    if (IoBuffer == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto ImReadFileEnd;
    }

    Status = IoReadAtOffset(File->Handle,
                            IoBuffer,
                            Offset,
                            AlignedSize,
                            0,
                            WAIT_TIME_INDEFINITE,
                            &BytesComplete,
                            NULL);

    if (Status == STATUS_END_OF_FILE) {
        Status = STATUS_SUCCESS;

    } else if (!KSUCCESS(Status)) {
        goto ImReadFileEnd;
    }

    Status = MmMapIoBuffer(IoBuffer, FALSE, FALSE, TRUE);
    if (!KSUCCESS(Status)) {
        goto ImReadFileEnd;
    }

    Buffer->Context = IoBuffer;
    Buffer->Data = IoBuffer->Fragment[0].VirtualAddress;
    Buffer->Size = BytesComplete;
    Status = STATUS_SUCCESS;

ImReadFileEnd:
    if (!KSUCCESS(Status)) {
        if (IoBuffer != NULL) {
            MmFreeIoBuffer(IoBuffer);
            IoBuffer = NULL;
        }
    }

    return Status;
}

VOID
PspImUnloadBuffer (
    PIMAGE_FILE_INFORMATION File,
    PIMAGE_BUFFER Buffer
    )

/*++

Routine Description:

    This routine unloads a file buffer created from either the load file or
    read file function, and frees the buffer.

Arguments:

    File - Supplies a pointer to the file information.

    Buffer - Supplies the buffer returned by the load file function.

Return Value:

    None.

--*/

{

    ULONGLONG AlignedSize;
    UINTN PageSize;
    KSTATUS Status;

    ASSERT(Buffer->Data != NULL);

    if (Buffer->Context != NULL) {
        MmFreeIoBuffer(Buffer->Context);

    } else {
        PageSize = MmPageSize();
        AlignedSize = ALIGN_RANGE_UP(File->Size, PageSize);
        Status = MmUnmapFileSection(NULL, Buffer->Data, AlignedSize, NULL);

        ASSERT(KSUCCESS(Status));
    }

    Buffer->Data = NULL;
    Buffer->Context = NULL;
    return;
}

KSTATUS
PspImAllocateAddressSpace (
    PVOID SystemContext,
    PIMAGE_FILE_INFORMATION File,
    ULONG Size,
    HANDLE *Handle,
    PVOID *Address,
    PVOID *AccessibleAddress
    )

/*++

Routine Description:

    This routine allocates a section of virtual address space that an image
    can be mapped in to.

Arguments:

    SystemContext - Supplies the context pointer passed to the load executable
        function.

    File - Supplies a pointer to the image file information.

    Size - Supplies the required size of the allocation, in bytes.

    Handle - Supplies a pointer where the handle representing this allocation
        will be returned on success.

    Address - Supplies a pointer that on input contains the preferred virtual
        address of the image load. On output, contains the allocated virtual
        address range. This is the VA allocated, but it may not actually be
        accessible at this time.

    AccessibleAddress - Supplies a pointer where a pointer will be returned
        that the caller can reach through to access the in-memory image. In
        online image loads this is probably the same as the returned address,
        though this cannot be assumed.

Return Value:

    Status code.

--*/

{

    PVOID AlignedPreferredAddress;
    BOOL KernelMode;
    UINTN PageOffset;
    UINTN PageSize;
    PKPROCESS Process;
    PMEMORY_RESERVATION Reservation;
    KSTATUS Status;

    Process = (PKPROCESS)SystemContext;
    KernelMode = FALSE;
    if (Process == PsGetKernelProcess()) {
        KernelMode = TRUE;
    }

    //
    // Align the preferred address down to a page.
    //

    PageSize = MmPageSize();
    AlignedPreferredAddress = (PVOID)(UINTN)ALIGN_RANGE_DOWN((UINTN)*Address,
                                                             PageSize);

    PageOffset = (UINTN)*Address - (UINTN)AlignedPreferredAddress;
    Reservation = MmCreateMemoryReservation(AlignedPreferredAddress,
                                            Size + PageOffset,
                                            KernelMode);

    if (Reservation == NULL) {
        Status = STATUS_UNSUCCESSFUL;
        goto AllocateAddressSpaceEnd;
    }

    //
    // Upon success, return the virtual address, accessible address, and return
    // the reservation as the handle. Since images are set up the process they
    // run in, the accessible VA is the same as the final VA.
    //

    *Address = Reservation->VirtualBase + PageOffset;
    *AccessibleAddress = *Address;
    *Handle = (HANDLE)Reservation;
    Status = STATUS_SUCCESS;

AllocateAddressSpaceEnd:
    if (!KSUCCESS(Status)) {
        if (Reservation != NULL) {
            MmFreeMemoryReservation(Reservation);
        }
    }

    return Status;
}

VOID
PspImFreeAddressSpace (
    HANDLE Handle,
    PVOID Address,
    UINTN Size
    )

/*++

Routine Description:

    This routine frees a section of virtual address space that was previously
    allocated.

Arguments:

    Handle - Supplies the handle returned during the allocate call.

    Address - Supplies the virtual address originally returned by the allocate
        routine.

    Size - Supplies the size in bytes of the originally allocated region.

Return Value:

    None.

--*/

{

    PMEMORY_RESERVATION Reservation;

    Reservation = (PMEMORY_RESERVATION)Handle;
    if ((Reservation != NULL) && (Reservation != INVALID_HANDLE)) {
        MmFreeMemoryReservation(Reservation);
    }

    return;
}

KSTATUS
PspImMapImageSegment (
    HANDLE AddressSpaceHandle,
    PVOID AddressSpaceAllocation,
    PIMAGE_FILE_INFORMATION File,
    ULONGLONG FileOffset,
    PIMAGE_SEGMENT Segment,
    PIMAGE_SEGMENT PreviousSegment
    )

/*++

Routine Description:

    This routine maps a section of the image to the given virtual address.

Arguments:

    AddressSpaceHandle - Supplies the handle used to claim the overall region
        of address space.

    AddressSpaceAllocation - Supplies the original lowest virtual address for
        this image.

    File - Supplies an optional pointer to the file being mapped. If this
        parameter is NULL, then a zeroed memory section is being mapped.

    FileOffset - Supplies the offset from the beginning of the file to the
        beginning of the mapping, in bytes.

    Segment - Supplies a pointer to the segment information to map. On output,
        the virtual address will contain the actual mapped address, and the
        mapping handle may be set.

    PreviousSegment - Supplies an optional pointer to the previous segment
        that was mapped, so this routine can handle overlap appropriately. This
        routine can assume that segments are always mapped in increasing order.

Return Value:

    Status code.

--*/

{

    PVOID Address;
    UINTN BytesCompleted;
    PIO_HANDLE FileHandle;
    PVOID FileRegion;
    UINTN FileRegionSize;
    UINTN FileSize;
    IO_BUFFER IoBuffer;
    ULONG IoBufferFlags;
    UINTN IoSize;
    BOOL KernelMode;
    PKPROCESS KernelProcess;
    ULONG MapFlags;
    UINTN MemoryRegionSize;
    UINTN MemorySize;
    UINTN NextPage;
    UINTN PageMask;
    UINTN PageOffset;
    UINTN PageSize;
    UINTN PreviousEnd;
    UINTN RegionEnd;
    UINTN RegionSize;
    PMEMORY_RESERVATION Reservation;
    UINTN SegmentAddress;
    KSTATUS Status;

    ASSERT((PreviousSegment == NULL) ||
           (Segment->VirtualAddress > PreviousSegment->VirtualAddress));

    FileRegion = NULL;
    FileRegionSize = 0;
    FileHandle = NULL;
    IoBufferFlags = 0;
    if (File != NULL) {
        FileHandle = File->Handle;
    }

    FileSize = Segment->FileSize;
    MemorySize = Segment->MemorySize;

    ASSERT((FileSize == Segment->FileSize) &&
           (MemorySize == Segment->MemorySize));

    Reservation = AddressSpaceHandle;
    KernelMode = FALSE;
    KernelProcess = PsGetKernelProcess();
    MapFlags = SYS_MAP_FLAG_READ;
    if ((Segment->Flags & IMAGE_MAP_FLAG_WRITE) != 0) {
        MapFlags |= SYS_MAP_FLAG_WRITE;
    }

    //
    // Map everything writable for now, it will get fixed up during
    // finalization.
    //

    MapFlags = IMAGE_SECTION_READABLE | IMAGE_SECTION_WRITABLE;
    if ((Segment->Flags & IMAGE_MAP_FLAG_EXECUTE) != 0) {
        MapFlags |= IMAGE_SECTION_EXECUTABLE;
    }

    if (Reservation->Process == KernelProcess) {
        KernelMode = TRUE;
        MapFlags |= IMAGE_SECTION_NON_PAGED;
        IoBufferFlags |= IO_BUFFER_FLAG_KERNEL_MODE_DATA;
    }

    //
    // Handle the first part, which may overlap with the previous segment.
    //

    PageSize = MmPageSize();
    PageMask = PageSize - 1;
    SegmentAddress = (UINTN)(Segment->VirtualAddress);
    if (PreviousSegment != NULL) {
        PreviousEnd = (UINTN)(PreviousSegment->VirtualAddress) +
                      PreviousSegment->MemorySize;

        RegionEnd = ALIGN_RANGE_UP(PreviousEnd, PageSize);
        if (RegionEnd > SegmentAddress) {

            //
            // Fail if this region is executable but the previous one was not,
            // as the kernel can't go make a portion of the previous section
            // executable. One potential workaround would be to make the entire
            // previous section executable. So far this is not needed.
            //

            if (((Segment->Flags & IMAGE_MAP_FLAG_EXECUTE) != 0) &&
                ((PreviousSegment->Flags & IMAGE_MAP_FLAG_EXECUTE) == 0)) {

                RtlDebugPrint("Error: Executable image section at 0x%x "
                              "overlaps with non-executable section at "
                              "0x%x.\n",
                              Segment->VirtualAddress,
                              PreviousSegment->VirtualAddress);

                Status = STATUS_MEMORY_CONFLICT;
                goto MapImageSegmentEnd;
            }

            //
            // Compute the portion of this section that needs to be read or
            // zeroed into it.
            //

            if (SegmentAddress + MemorySize < RegionEnd) {
                RegionEnd = SegmentAddress + MemorySize;
            }

            RegionSize = RegionEnd - SegmentAddress;
            IoSize = FileSize;
            if (IoSize > RegionSize) {
                IoSize = RegionSize;
            }

            Status = MmInitializeIoBuffer(&IoBuffer,
                                          (PVOID)SegmentAddress,
                                          INVALID_PHYSICAL_ADDRESS,
                                          IoSize,
                                          IoBufferFlags);

            if (!KSUCCESS(Status)) {
                goto MapImageSegmentEnd;
            }

            Status = IoReadAtOffset(FileHandle,
                                    &IoBuffer,
                                    FileOffset,
                                    IoSize,
                                    0,
                                    WAIT_TIME_INDEFINITE,
                                    &BytesCompleted,
                                    NULL);

            if (!KSUCCESS(Status)) {
                goto MapImageSegmentEnd;
            }

            if (BytesCompleted != IoSize) {
                Status = STATUS_END_OF_FILE;
                goto MapImageSegmentEnd;
            }

            if (IoSize < RegionSize) {
                RtlZeroMemory((PVOID)SegmentAddress + IoSize,
                              RegionSize - IoSize);
            }

            Status = MmSyncCacheRegion((PVOID)SegmentAddress, RegionSize);

            ASSERT(KSUCCESS(Status));

            FileOffset += IoSize;
            FileSize -= IoSize;
            MemorySize -= RegionSize;
            SegmentAddress = RegionEnd;
        }
    }

    //
    // This is the main portion. If the file offset and address have the same
    // page alignment, then it can be mapped directly. Otherwise, it must be
    // read in.
    //

    if (FileSize != 0) {
        PageOffset = FileOffset & PageMask;
        FileRegion = (PVOID)(SegmentAddress - PageOffset);
        FileRegionSize = ALIGN_RANGE_UP(FileSize + PageOffset, PageSize);

        //
        // Try to memory map the file directly.
        //

        if (PageOffset == (SegmentAddress & PageMask)) {
            Status = MmMapFileSection(FileHandle,
                                      FileOffset - PageOffset,
                                      FileRegionSize,
                                      MapFlags,
                                      KernelMode,
                                      Reservation,
                                      AllocationStrategyFixedAddress,
                                      &FileRegion);

            if (!KSUCCESS(Status)) {
                RtlDebugPrint("Failed to map %x bytes at %x: %x\n",
                              FileRegionSize,
                              FileRegion,
                              Status);

                FileRegionSize = 0;
                goto MapImageSegmentEnd;
            }

            IoSize = 0;

        //
        // The file offsets don't agree. Allocate a region for reading.
        //

        } else {
            Status = MmMapFileSection(INVALID_HANDLE,
                                      0,
                                      FileRegionSize,
                                      MapFlags,
                                      KernelMode,
                                      Reservation,
                                      AllocationStrategyFixedAddress,
                                      &FileRegion);

            if (!KSUCCESS(Status)) {
                RtlDebugPrint("Failed to map %x bytes at %x: %x\n",
                              FileRegionSize,
                              FileRegion,
                              Status);

                FileRegionSize = 0;
                goto MapImageSegmentEnd;
            }

            IoSize = FileSize;
        }

        Segment->MappingStart = FileRegion;

        ASSERT((UINTN)FileRegion == SegmentAddress - PageOffset);

        //
        // Read from the file if the file wasn't mapped directly.
        //

        if (IoSize != 0) {
            Status = MmInitializeIoBuffer(&IoBuffer,
                                          (PVOID)SegmentAddress,
                                          INVALID_PHYSICAL_ADDRESS,
                                          IoSize,
                                          IoBufferFlags);

            if (!KSUCCESS(Status)) {
                goto MapImageSegmentEnd;
            }

            Status = IoReadAtOffset(FileHandle,
                                    &IoBuffer,
                                    FileOffset,
                                    IoSize,
                                    0,
                                    WAIT_TIME_INDEFINITE,
                                    &BytesCompleted,
                                    NULL);

            if (!KSUCCESS(Status)) {
                goto MapImageSegmentEnd;
            }

            if (BytesCompleted != IoSize) {
                Status = STATUS_END_OF_FILE;
                goto MapImageSegmentEnd;
            }

            Status = MmSyncCacheRegion((PVOID)SegmentAddress, IoSize);

            ASSERT(KSUCCESS(Status));
        }

        SegmentAddress += FileSize;
        MemorySize -= FileSize;

        //
        // Zero out any region between the end of the file portion and the next
        // page.
        //

        NextPage = ALIGN_RANGE_UP(SegmentAddress, PageSize);
        if (NextPage - SegmentAddress != 0) {
            RtlZeroMemory((PVOID)SegmentAddress, NextPage - SegmentAddress);
            Status = MmSyncCacheRegion((PVOID)SegmentAddress,
                                       NextPage - SegmentAddress);

            ASSERT(KSUCCESS(Status));
        }

        if (NextPage >= SegmentAddress + MemorySize) {
            Status = STATUS_SUCCESS;
            goto MapImageSegmentEnd;
        }

        MemorySize -= NextPage - SegmentAddress;
        SegmentAddress = NextPage;
    }

    //
    // Memory map the remaining region, which is not backed by the image.
    //

    PageOffset = SegmentAddress & PageMask;
    Address = (PVOID)(SegmentAddress - PageOffset);
    MemoryRegionSize = MemorySize + PageOffset;
    MemoryRegionSize = ALIGN_RANGE_UP(MemoryRegionSize, PageSize);
    Status = MmMapFileSection(INVALID_HANDLE,
                              0,
                              MemoryRegionSize,
                              MapFlags,
                              KernelMode,
                              Reservation,
                              AllocationStrategyFixedAddress,
                              &Address);

    if (!KSUCCESS(Status)) {
        RtlDebugPrint("Failed to map %x bytes at %x: %x\n",
                      MemorySize + PageOffset,
                      Address,
                      Status);

        goto MapImageSegmentEnd;
    }

    //
    // If this is a kernel mode segment, then the anonymous non-paged section
    // just created will have been backed by fresh pages but not initialized to
    // zero.
    //

    if (KernelMode != FALSE) {
        RtlZeroMemory(Address, MemoryRegionSize);
    }

    if (Segment->MappingStart == NULL) {
        Segment->MappingStart = Address;
    }

MapImageSegmentEnd:
    if (!KSUCCESS(Status)) {
        if (FileRegionSize != 0) {
            MmUnmapFileSection(Reservation->Process,
                               FileRegion,
                               FileRegionSize,
                               Reservation);
        }
    }

    return Status;
}

VOID
PspImUnmapImageSegment (
    HANDLE AddressSpaceHandle,
    PIMAGE_SEGMENT Segment
    )

/*++

Routine Description:

    This routine maps unmaps an image segment.

Arguments:

    AddressSpaceHandle - Supplies the handle used to claim the overall region
        of address space.

    Segment - Supplies a pointer to the segment information to unmap.

Return Value:

    None.

--*/

{

    UINTN End;
    UINTN PageSize;
    PMEMORY_RESERVATION Reservation;
    UINTN SectionBegin;
    KSTATUS Status;

    PageSize = MmPageSize();
    Reservation = AddressSpaceHandle;
    if (AddressSpaceHandle == INVALID_HANDLE) {
        Reservation = NULL;
    }

    if (Segment->MappingStart == NULL) {
        return;
    }

    SectionBegin = (UINTN)(Segment->MappingStart);
    End = (UINTN)(Segment->VirtualAddress) + Segment->MemorySize;
    End = ALIGN_RANGE_UP((UINTN)End, PageSize);
    Status = MmUnmapFileSection(NULL,
                                (PVOID)SectionBegin,
                                End - SectionBegin,
                                Reservation);

    ASSERT(KSUCCESS(Status));

    return;
}

KSTATUS
PspImNotifyImageLoad (
    PLOADED_IMAGE Image
    )

/*++

Routine Description:

    This routine notifies the primary consumer of the image library that an
    image has been loaded.

Arguments:

    Image - Supplies the image that has just been loaded. This image should
        be subsequently returned to the image library upon requests for loaded
        images with the given name.

Return Value:

    Status code. Failing status codes veto the image load.

--*/

{

    PKPROCESS KernelProcess;
    PKPROCESS Process;
    PMEMORY_RESERVATION Reservation;
    KSTATUS Status;

    if (Image->AllocatorHandle == INVALID_HANDLE) {
        Process = PsGetCurrentProcess();

    } else {
        Reservation = (PMEMORY_RESERVATION)(Image->AllocatorHandle);
        Process = Reservation->Process;
    }

    KernelProcess = PsGetKernelProcess();

    ASSERT((KeIsQueuedLockHeld(Process->QueuedLock) != FALSE) ||
           (Process == KernelProcess));

    Process->ImageCount += 1;
    Process->ImageListSignature += Image->File.ModificationDate +
                                   (UINTN)(Image->LoadedLowestAddress);

    //
    // If the debug flag is enabled, then make the kernel debugger aware of
    // this user mode module.
    //

    if ((PsKdLoadAllImages != FALSE) ||
        (Image->SystemContext == KernelProcess)) {

        PspLoadProcessImageIntoKernelDebugger(Process, Image);
    }

    //
    // Let I/O do some initialization if this is a driver.
    //

    if (Image->SystemContext == KernelProcess) {
        if (Image->TlsSize != 0) {
            Status = STATUS_NOT_SUPPORTED;
            goto ImNotifyImageLoadEnd;
        }

        Status = IoCreateDriverStructure(Image);
        if (!KSUCCESS(Status)) {
            goto ImNotifyImageLoadEnd;
        }
    }

    Status = STATUS_SUCCESS;

ImNotifyImageLoadEnd:
    return Status;
}

VOID
PspImNotifyImageUnload (
    PLOADED_IMAGE Image
    )

/*++

Routine Description:

    This routine notifies the primary consumer of the image library that an
    image is about to be unloaded from memory. Once this routine returns, the
    image should not be referenced again as it will be freed.

Arguments:

    Image - Supplies the image that is about to be unloaded.

Return Value:

    None.

--*/

{

    PKPROCESS KernelProcess;
    PKPROCESS Process;

    KernelProcess = PsGetKernelProcess();
    Process = Image->SystemContext;
    if (Process == NULL) {
        Process = PsGetCurrentProcess();
    }

    ASSERT((KeIsQueuedLockHeld(Process->QueuedLock) != FALSE) ||
           (Process == KernelProcess));

    ASSERT(Process->ImageCount != 0);

    Process->ImageCount -= 1;
    Process->ImageListSignature -= Image->File.ModificationDate +
                                   (UINTN)(Image->LoadedLowestAddress);

    if (Image->DebuggerModule != NULL) {
        KdReportModuleChange(Image->DebuggerModule, FALSE);
        MmFreeNonPagedPool(Image->DebuggerModule);
        Image->DebuggerModule = NULL;
    }

    //
    // Let I/O destroy its structures if this is a driver.
    //

    if (Image->SystemContext == KernelProcess) {
        IoDestroyDriverStructure(Image);
    }

    return;
}

VOID
PspImInvalidateInstructionCacheRegion (
    PVOID Address,
    ULONG Size
    )

/*++

Routine Description:

    This routine invalidates an instruction cache region after code has been
    modified.

Arguments:

    Address - Supplies the virtual address of the revion to invalidate.

    Size - Supplies the number of bytes to invalidate.

Return Value:

    None.

--*/

{

    KSTATUS Status;

    Status = MmSyncCacheRegion(Address, Size);

    ASSERT(KSUCCESS(Status));

    return;
}

PSTR
PspImGetEnvironmentVariable (
    PSTR Variable
    )

/*++

Routine Description:

    This routine gets an environment variable value for the image library.

Arguments:

    Variable - Supplies a pointer to a null terminated string containing the
        name of the variable to get.

Return Value:

    Returns a pointer to the value of the environment variable. The image
    library will not free or modify this value.

    NULL if the given environment variable is not set.

--*/

{

    BOOL Match;
    PKPROCESS Process;
    UINTN VariableLength;

    //
    // User mode gets no help.
    //

    Process = PsGetCurrentProcess();
    if (Process != PsGetKernelProcess()) {
        return NULL;
    }

    VariableLength = RtlStringLength(Variable);
    Match = RtlAreStringsEqual(Variable,
                               IMAGE_DYNAMIC_LIBRARY_PATH_VARIABLE,
                               VariableLength + 1);

    if (Match != FALSE) {

        //
        // Return a separator, which will append an empty prefix.
        //

        return ":";
    }

    return NULL;
}

KSTATUS
PspImFinalizeSegments (
    HANDLE AddressSpaceHandle,
    PIMAGE_SEGMENT Segments,
    UINTN SegmentCount
    )

/*++

Routine Description:

    This routine applies the final memory protection attributes to the given
    segments. Read and execute bits can be applied at the time of mapping, but
    write protection may be applied here.

Arguments:

    AddressSpaceHandle - Supplies the handle used to claim the overall region
        of address space.

    Segments - Supplies the final array of segments.

    SegmentCount - Supplies the number of segments.

Return Value:

    Status code.

--*/

{

    UINTN End;
    ULONG MapFlags;
    UINTN PageSize;
    PIMAGE_SEGMENT Segment;
    UINTN SegmentIndex;
    UINTN Size;
    KSTATUS Status;

    PageSize = MmPageSize();
    for (SegmentIndex = 0; SegmentIndex < SegmentCount; SegmentIndex += 1) {
        Segment = &(Segments[SegmentIndex]);
        if (Segment->Type == ImageSegmentInvalid) {
            continue;
        }

        //
        // If the segment has no protection features, then there's nothing to
        // tighten up.
        //

        if ((Segment->Flags & IMAGE_MAP_FLAG_WRITE) != 0) {
            continue;
        }

        //
        // If the image was so small it fit entirely in some other segment's
        // remainder, skip it.
        //

        if (Segment->MappingStart == NULL) {
            continue;
        }

        //
        // Compute the region whose protection should actually be changed.
        //

        End = (UINTN)(Segment->VirtualAddress) + Segment->MemorySize;
        End = ALIGN_RANGE_UP(End, PageSize);

        //
        // If the region has a real size, changed its protection to read-only.
        //

        if ((PVOID)End > Segment->MappingStart) {
            Size = End - (UINTN)(Segment->MappingStart);
            MapFlags = IMAGE_SECTION_READABLE;
            if ((Segment->Flags & IMAGE_MAP_FLAG_EXECUTE) != 0) {
                MapFlags |= IMAGE_SECTION_EXECUTABLE;
            }

            Status = MmChangeImageSectionRegionAccess(Segment->MappingStart,
                                                      Size,
                                                      MapFlags);

            if (!KSUCCESS(Status)) {
                goto FinalizeSegmentsEnd;
            }
        }
    }

    Status = STATUS_SUCCESS;

FinalizeSegmentsEnd:
    return Status;
}

KSTATUS
PspImCloneImage (
    PKPROCESS Source,
    PKPROCESS Destination,
    PLOADED_IMAGE SourceImage,
    PLOADED_IMAGE *NewDestinationImage
    )

/*++

Routine Description:

    This routine makes a copy of the given process' image. This routine creates
    the imports array but every entry is null, and needs to be filled in later.

Arguments:

    Source - Supplies a pointer to the source process.

    Destination - Supplies a pointer to the destination process.

    SourceImage - Supplies a pointer to the image to copy.

    NewDestinationImage - Supplies a pointer where a pointer to the newly
        created destination image will be returned.

Return Value:

    Status code.

--*/

{

    ULONG AllocationSize;
    ULONG NameSize;
    PLOADED_IMAGE NewImage;
    KSTATUS Status;

    //
    // Allocate a new image.
    //

    NameSize = RtlStringLength(SourceImage->BinaryName) + 1;
    AllocationSize = sizeof(LOADED_IMAGE) + NameSize;
    NewImage = PspImAllocateMemory(AllocationSize, PS_ALLOCATION_TAG);
    if (NewImage == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto ImCloneImageEnd;
    }

    //
    // Initialize the new image.
    //

    ASSERT(SourceImage->BinaryName == (PSTR)(SourceImage + 1));

    RtlCopyMemory(NewImage, SourceImage, AllocationSize);
    NewImage->BinaryName = (PSTR)(NewImage + 1);
    NewImage->ListEntry.Next = NULL;
    NewImage->ListEntry.Previous = NULL;
    if (NewImage->File.Handle != INVALID_HANDLE) {
        IoIoHandleAddReference(NewImage->File.Handle);
    }

    NewImage->SystemContext = Destination;
    NewImage->AllocatorHandle = INVALID_HANDLE;
    NewImage->Segments = NULL;
    NewImage->Imports = NULL;
    NewImage->DebuggerModule = NULL;
    NewImage->StaticFunctions = NULL;
    NewImage->ImageContext = NULL;

    //
    // Create the image segments.
    //

    if (NewImage->SegmentCount != 0) {
        AllocationSize = sizeof(IMAGE_SEGMENT) * NewImage->SegmentCount;
        NewImage->Segments = PspImAllocateMemory(AllocationSize,
                                                 PS_ALLOCATION_TAG);

        if (NewImage->Segments == NULL) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto ImCloneImageEnd;
        }

        RtlCopyMemory(NewImage->Segments,
                      SourceImage->Segments,
                      AllocationSize);
    }

    //
    // Allocate space for the imports array. Unfortunately it cannot be
    // populated yet because it may point to images that have not yet been
    // cloned.
    //

    if (SourceImage->ImportCount != 0) {
        AllocationSize = SourceImage->ImportCount * sizeof(PLOADED_IMAGE);
        NewImage->Imports = PspImAllocateMemory(AllocationSize,
                                                PS_ALLOCATION_TAG);

        if (NewImage->Imports == NULL) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto ImCloneImageEnd;
        }

        RtlZeroMemory(NewImage->Imports, AllocationSize);
    }

    INSERT_BEFORE(&(NewImage->ListEntry), &(Destination->ImageListHead));
    Destination->ImageCount += 1;
    if (PsKdLoadAllImages != FALSE) {
        PspLoadProcessImageIntoKernelDebugger(Destination, NewImage);
    }

    Destination->ImageListSignature += NewImage->File.ModificationDate +
                                       (UINTN)(NewImage->LoadedLowestAddress);

    Status = STATUS_SUCCESS;

ImCloneImageEnd:
    if (!KSUCCESS(Status)) {
        if (NewImage != NULL) {
            if (NewImage->Imports != NULL) {
                PspImFreeMemory(NewImage->Imports);
            }

            if (NewImage->Segments != NULL) {
                PspImFreeMemory(NewImage->Segments);
            }

            PspImFreeMemory(NewImage);
            NewImage = NULL;
        }
    }

    *NewDestinationImage = NewImage;
    return Status;
}

PLOADED_IMAGE
PspImGetAssociatedImage (
    PLOADED_IMAGE QueryImage,
    PIMAGE_ASSOCIATION AssociationMapping,
    ULONG AssociationCount
    )

/*++

Routine Description:

    This routine searches through the given association mapping looking for
    an image that maps to the query.

Arguments:

    QueryImage - Supplies a pointer to the image whose associated image is
        requested.

    AssociationMapping - Supplies a pointer to the association mapping array.

    AssociationCount - Supplies the number of elements in the association array.

Return Value:

    Returns a pointer to the image associated with the query image on success.

    NULL if no mapping could be found.

--*/

{

    ULONG AssociationIndex;

    for (AssociationIndex = 0;
         AssociationIndex < AssociationCount;
         AssociationIndex += 1) {

        if (AssociationMapping[AssociationIndex].SourceImage == QueryImage) {
            return AssociationMapping[AssociationIndex].DestinationImage;
        }

        if (AssociationMapping[AssociationIndex].DestinationImage ==
                                                                  QueryImage) {

            return AssociationMapping[AssociationIndex].SourceImage;
        }
    }

    return NULL;
}

KSTATUS
PspLoadProcessImageIntoKernelDebugger (
    PKPROCESS Process,
    PLOADED_IMAGE Image
    )

/*++

Routine Description:

    This routine loads the given image into the kernel debugger. This routine
    assumes the process image list lock is already held.

Arguments:

    Process - Supplies a pointer to the process.

    Image - Supplies a pointer to the image to load into the kernel debugger.

Return Value:

    Status code.

--*/

{

    ULONG AllocationSize;
    UINTN BaseDifference;
    PLOADED_MODULE DebuggerModule;
    ULONG NameSize;

    ASSERT(KeGetRunLevel() == RunLevelLow);

    //
    // If the image is already loaded, skip it.
    //

    if (Image->DebuggerModule != NULL) {
        return STATUS_SUCCESS;
    }

    //
    // If for some odd reason the image doesn't have a name, skip it.
    //

    if (Image->BinaryName == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    //
    // Allocate and initialize the debugger module structure.
    //

    NameSize = ((RtlStringLength(Image->BinaryName) + 1) * sizeof(CHAR));
    AllocationSize = sizeof(LOADED_MODULE) + NameSize -
                     (sizeof(CHAR) * ANYSIZE_ARRAY);

    DebuggerModule = MmAllocateNonPagedPool(AllocationSize,
                                            PS_DEBUG_ALLOCATION_TAG);

    if (DebuggerModule == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(DebuggerModule, AllocationSize);
    DebuggerModule->StructureSize = AllocationSize;
    DebuggerModule->Timestamp = Image->File.ModificationDate;
    BaseDifference = (UINTN)Image->LoadedLowestAddress -
                     (UINTN)Image->PreferredLowestAddress;

    DebuggerModule->BaseAddress = Image->DeclaredBase + BaseDifference;
    DebuggerModule->LowestAddress = Image->LoadedLowestAddress;
    DebuggerModule->EntryPoint = Image->EntryPoint;
    DebuggerModule->Size = Image->Size;
    DebuggerModule->Process = Process->Identifiers.ProcessId;
    RtlStringCopy(DebuggerModule->BinaryName, Image->BinaryName, NameSize);

    //
    // Save the pointer and make the debugger aware of this new module.
    //

    Image->DebuggerModule = DebuggerModule;
    KdReportModuleChange(DebuggerModule, TRUE);
    return STATUS_SUCCESS;
}
