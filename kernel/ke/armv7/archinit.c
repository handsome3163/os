/*++

Copyright (c) 2012 Minoca Corp. All Rights Reserved

Module Name:

    archinit.c

Abstract:

    This module implements kernel executive initialization routines specific
    to the ARM architecture.

Author:

    Evan Green 27-Sep-2012

Environment:

    Kernel

--*/

//
// ------------------------------------------------------------------- Includes
//

#include <minoca/kernel.h>
#include <minoca/bootload.h>
#include <minoca/arm.h>

//
// ---------------------------------------------------------------- Definitions
//

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
// ------------------------------------------------------------------ Functions
//

KSTATUS
KepArchInitialize (
    PKERNEL_INITIALIZATION_BLOCK Parameters,
    ULONG Phase
    )

/*++

Routine Description:

    This routine performs architecture-specific initialization for the kernel
    executive.

Arguments:

    Parameters - Supplies a pointer to the kernel initialization parameters
        from the loader.

    Phase - Supplies the initialization phase.

Return Value:

    Status code.

--*/

{

    KSTATUS Status;

    ASSERT(Phase == 0);

    //
    // There is currently no ARM-specific initialization required. The IPI
    // handler will need to be wired up here once MP is supported.
    //

    Status = STATUS_SUCCESS;
    return Status;
}

//
// --------------------------------------------------------- Internal Functions
//
