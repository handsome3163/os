/*++

Copyright (c) 2012 Minoca Corp. All Rights Reserved

Module Name:

    osimag.c

Abstract:

    This module implements the underlying support routines for the image
    library to be run in user mode.

Author:

    Evan Green 17-Oct-2013

Environment:

    User

--*/

//
// ------------------------------------------------------------------- Includes
//

#include "osbasep.h"

//
// ---------------------------------------------------------------- Definitions
//

#define OS_IMAGE_ALLOCATION_TAG 0x6D49734F // 'mIsO'

#define OS_IMAGE_LIST_SIZE_GUESS 512
#define OS_IMAGE_LIST_TRY_COUNT 10

#define OS_DYNAMIC_LOADER_USAGE                                                \
    "usage: libminocaos.so [options] [program [arguments]]\n"                  \
    "This can be run either indirectly as an interpreter, or it can load and " \
    "execute a command line directly.\n"

//
// ------------------------------------------------------ Data Type Definitions
//

typedef
VOID
(*PIMAGE_ENTRY_POINT) (
    PPROCESS_ENVIRONMENT Environment
    );

/*++

Routine Description:

    This routine implements the entry point for a loaded image.

Arguments:

    Environment - Supplies the process environment.

Return Value:

    None, the image does not return.

--*/

//
// ----------------------------------------------- Internal Function Prototypes
//

PVOID
OspImAllocateMemory (
    ULONG Size,
    ULONG Tag
    );

VOID
OspImFreeMemory (
    PVOID Allocation
    );

KSTATUS
OspImOpenFile (
    PVOID SystemContext,
    PSTR BinaryName,
    PIMAGE_FILE_INFORMATION File
    );

VOID
OspImCloseFile (
    PIMAGE_FILE_INFORMATION File
    );

KSTATUS
OspImLoadFile (
    PIMAGE_FILE_INFORMATION File,
    PIMAGE_BUFFER Buffer
    );

VOID
OspImUnloadBuffer (
    PIMAGE_FILE_INFORMATION File,
    PIMAGE_BUFFER Buffer
    );

KSTATUS
OspImAllocateAddressSpace (
    PVOID SystemContext,
    PIMAGE_FILE_INFORMATION File,
    ULONG Size,
    HANDLE *Handle,
    PVOID *Address,
    PVOID *AccessibleAddress
    );

VOID
OspImFreeAddressSpace (
    HANDLE Handle,
    PVOID Address,
    UINTN Size
    );

KSTATUS
OspImMapImageSegment (
    HANDLE AddressSpaceHandle,
    PVOID AddressSpaceAllocation,
    PIMAGE_FILE_INFORMATION File,
    ULONGLONG FileOffset,
    PIMAGE_SEGMENT Segment,
    PIMAGE_SEGMENT PreviousSegment
    );

VOID
OspImUnmapImageSegment (
    HANDLE AddressSpaceHandle,
    PIMAGE_SEGMENT Segment
    );

KSTATUS
OspImNotifyImageLoad (
    PLOADED_IMAGE Image
    );

VOID
OspImNotifyImageUnload (
    PLOADED_IMAGE Image
    );

VOID
OspImInvalidateInstructionCacheRegion (
    PVOID Address,
    ULONG Size
    );

PSTR
OspImGetEnvironmentVariable (
    PSTR Variable
    );

KSTATUS
OspImFinalizeSegments (
    HANDLE AddressSpaceHandle,
    PIMAGE_SEGMENT Segments,
    UINTN SegmentCount
    );

VOID
OspImInitializeImages (
    PLIST_ENTRY ListHead
    );

VOID
OspImInitializeImage (
    PLOADED_IMAGE Image
    );

KSTATUS
OspLoadInitialImageList (
    BOOL Relocate
    );

KSTATUS
OspImAssignModuleNumber (
    PLOADED_IMAGE Image
    );

VOID
OspImReleaseModuleNumber (
    PLOADED_IMAGE Image
    );

//
// -------------------------------------------------------------------- Globals
//

//
// Store a pointer to the routine used to get environment variable contents.
//

OS_API PIM_GET_ENVIRONMENT_VARIABLE OsImGetEnvironmentVariable;

//
// Store the page shift and mask for easy use during image section mappings.
//

UINTN OsPageShift;
UINTN OsPageSize;

//
// Store a pointer to the list head of all loaded images.
//

LIST_ENTRY OsLoadedImagesHead;
OS_LOCK OsLoadedImagesLock;

//
// Define the image library function table.
//

IM_IMPORT_TABLE OsImageFunctionTable = {
    OspImAllocateMemory,
    OspImFreeMemory,
    OspImOpenFile,
    OspImCloseFile,
    OspImLoadFile,
    NULL,
    OspImUnloadBuffer,
    OspImAllocateAddressSpace,
    OspImFreeAddressSpace,
    OspImMapImageSegment,
    OspImUnmapImageSegment,
    OspImNotifyImageLoad,
    OspImNotifyImageUnload,
    OspImInvalidateInstructionCacheRegion,
    OspImGetEnvironmentVariable,
    OspImFinalizeSegments
};

//
// Store the overridden library path specified by the command arguments to the
// dynamic linker.
//

PSTR OsImLibraryPathOverride;

//
// Store the bitmap for the image module numbers. Index zero is never valid.
//

UINTN OsImStaticModuleNumberBitmap = 0x1;
PUINTN OsImModuleNumberBitmap = &OsImStaticModuleNumberBitmap;
UINTN OsImModuleNumberBitmapSize = 1;

//
// Store the module generation number, which increments whenever a module is
// loaded or unloaded. It is protected under the image list lock.
//

UINTN OsImModuleGeneration;

//
// Store a boolean indicating whether or not the initial image is loaded.
//

BOOL OsImExecutableLoaded = TRUE;

//
// ------------------------------------------------------------------ Functions
//

OS_API
VOID
OsDynamicLoaderMain (
    PPROCESS_ENVIRONMENT Environment
    )

/*++

Routine Description:

    This routine implements the main routine for the Minoca OS loader when
    invoked directly (either as a standalone application or an interpreter).

Arguments:

    Environment - Supplies the process environment.

Return Value:

    None. This routine exits directly and never returns.

--*/

{

    PSTR Argument;
    UINTN ArgumentIndex;
    PLIST_ENTRY CurrentEntry;
    PLOADED_IMAGE CurrentImage;
    PLOADED_IMAGE Image;
    ULONG LoadFlags;
    PIMAGE_ENTRY_POINT Start;
    INT Status;
    PVOID ThreadData;

    OsInitializeLibrary(Environment);
    OsImExecutableLoaded = FALSE;
    Status = OspLoadInitialImageList(TRUE);
    if (!KSUCCESS(Status)) {
        RtlDebugPrint("Failed to populate initial image list: %x.\n", Status);
        goto DynamicLoaderMainEnd;
    }

    //
    // If the executable is this library, then the dynamic loader is being
    // invoked directly.
    //

    if (Environment->StartData->ExecutableBase ==
        Environment->StartData->OsLibraryBase) {

        LoadFlags = IMAGE_LOAD_FLAG_IGNORE_INTERPRETER |
                    IMAGE_LOAD_FLAG_PRIMARY_EXECUTABLE |
                    IMAGE_LOAD_FLAG_NO_RELOCATIONS;

        ArgumentIndex = 1;
        while (ArgumentIndex < Environment->ArgumentCount) {
            Argument = Environment->Arguments[ArgumentIndex];
            if (RtlAreStringsEqual(Argument, "--library-path", -1) != FALSE) {
                ArgumentIndex += 1;
                if (ArgumentIndex == Environment->ArgumentCount) {
                    RtlDebugPrint("--library-path Argument missing.\n");
                    Status = STATUS_INVALID_PARAMETER;
                    goto DynamicLoaderMainEnd;
                }

                OsImLibraryPathOverride = Environment->Arguments[ArgumentIndex];
                ArgumentIndex += 1;

            } else {
                break;
            }
        }

        if (ArgumentIndex >= Environment->ArgumentCount) {
            RtlDebugPrint(OS_DYNAMIC_LOADER_USAGE);
            Status = 1;
            goto DynamicLoaderMainEnd;
        }

        //
        // Munge the environment to make it look like the program was
        // invoked directly.
        //

        Environment->Arguments = &(Environment->Arguments[ArgumentIndex]);
        Environment->ArgumentCount -= ArgumentIndex;
        Environment->ImageName = Environment->Arguments[0];
        Environment->ImageNameLength =
                                RtlStringLength(Environment->Arguments[0]) + 1;

        Status = ImLoadExecutable(&OsLoadedImagesHead,
                                  Environment->ImageName,
                                  NULL,
                                  NULL,
                                  NULL,
                                  LoadFlags,
                                  0,
                                  &Image,
                                  NULL);
    }

    if (!KSUCCESS(Status)) {
        RtlDebugPrint("Failed to load %s: %x\n",
                      Environment->ImageName,
                      Status);

        goto DynamicLoaderMainEnd;
    }

    //
    // Assign module numbers to any modules that do not have them yet. This is
    // done after the executable is loaded so the executable gets the first
    // slot.
    //

    CurrentEntry = OsLoadedImagesHead.Next;
    while (CurrentEntry != &OsLoadedImagesHead) {
        CurrentImage = LIST_VALUE(CurrentEntry, LOADED_IMAGE, ListEntry);
        CurrentEntry = CurrentEntry->Next;
        if (CurrentImage->ModuleNumber == 0) {
            OspImAssignModuleNumber(CurrentImage);
        }

        if ((Image == NULL) &&
            ((CurrentImage->LoadFlags &
              IMAGE_LOAD_FLAG_PRIMARY_EXECUTABLE) != 0)) {

            Image = CurrentImage;
        }
    }

    OsImExecutableLoaded = TRUE;

    //
    // Initialize TLS support.
    //

    OspTlsAllocate(&OsLoadedImagesHead, &ThreadData);
    OsSetThreadPointer(ThreadData);

    //
    // Now that TLS offsets are settled, relocate the images.
    //

    Status = ImRelocateImages(&OsLoadedImagesHead);
    if (!KSUCCESS(Status)) {
        RtlDebugPrint("Failed to relocate: %x\n", Status);
        goto DynamicLoaderMainEnd;
    }

    //
    // Call static constructors, without acquiring and releasing the lock
    // constantly.
    //

    CurrentEntry = OsLoadedImagesHead.Previous;
    while (CurrentEntry != &OsLoadedImagesHead) {
        CurrentImage = LIST_VALUE(CurrentEntry, LOADED_IMAGE, ListEntry);

        ASSERT((CurrentImage->Flags & IMAGE_FLAG_INITIALIZED) == 0);

        OspImInitializeImage(CurrentImage);
        CurrentImage->Flags |= IMAGE_FLAG_INITIALIZED;
        CurrentEntry = CurrentEntry->Previous;
    }

    //
    // Jump off to the image entry point.
    //

    Start = Image->EntryPoint;
    Start(Environment);
    RtlDebugPrint("Warning: Image returned to interpreter!\n");
    Status = 1;

DynamicLoaderMainEnd:
    OsExitProcess(Status);
    return;
}

OS_API
KSTATUS
OsLoadLibrary (
    PSTR LibraryName,
    ULONG Flags,
    PHANDLE Handle
    )

/*++

Routine Description:

    This routine loads a dynamic library.

Arguments:

    LibraryName - Supplies a pointer to the library name to load.

    Flags - Supplies a bitfield of flags associated with the request.

    Handle - Supplies a pointer where a handle to the dynamic library will be
        returned on success. INVALID_HANDLE will be returned on failure.

Return Value:

    Status code.

--*/

{

    PLOADED_IMAGE LoadedImage;
    KSTATUS Status;

    OspAcquireImageLock();
    if (OsLoadedImagesHead.Next == NULL) {
        Status = OspLoadInitialImageList(FALSE);
        if (!KSUCCESS(Status)) {
            OspReleaseImageLock();
            goto LoadLibraryEnd;
        }
    }

    *Handle = INVALID_HANDLE;
    LoadedImage = NULL;
    Status = ImLoadExecutable(&OsLoadedImagesHead,
                              LibraryName,
                              NULL,
                              NULL,
                              NULL,
                              0,
                              0,
                              &LoadedImage,
                              NULL);

    OspReleaseImageLock();
    if (!KSUCCESS(Status)) {
        goto LoadLibraryEnd;
    }

    OspImInitializeImages(&OsLoadedImagesHead);
    *Handle = LoadedImage;

LoadLibraryEnd:
    return Status;
}

OS_API
VOID
OsFreeLibrary (
    HANDLE Library
    )

/*++

Routine Description:

    This routine indicates a release of the resources associated with a
    previously loaded library. This may or may not actually unload the library
    depending on whether or not there are other references to it.

Arguments:

    Library - Supplies the library to release.

Return Value:

    None.

--*/

{

    if (Library == INVALID_HANDLE) {
        return;
    }

    OspAcquireImageLock();
    ImImageReleaseReference(Library);
    OspReleaseImageLock();
    return;
}

OS_API
KSTATUS
OsGetLibrarySymbolAddress (
    HANDLE Library,
    PSTR SymbolName,
    PVOID *Address
    )

/*++

Routine Description:

    This routine returns the address of the given symbol in the given library.
    Both the library and all of its imports will be searched.

Arguments:

    Library - Supplies the library to look up.

    SymbolName - Supplies a pointer to a null terminated string containing the
        name of the symbol to look up.

    Address - Supplies a pointer that on success receives the address of the
        symbol, or NULL on failure.

Return Value:

    STATUS_SUCCESS on success.

    STATUS_INVALID_HANDLE if the library handle is not valid.

    STATUS_NOT_FOUND if the symbol could not be found.

--*/

{

    KSTATUS Status;

    if (Library == INVALID_HANDLE) {
        return STATUS_INVALID_HANDLE;
    }

    OspAcquireImageLock();
    if (OsLoadedImagesHead.Next == NULL) {
        Status = OspLoadInitialImageList(FALSE);
        if (!KSUCCESS(Status)) {
            goto GetLibrarySymbolAddressEnd;
        }
    }

    Status = ImGetSymbolAddress(&OsLoadedImagesHead,
                                Library,
                                SymbolName,
                                TRUE,
                                Address);

GetLibrarySymbolAddressEnd:
    OspReleaseImageLock();
    return Status;
}

OS_API
KSTATUS
OsFlushCache (
    PVOID Address,
    UINTN Size
    )

/*++

Routine Description:

    This routine flushes the caches for a region of memory after executable
    code has been modified.

Arguments:

    Address - Supplies the address of the region to flush.

    Size - Supplies the number of bytes in the region.

Return Value:

    STATUS_SUCCESS on success.

    STATUS_ACCESS_VIOLATION if the given address was not valid.

--*/

{

    SYSTEM_CALL_FLUSH_CACHE Parameters;

    Parameters.Address = Address;
    Parameters.Size = Size;
    OsSystemCall(SystemCallFlushCache, &Parameters);
    return Parameters.Status;
}

OS_API
KSTATUS
OsCreateThreadData (
    PVOID *ThreadData
    )

/*++

Routine Description:

    This routine creates the OS library data necessary to manage a new thread.
    This function is usually called by the C library.

Arguments:

    ThreadData - Supplies a pointer where a pointer to the thread data will be
        returned on success. It is the callers responsibility to destroy this
        thread data. The contents of this data are opaque and should not be
        interpreted. The caller should set this returned pointer as the
        thread pointer.

Return Value:

    Status code.

--*/

{

    KSTATUS Status;

    //
    // Allocate the initial TLS image and control block for the thread.
    //

    OspAcquireImageLock();
    Status = OspTlsAllocate(&OsLoadedImagesHead, ThreadData);
    OspReleaseImageLock();
    return Status;
}

OS_API
VOID
OsDestroyThreadData (
    PVOID ThreadData
    )

/*++

Routine Description:

    This routine destroys the previously created OS library thread data.

Arguments:

    ThreadData - Supplies the previously returned thread data.

Return Value:

    Status code.

--*/

{

    OspAcquireImageLock();
    OspTlsDestroy(ThreadData);
    OspReleaseImageLock();
    return;
}

VOID
OspInitializeImageSupport (
    VOID
    )

/*++

Routine Description:

    This routine initializes the image library for use in the image creation
    tool.

Arguments:

    None.

Return Value:

    None.

--*/

{

    OsInitializeLockDefault(&OsLoadedImagesLock);
    OsPageSize = OsEnvironment->StartData->PageSize;
    OsPageShift = RtlCountTrailingZeros(OsPageSize);
    ImInitialize(&OsImageFunctionTable);
    return;
}

VOID
OspAcquireImageLock (
    VOID
    )

/*++

Routine Description:

    This routine acquires the global image lock.

Arguments:

    None.

Return Value:

    None.

--*/

{

    OsAcquireLock(&OsLoadedImagesLock);
    return;
}

VOID
OspReleaseImageLock (
    VOID
    )

/*++

Routine Description:

    This routine releases the global image lock.

Arguments:

    None.

Return Value:

    None.

--*/

{

    OsReleaseLock(&OsLoadedImagesLock);
    return;
}

//
// --------------------------------------------------------- Internal Functions
//

PVOID
OspImAllocateMemory (
    ULONG Size,
    ULONG Tag
    )

/*++

Routine Description:

    This routine allocates memory for the image library.

Arguments:

    Size - Supplies the number of bytes required for the memory allocation.

    Tag - Supplies a 32-bit ASCII identifier used to tag the memroy allocation.

Return Value:

    Returns a pointer to the memory allocation on success.

    NULL on failure.

--*/

{

    return OsHeapAllocate(Size, Tag);
}

VOID
OspImFreeMemory (
    PVOID Allocation
    )

/*++

Routine Description:

    This routine frees memory allocated by the image library.

Arguments:

    Allocation - Supplies a pointer the allocation to free.

Return Value:

    None.

--*/

{

    OsHeapFree(Allocation);
    return;
}

KSTATUS
OspImOpenFile (
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

    ULONG BinaryNameSize;
    FILE_CONTROL_PARAMETERS_UNION FileControlParameters;
    PFILE_PROPERTIES FileProperties;
    ULONGLONG LocalFileSize;
    KSTATUS Status;

    File->Handle = INVALID_HANDLE;
    BinaryNameSize = RtlStringLength(BinaryName) + 1;
    Status = OsOpen(INVALID_HANDLE,
                    BinaryName,
                    BinaryNameSize,
                    SYS_OPEN_FLAG_READ,
                    FILE_PERMISSION_NONE,
                    &(File->Handle));

    if (!KSUCCESS(Status)) {
        goto OpenFileEnd;
    }

    Status = OsFileControl(File->Handle,
                           FileControlCommandGetFileInformation,
                           &FileControlParameters);

    if (!KSUCCESS(Status)) {
        goto OpenFileEnd;
    }

    FileProperties = &(FileControlParameters.SetFileInformation.FileProperties);
    if (FileProperties->Type != IoObjectRegularFile) {
        Status = STATUS_UNEXPECTED_TYPE;
        goto OpenFileEnd;
    }

    READ_INT64_SYNC(&(FileProperties->FileSize), &LocalFileSize);
    File->Size = LocalFileSize;
    File->ModificationDate = FileProperties->ModifiedTime.Seconds;
    Status = STATUS_SUCCESS;

OpenFileEnd:
    if (!KSUCCESS(Status)) {
        if (File->Handle != INVALID_HANDLE) {
            OsClose(File->Handle);
        }
    }

    return Status;
}

VOID
OspImCloseFile (
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

    KSTATUS Status;

    if (File->Handle != INVALID_HANDLE) {
        Status = OsClose(File->Handle);

        ASSERT(KSUCCESS(Status));

        File->Handle = INVALID_HANDLE;
    }

    return;
}

KSTATUS
OspImLoadFile (
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
    KSTATUS Status;

    AlignedSize = ALIGN_RANGE_UP(File->Size, OsPageSize);
    if (AlignedSize > MAX_UINTN) {
        return STATUS_NOT_SUPPORTED;
    }

    Status = OsMemoryMap(File->Handle,
                         0,
                         (UINTN)AlignedSize,
                         SYS_MAP_FLAG_READ,
                         &(Buffer->Data));

    if (!KSUCCESS(Status)) {
        return Status;
    }

    Buffer->Size = File->Size;
    return Status;
}

VOID
OspImUnloadBuffer (
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

    UINTN AlignedSize;
    KSTATUS Status;

    ASSERT(Buffer->Data != NULL);

    AlignedSize = ALIGN_RANGE_UP(File->Size, OsPageSize);
    Status = OsMemoryUnmap(Buffer->Data, AlignedSize);

    ASSERT(KSUCCESS(Status));

    Buffer->Data = NULL;
    return;
}

KSTATUS
OspImAllocateAddressSpace (
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

    UINTN AlignedSize;
    ULONG MapFlags;
    KSTATUS Status;

    //
    // Memory map a region to use.
    //

    AlignedSize = ALIGN_RANGE_UP(Size, OsPageSize);
    MapFlags = SYS_MAP_FLAG_READ | SYS_MAP_FLAG_WRITE | SYS_MAP_FLAG_EXECUTE;
    Status = OsMemoryMap(File->Handle,
                         0,
                         AlignedSize,
                         MapFlags,
                         Address);

    *AccessibleAddress = *Address;
    *Handle = *Address;
    return Status;
}

VOID
OspImFreeAddressSpace (
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

    KSTATUS Status;

    Status = OsMemoryUnmap(Address, Size);

    ASSERT(KSUCCESS(Status));

    return;
}

KSTATUS
OspImMapImageSegment (
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
    HANDLE FileHandle;
    PVOID FileRegion;
    UINTN FileRegionSize;
    UINTN FileSize;
    UINTN IoSize;
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
    UINTN SegmentAddress;
    KSTATUS Status;

    ASSERT((PreviousSegment == NULL) ||
           (Segment->VirtualAddress > PreviousSegment->VirtualAddress));

    FileRegion = NULL;
    FileRegionSize = 0;
    FileHandle = INVALID_HANDLE;
    if (File != NULL) {
        FileHandle = File->Handle;
    }

    FileSize = Segment->FileSize;
    MemorySize = Segment->MemorySize;

    ASSERT((FileSize == Segment->FileSize) &&
           (MemorySize == Segment->MemorySize));

    //
    // Map everything readable and writable for now, it will get fixed up
    // during finalization.
    //

    MapFlags = SYS_MAP_FLAG_READ | SYS_MAP_FLAG_WRITE;
    if ((Segment->Flags & IMAGE_MAP_FLAG_EXECUTE) != 0) {
        MapFlags |= SYS_MAP_FLAG_EXECUTE;
    }

    if ((Segment->Flags & IMAGE_MAP_FLAG_FIXED) != 0) {
        MapFlags |= SYS_MAP_FLAG_FIXED;
    }

    //
    // Handle the first part, which may overlap with the previous segment.
    //

    PageSize = OsPageSize;
    PageMask = PageSize - 1;
    SegmentAddress = (UINTN)(Segment->VirtualAddress);
    if (PreviousSegment != NULL) {
        PreviousEnd = (UINTN)(PreviousSegment->VirtualAddress) +
                      PreviousSegment->MemorySize;

        RegionEnd = ALIGN_RANGE_UP(PreviousEnd, PageSize);
        if (RegionEnd > SegmentAddress) {

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

            Status = OsPerformIo(FileHandle,
                                 FileOffset,
                                 IoSize,
                                 0,
                                 SYS_WAIT_TIME_INDEFINITE,
                                 (PVOID)SegmentAddress,
                                 &BytesCompleted);

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

            Status = OsFlushCache((PVOID)SegmentAddress, RegionSize);

            ASSERT(KSUCCESS(Status));

            FileOffset += IoSize;
            FileSize -= IoSize;
            MemorySize -= RegionSize;
            SegmentAddress = RegionEnd;

        //
        // If there is a hole in between the previous segment and this one,
        // change the protection to none for the hole.
        //

        } else {
            RegionSize = SegmentAddress - RegionEnd;
            RegionSize = ALIGN_RANGE_DOWN(RegionSize, PageSize);
            if (RegionSize != 0) {
                Status = OsSetMemoryProtection((PVOID)RegionEnd, RegionSize, 0);
                if (!KSUCCESS(Status)) {

                    ASSERT(FALSE);

                    goto MapImageSegmentEnd;
                }
            }
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

            //
            // Memory map the file to the desired address. The address space
            // allocation was created by memory mapping the beginning of the
            // file, so skip the mapping if it's trying to do exactly that.
            // This saves a redundant system call.
            //

            if ((FileOffset != PageOffset) ||
                (FileRegion != AddressSpaceAllocation)) {

                Status = OsMemoryMap(FileHandle,
                                     FileOffset - PageOffset,
                                     FileRegionSize,
                                     MapFlags,
                                     &FileRegion);

                if (!KSUCCESS(Status)) {
                    RtlDebugPrint("Failed to map %x bytes at %x: %x\n",
                                  FileRegionSize,
                                  FileRegion,
                                  Status);

                    FileRegionSize = 0;
                    goto MapImageSegmentEnd;
                }
            }

            IoSize = 0;

        //
        // The file offsets don't agree. Allocate a region for reading.
        //

        } else {
            Status = OsMemoryMap(INVALID_HANDLE,
                                 0,
                                 FileRegionSize,
                                 MapFlags | SYS_MAP_FLAG_ANONYMOUS,
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

        //
        // If the mapping wasn't at the expected location, adjust.
        //

        if ((UINTN)FileRegion != SegmentAddress - PageOffset) {

            ASSERT((PreviousSegment == NULL) &&
                   ((Segment->Flags & IMAGE_MAP_FLAG_FIXED) == 0));

            SegmentAddress = (UINTN)FileRegion + PageOffset;
            Segment->VirtualAddress = (PVOID)SegmentAddress;
        }

        Segment->MappingStart = FileRegion;

        //
        // Read from the file if the file wasn't mapped directly.
        //

        if (IoSize != 0) {
            Status = OsPerformIo(FileHandle,
                                 FileOffset,
                                 IoSize,
                                 0,
                                 SYS_WAIT_TIME_INDEFINITE,
                                 (PVOID)SegmentAddress,
                                 &BytesCompleted);

            if (!KSUCCESS(Status)) {
                goto MapImageSegmentEnd;
            }

            if (BytesCompleted != IoSize) {
                Status = STATUS_END_OF_FILE;
                goto MapImageSegmentEnd;
            }

            Status = OsFlushCache((PVOID)SegmentAddress, IoSize);

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
            Status = OsFlushCache((PVOID)SegmentAddress,
                                  NextPage - SegmentAddress);

            ASSERT(KSUCCESS(Status));
        }

        if (NextPage >= SegmentAddress + MemorySize) {
            Status = STATUS_SUCCESS;
            goto MapImageSegmentEnd;
        }

        MemorySize -= NextPage - SegmentAddress;
        SegmentAddress = NextPage;

        //
        // If the file region was decided, any remaining memory region is now
        // fixed.
        //

        MapFlags |= SYS_MAP_FLAG_FIXED;
    }

    //
    // Memory map the remaining region.
    //

    PageOffset = SegmentAddress & PageMask;
    Address = (PVOID)(SegmentAddress - PageOffset);
    MemoryRegionSize = MemorySize + PageOffset;
    MemoryRegionSize = ALIGN_RANGE_UP(MemoryRegionSize, PageSize);
    Status = OsMemoryMap(INVALID_HANDLE,
                         0,
                         MemoryRegionSize,
                         MapFlags | SYS_MAP_FLAG_ANONYMOUS,
                         &Address);

    if (!KSUCCESS(Status)) {
        RtlDebugPrint("Failed to map %x bytes at %x: %x\n",
                      MemorySize + PageOffset,
                      Address,
                      Status);

        goto MapImageSegmentEnd;
    }

    if (Segment->MappingStart == NULL) {
        Segment->MappingStart = Address;
    }

MapImageSegmentEnd:
    if (!KSUCCESS(Status)) {
        if (FileRegionSize != 0) {
            OsMemoryUnmap(FileRegion, FileRegionSize);
        }
    }

    return Status;
}

VOID
OspImUnmapImageSegment (
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

    //
    // There's no need to unmap each segment individually, the free address
    // space function does it all at the end.
    //

    return;
}

KSTATUS
OspImNotifyImageLoad (
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

    PROCESS_DEBUG_MODULE_CHANGE Notification;
    KSTATUS Status;

    ASSERT(OsLoadedImagesHead.Next != NULL);

    Notification.Version = PROCESS_DEBUG_MODULE_CHANGE_VERSION;
    Notification.Load = TRUE;
    Notification.Image = Image;
    Notification.BinaryNameSize = RtlStringLength(Image->BinaryName) + 1;
    Status = OsDebug(DebugCommandReportModuleChange,
                     0,
                     NULL,
                     &Notification,
                     sizeof(PROCESS_DEBUG_MODULE_CHANGE),
                     0);

    if (!KSUCCESS(Status)) {
        RtlDebugPrint("Warning: Failed to notify kernel of module %s: %x\n",
                      Image->BinaryName,
                      Status);
    }

    Status = OspImAssignModuleNumber(Image);
    if (!KSUCCESS(Status)) {
        return Status;
    }

    return STATUS_SUCCESS;
}

VOID
OspImNotifyImageUnload (
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

    PIMAGE_STATIC_FUNCTION *Begin;
    PIMAGE_STATIC_FUNCTION *DestructorPointer;
    PROCESS_DEBUG_MODULE_CHANGE Notification;
    PIMAGE_STATIC_FUNCTIONS StaticFunctions;
    KSTATUS Status;

    //
    // Release the image lock while calling out to destructors.
    //

    if (OsImExecutableLoaded != FALSE) {
        OspReleaseImageLock();
    }

    //
    // Call the static destructor functions. These are only filled in for
    // dynamic objects. For executables, this is all handled internally in the
    // static portion of the C library.
    //

    StaticFunctions = Image->StaticFunctions;
    if ((StaticFunctions != NULL) &&
        ((Image->LoadFlags & IMAGE_LOAD_FLAG_PRIMARY_EXECUTABLE) == 0)) {

        //
        // Call the .fini_array functions in reverse order.
        //

        if (StaticFunctions->FiniArraySize > sizeof(PIMAGE_STATIC_FUNCTION)) {
            Begin = StaticFunctions->FiniArray;
            DestructorPointer = (PVOID)(Begin) + StaticFunctions->FiniArraySize;
            DestructorPointer -= 1;
            while (DestructorPointer >= Begin) {

                //
                // Call the destructor.
                //

                (*DestructorPointer)();
                DestructorPointer -= 1;
            }
        }

        //
        // Also call the old school _fini destructor if present.
        //

        if (StaticFunctions->FiniFunction != NULL) {
            StaticFunctions->FiniFunction();
        }
    }

    ASSERT(OsLoadedImagesHead.Next != NULL);

    if (OsImExecutableLoaded != FALSE) {
        OspAcquireImageLock();
    }

    //
    // Tear down all the TLS segments for this module.
    //

    OspTlsTearDownModule(Image);

    //
    // Notify the kernel the module is being unloaded.
    //

    Notification.Version = PROCESS_DEBUG_MODULE_CHANGE_VERSION;
    Notification.Load = FALSE;
    Notification.Image = Image;
    Notification.BinaryNameSize = RtlStringLength(Image->BinaryName) + 1;
    Status = OsDebug(DebugCommandReportModuleChange,
                     0,
                     NULL,
                     &Notification,
                     sizeof(PROCESS_DEBUG_MODULE_CHANGE),
                     0);

    if (!KSUCCESS(Status)) {
        RtlDebugPrint("Warning: Failed to unload module %s: %x\n",
                      Image->BinaryName,
                      Status);
    }

    OspImReleaseModuleNumber(Image);
    return;
}

VOID
OspImInvalidateInstructionCacheRegion (
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

    Status = OsFlushCache(Address, Size);

    ASSERT(KSUCCESS(Status));

    return;
}

PSTR
OspImGetEnvironmentVariable (
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

    PPROCESS_ENVIRONMENT Environment;
    UINTN Index;
    BOOL Match;
    UINTN VariableLength;
    PSTR VariableString;

    VariableLength = RtlStringLength(Variable);
    Match = RtlAreStringsEqual(Variable,
                               IMAGE_DYNAMIC_LIBRARY_PATH_VARIABLE,
                               VariableLength + 1);

    if (Match != FALSE) {
        if (OsImLibraryPathOverride != NULL) {
            return OsImLibraryPathOverride;
        }
    }

    if (OsImGetEnvironmentVariable != NULL) {
        return OsImGetEnvironmentVariable(Variable);
    }

    //
    // Search through the initial environment.
    //

    Environment = OsGetCurrentEnvironment();
    for (Index = 0; Index < Environment->EnvironmentCount; Index += 1) {
        VariableString = Environment->Environment[Index];
        Match = RtlAreStringsEqual(Variable,
                                   VariableString,
                                   VariableLength);

        if ((Match != FALSE) && (VariableString[VariableLength] == '=')) {
            return VariableString + VariableLength + 1;
        }
    }

    return NULL;
}

KSTATUS
OspImFinalizeSegments (
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

    PageSize = OsPageSize;
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
        // If the region has a real size, change it's protection to read-only.
        //

        if ((PVOID)End > Segment->MappingStart) {
            Size = End - (UINTN)(Segment->MappingStart);
            MapFlags = SYS_MAP_FLAG_READ;
            if ((Segment->Flags & IMAGE_MAP_FLAG_EXECUTE) != 0) {
                MapFlags |= SYS_MAP_FLAG_EXECUTE;
            }

            Status = OsSetMemoryProtection(Segment->MappingStart,
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

VOID
OspImInitializeImages (
    PLIST_ENTRY ListHead
    )

/*++

Routine Description:

    This routine initializes any new images and calls their static constructors.
    This routine assumes the list lock is already held.

Arguments:

    ListHead - Supplies a pointer to the head of the list of images to
        initialize.

Return Value:

    None.

--*/

{

    PLIST_ENTRY CurrentEntry;
    PLOADED_IMAGE Image;

    //
    // Iterate over list backwards to initialize dependencies before the
    // libraries that depend on them.
    //

    OspAcquireImageLock();
    CurrentEntry = ListHead->Previous;
    while (CurrentEntry != ListHead) {
        Image = LIST_VALUE(CurrentEntry, LOADED_IMAGE, ListEntry);
        if ((Image->Flags & IMAGE_FLAG_INITIALIZED) == 0) {

            //
            // Release the lock around initializing the image.
            //

            OspReleaseImageLock();
            OspImInitializeImage(Image);
            OspAcquireImageLock();
            Image->Flags |= IMAGE_FLAG_INITIALIZED;
        }

        CurrentEntry = CurrentEntry->Previous;
    }

    OspReleaseImageLock();
    return;
}

VOID
OspImInitializeImage (
    PLOADED_IMAGE Image
    )

/*++

Routine Description:

    This routine is called when the image is fully loaded. It flushes the cache
    region and calls the entry point.

Arguments:

    Image - Supplies the image that has just been completely loaded.

Return Value:

    None.

--*/

{

    PIMAGE_STATIC_FUNCTION *ConstructorPointer;
    PIMAGE_STATIC_FUNCTION *End;
    PIMAGE_STATIC_FUNCTIONS StaticFunctions;

    StaticFunctions = Image->StaticFunctions;
    if (StaticFunctions == NULL) {
        return;
    }

    //
    // The executable is responsible for its own initialization.
    //

    if ((Image->LoadFlags & IMAGE_LOAD_FLAG_PRIMARY_EXECUTABLE) != 0) {
        return;
    }

    //
    // Call the .preinit_array functions.
    //

    ConstructorPointer = StaticFunctions->PreinitArray;
    End = ((PVOID)ConstructorPointer) + StaticFunctions->PreinitArraySize;
    while (ConstructorPointer < End) {

        //
        // Call the constructor function. Remember it's an array pointer, hence
        // the extra dereference.
        //

        (*ConstructorPointer)();
        ConstructorPointer += 1;
    }

    //
    // Call the old school init function if it exists.
    //

    if (StaticFunctions->InitFunction != NULL) {
        StaticFunctions->InitFunction();
    }

    //
    // Call the .init_array functions.
    //

    ConstructorPointer = StaticFunctions->InitArray;
    End = ((PVOID)ConstructorPointer) + StaticFunctions->InitArraySize;
    while (ConstructorPointer < End) {

        //
        // Call the constructor function. Remember it's an array pointer, hence
        // the extra dereference.
        //

        (*ConstructorPointer)();
        ConstructorPointer += 1;
    }

    return;
}

KSTATUS
OspLoadInitialImageList (
    BOOL Relocate
    )

/*++

Routine Description:

    This routine attempts to populate the initial image list with data
    from the kernel.

Arguments:

    Relocate - Supplies a boolean indicating whether or not the loaded images
        should be relocated.

Return Value:

    Status code.

--*/

{

    PLOADED_IMAGE Executable;
    ULONG Flags;
    IMAGE_BUFFER ImageBuffer;
    PLOADED_IMAGE Interpreter;
    PLOADED_IMAGE OsLibrary;
    PPROCESS_START_DATA StartData;
    KSTATUS Status;

    Interpreter = NULL;

    ASSERT(OsLoadedImagesHead.Next == NULL);

    INITIALIZE_LIST_HEAD(&OsLoadedImagesHead);
    RtlZeroMemory(&ImageBuffer, sizeof(IMAGE_BUFFER));
    StartData = OsEnvironment->StartData;
    ImageBuffer.Size = MAX_UINTN;
    ImageBuffer.Data = StartData->OsLibraryBase;
    Status = ImAddImage(NULL, &ImageBuffer, &OsLibrary);
    if (!KSUCCESS(Status)) {
        goto LoadInitialImageListEnd;
    }

    INSERT_BEFORE(&(OsLibrary->ListEntry), &OsLoadedImagesHead);
    OsLibrary->LoadFlags |= IMAGE_LOAD_FLAG_PRIMARY_LOAD;
    if ((StartData->InterpreterBase != NULL) &&
        (StartData->InterpreterBase != StartData->OsLibraryBase)) {

        ImageBuffer.Data = StartData->InterpreterBase;
        Status = ImAddImage(NULL, &ImageBuffer, &Interpreter);
        if (!KSUCCESS(Status)) {
            goto LoadInitialImageListEnd;
        }

        INSERT_BEFORE(&(Interpreter->ListEntry), &OsLoadedImagesHead);
        Interpreter->LoadFlags |= IMAGE_LOAD_FLAG_PRIMARY_LOAD;
    }

    ASSERT(StartData->ExecutableBase != StartData->InterpreterBase);

    if (StartData->ExecutableBase != StartData->OsLibraryBase) {
        ImageBuffer.Data = StartData->ExecutableBase;
        Status = ImAddImage(NULL, &ImageBuffer, &Executable);
        if (!KSUCCESS(Status)) {
            goto LoadInitialImageListEnd;
        }

        INSERT_BEFORE(&(Executable->ListEntry), &OsLoadedImagesHead);

    } else {
        Executable = OsLibrary;
    }

    Executable->LoadFlags |= IMAGE_LOAD_FLAG_PRIMARY_LOAD |
                             IMAGE_LOAD_FLAG_PRIMARY_EXECUTABLE;

    //
    // If no relocations should be performed, another binary is taking care of
    // the binary linking. If this library ever requires relocations to work
    // properly, then relocate just the OS library image here.
    //

    if (Relocate != FALSE) {
        Status = ImLoadImports(&OsLoadedImagesHead);
        if (!KSUCCESS(Status)) {
            RtlDebugPrint("Failed to load initial imports: %x\n", Status);
            goto LoadInitialImageListEnd;
        }

    } else {
        Flags = IMAGE_FLAG_IMPORTS_LOADED | IMAGE_FLAG_RELOCATED |
                IMAGE_FLAG_INITIALIZED;

        OsLibrary->Flags |= Flags;
        if (Interpreter != NULL) {
            Interpreter->Flags |= Flags;
        }

        if (Executable != NULL) {
            Executable->Flags |= Flags;
        }
    }

    Status = STATUS_SUCCESS;

LoadInitialImageListEnd:
    return Status;
}

KSTATUS
OspImAssignModuleNumber (
    PLOADED_IMAGE Image
    )

/*++

Routine Description:

    This routine attempts to assign the newly loaded module an image number.

Arguments:

    Image - Supplies a pointer to the image to assign.

Return Value:

    STATUS_SUCCESS on success.

    STATUS_INSUFFICIENT_RESOURCES if an allocation failed.

--*/

{

    UINTN Bitmap;
    UINTN BlockIndex;
    UINTN Index;
    PUINTN NewBuffer;
    UINTN NewCapacity;

    ASSERT(Image->ModuleNumber == 0);

    for (BlockIndex = 0;
         BlockIndex < OsImModuleNumberBitmapSize;
         BlockIndex += 1) {

        Bitmap = OsImModuleNumberBitmap[BlockIndex];
        for (Index = 0; Index < sizeof(UINTN) * BITS_PER_BYTE; Index += 1) {
            if ((Bitmap & (1 << Index)) == 0) {
                OsImModuleNumberBitmap[BlockIndex] |= 1 << Index;
                Image->ModuleNumber =
                        (BlockIndex * (sizeof(UINTN) * BITS_PER_BYTE)) + Index;

                if (Image->ModuleNumber > OsImModuleGeneration) {
                    OsImModuleGeneration += 1;
                }

                return STATUS_SUCCESS;
            }
        }
    }

    //
    // Allocate more space.
    //

    NewCapacity = OsImModuleNumberBitmapSize * 2;
    if (OsImModuleNumberBitmap == &OsImStaticModuleNumberBitmap) {
        NewBuffer = OsHeapAllocate(NewCapacity * sizeof(UINTN),
                                   OS_IMAGE_ALLOCATION_TAG);

    } else {
        NewBuffer = OsHeapReallocate(OsImModuleNumberBitmap,
                                     NewCapacity * sizeof(UINTN),
                                     OS_IMAGE_ALLOCATION_TAG);
    }

    if (NewBuffer == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(NewBuffer + OsImModuleNumberBitmapSize,
                  (NewCapacity - OsImModuleNumberBitmapSize) * sizeof(UINTN));

    Image->ModuleNumber = OsImModuleNumberBitmapSize * sizeof(UINTN) *
                          BITS_PER_BYTE;

    if (Image->ModuleNumber > OsImModuleGeneration) {
        OsImModuleGeneration += 1;
    }

    OsImModuleNumberBitmap = NewBuffer;
    OsImModuleNumberBitmapSize = NewCapacity;
    return STATUS_SUCCESS;
}

VOID
OspImReleaseModuleNumber (
    PLOADED_IMAGE Image
    )

/*++

Routine Description:

    This routine releases the module number assigned to the loaded image.

Arguments:

    Image - Supplies a pointer to the image to release.

Return Value:

    None.

--*/

{

    UINTN BlockIndex;
    UINTN BlockOffset;

    ASSERT((Image->ModuleNumber != 0) &&
           (Image->ModuleNumber <
            OsImModuleNumberBitmapSize * sizeof(UINTN) * BITS_PER_BYTE));

    BlockIndex = Image->ModuleNumber / (sizeof(UINTN) * BITS_PER_BYTE);
    BlockOffset = Image->ModuleNumber % (sizeof(UINTN) * BITS_PER_BYTE);
    OsImModuleNumberBitmap[BlockIndex] &= ~(1 << BlockOffset);
    Image->ModuleNumber = 0;
    return;
}
