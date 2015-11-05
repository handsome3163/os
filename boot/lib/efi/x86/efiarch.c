/*++

Copyright (c) 2014 Minoca Corp. All Rights Reserved

Module Name:

    efiarch.c

Abstract:

    This module implements CPU architecture support for UEFI in the boot loader.

Author:

    Evan Green 11-Feb-2014

Environment:

    Boot

--*/

//
// ------------------------------------------------------------------- Includes
//

#include <minoca/kernel.h>
#include <minoca/uefi/uefi.h>
#include <minoca/x86.h>
#include "firmware.h"
#include "efisup.h"

//
// ---------------------------------------------------------------- Definitions
//

//
// Define the supposed size of the stack EFI hands to the boot application.
//

#define EFI_STACK_SIZE 0x4000

//
// ------------------------------------------------------ Data Type Definitions
//

//
// ----------------------------------------------- Internal Function Prototypes
//

//
// -------------------------------------------------------------------- Globals
//

//
// Save the original EFI state.
//

ULONG BoFirmwareCs;
ULONG BoFirmwareDs;
ULONG BoFirmwareEs;
ULONG BoFirmwareFs;
ULONG BoFirmwareGs;
ULONG BoFirmwareSs;
ULONG BoFirmwareEflags;
TABLE_REGISTER BoFirmwareIdt;
TABLE_REGISTER BoFirmwareGdt;

//
// Define the global used to temporarily save the OS loader state when
// switching back to firmware context.
//

ULONG BoLoaderCs;
ULONG BoLoaderDs;
ULONG BoLoaderEs;
ULONG BoLoaderFs;
ULONG BoLoaderGs;
ULONG BoLoaderSs;
ULONG BoLoaderEflags;
TABLE_REGISTER BoLoaderIdt;
TABLE_REGISTER BoLoaderGdt;

//
// ------------------------------------------------------------------ Functions
//

VOID
BopEfiArchInitialize (
    PVOID *TopOfStack,
    PULONG StackSize
    )

/*++

Routine Description:

    This routine performs early architecture specific initialization of an EFI
    application.

Arguments:

    TopOfStack - Supplies a pointer where an approximation of the top of the
        stack will be returned.

    StackSize - Supplies a pointer where the stack size will be returned.

Return Value:

    None.

--*/

{

    UINTN StackTop;

    StackTop = BopEfiGetStackPointer();
    StackTop = ALIGN_RANGE_UP(StackTop, EFI_PAGE_SIZE);
    *TopOfStack = (PVOID)StackTop;
    *StackSize = EFI_STACK_SIZE;
    BopEfiSaveInitialState();
    return;
}

//
// --------------------------------------------------------- Internal Functions
//
