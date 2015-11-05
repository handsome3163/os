/*++

Copyright (c) 2012 Minoca Corp. All Rights Reserved

Module Name:

    imp.h

Abstract:

    This header contains definitions internal to the Image Library.

Author:

    Evan Green 13-Oct-2012

--*/

//
// ------------------------------------------------------------------- Includes
//

#define RTL_API DLLEXPORT

#include <minoca/driver.h>

//
// ---------------------------------------------------------------- Definitions
//

//
// Define the initial amount to read for loading image segments.
//

#define IMAGE_INITIAL_READ_SIZE 1024

//
// Define the macros to the various functions.
//

#define ImAllocateMemory ImImportTable->AllocateMemory
#define ImFreeMemory ImImportTable->FreeMemory
#define ImOpenFile ImImportTable->OpenFile
#define ImCloseFile ImImportTable->CloseFile
#define ImLoadFile ImImportTable->LoadFile
#define ImReadFile ImImportTable->ReadFile
#define ImUnloadBuffer ImImportTable->UnloadBuffer
#define ImAllocateAddressSpace ImImportTable->AllocateAddressSpace
#define ImFreeAddressSpace ImImportTable->FreeAddressSpace
#define ImMapImageSegment ImImportTable->MapImageSegment
#define ImUnmapImageSegment ImImportTable->UnmapImageSegment
#define ImNotifyImageLoad ImImportTable->NotifyImageLoad
#define ImNotifyImageUnload ImImportTable->NotifyImageUnload
#define ImInvalidateInstructionCacheRegion \
    ImImportTable->InvalidateInstructionCacheRegion

#define ImGetEnvironmentVariable ImImportTable->GetEnvironmentVariable
#define ImFinalizeSegments ImImportTable->FinalizeSegments

//
// Define the maximum import recursion depth.
//

#define MAX_IMPORT_RECURSION_DEPTH 1000

//
// ------------------------------------------------------ Data Type Definitions
//

//
// -------------------------------------------------------------------- Globals
//

extern PIM_IMPORT_TABLE ImImportTable;

//
// -------------------------------------------------------- Function Prototypes
//

PVOID
ImpReadBuffer (
    PIMAGE_FILE_INFORMATION File,
    PIMAGE_BUFFER Buffer,
    UINTN Offset,
    UINTN Size
    );

/*++

Routine Description:

    This routine handles access to an image buffer.

Arguments:

    File - Supplies an optional pointer to the file information, if the buffer
        may need to be resized.

    Buffer - Supplies a pointer to the buffer to read from.

    Offset - Supplies the offset from the start of the file to read.

    Size - Supplies the required size.

Return Value:

    Returns a pointer to the image file at the requested offset on success.

    NULL if the range is invalid or the file could not be fully loaded.

--*/