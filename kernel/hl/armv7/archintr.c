/*++

Copyright (c) 2012 Minoca Corp. All Rights Reserved

Module Name:

    archintr.c

Abstract:

    This module implements ARMv7 system interrupt functionality.

Author:

    Evan Green 11-Aug-2012

Environment:

    Kernel

--*/

//
// ------------------------------------------------------------------- Includes
//

#include <minoca/kernel.h>
#include <minoca/arm.h>
#include <minoca/kdebug.h>
#include "../hlp.h"
#include "../intrupt.h"
#include "../profiler.h"

//
// ---------------------------------------------------------------- Definitions
//

//
// Define the number of IPI lines needed for normal system operation on ARM
// processors.
//

#define REQUIRED_IPI_LINE_COUNT 5

//
// ----------------------------------------------- Internal Function Prototypes
//

//
// System interrupt service routines.
//

INTERRUPT_STATUS
KeIpiServiceRoutine (
    PVOID Context
    );

INTERRUPT_STATUS
MmTlbInvalidateIpiServiceRoutine (
    PVOID Context
    );

//
// Builtin hardware module function prototypes.
//

VOID
HlpCpInterruptModuleEntry (
    PHARDWARE_MODULE_KERNEL_SERVICES Services
    );

VOID
HlpOmap3InterruptModuleEntry (
    PHARDWARE_MODULE_KERNEL_SERVICES Services
    );

VOID
HlpAm335InterruptModuleEntry (
    PHARDWARE_MODULE_KERNEL_SERVICES Services
    );

VOID
HlpGicModuleEntry (
    PHARDWARE_MODULE_KERNEL_SERVICES Services
    );

VOID
HlpBcm2709InterruptModuleEntry (
    PHARDWARE_MODULE_KERNEL_SERVICES Services
    );

INTERRUPT_STATUS
HlpNmiServiceRoutine (
    PVOID Context
    );

//
// ------------------------------------------------------ Data Type Definitions
//

//
// -------------------------------------------------------------------- Globals
//

//
// Built-in hardware modules.
//

PHARDWARE_MODULE_ENTRY HlBuiltinModules[] = {
    HlpCpInterruptModuleEntry,
    HlpOmap3InterruptModuleEntry,
    HlpAm335InterruptModuleEntry,
    HlpGicModuleEntry,
    HlpBcm2709InterruptModuleEntry,
    NULL
};

//
// Store the first vector number of the processor's interrupt array.
//

ULONG HlFirstConfigurableVector = MINIMUM_VECTOR;

//
// ------------------------------------------------------------------ Functions
//

KSTATUS
HlpArchInitializeInterrupts (
    VOID
    )

/*++

Routine Description:

    This routine performs architecture-specific initialization for the
    interrupt subsystem.

Arguments:

    None.

Return Value:

    Status code.

--*/

{

    ULONG LineIndex;
    PHARDWARE_MODULE_ENTRY ModuleEntry;
    ULONG ModuleIndex;
    KSTATUS Status;

    //
    // Connect some built-in vectors.
    //

    LineIndex = HlpInterruptGetIpiLineIndex(IpiTypePacket);
    HlIpiKInterrupt[LineIndex] = HlpCreateAndConnectInternalInterrupt(
                                                          VECTOR_IPI_INTERRUPT,
                                                          RunLevelIpi,
                                                          KeIpiServiceRoutine,
                                                          NULL);

    if (HlIpiKInterrupt[LineIndex] == NULL) {
        Status = STATUS_UNSUCCESSFUL;
        goto ArchInitializeInterruptsEnd;
    }

    LineIndex = HlpInterruptGetIpiLineIndex(IpiTypeTlbFlush);
    HlIpiKInterrupt[LineIndex] = HlpCreateAndConnectInternalInterrupt(
                                              VECTOR_TLB_IPI,
                                              RunLevelIpi,
                                              MmTlbInvalidateIpiServiceRoutine,
                                              NULL);

    if (HlIpiKInterrupt[LineIndex] == NULL) {
        Status = STATUS_UNSUCCESSFUL;
        goto ArchInitializeInterruptsEnd;
    }

    LineIndex = HlpInterruptGetIpiLineIndex(IpiTypeNmi);
    HlIpiKInterrupt[LineIndex] = HlpCreateAndConnectInternalInterrupt(
                                                 VECTOR_NMI,
                                                 RunLevelHigh,
                                                 HlpNmiServiceRoutine,
                                                 INTERRUPT_CONTEXT_TRAP_FRAME);

    if (HlIpiKInterrupt[LineIndex] == NULL) {
        Status = STATUS_UNSUCCESSFUL;
        goto ArchInitializeInterruptsEnd;
    }

    LineIndex = HlpInterruptGetIpiLineIndex(IpiTypeProfiler);
    HlIpiKInterrupt[LineIndex] = HlpCreateAndConnectInternalInterrupt(
                                                 VECTOR_PROFILER_INTERRUPT,
                                                 RunLevelHigh,
                                                 HlpProfilerInterruptHandler,
                                                 INTERRUPT_CONTEXT_TRAP_FRAME);

    if (HlIpiKInterrupt[LineIndex] == NULL) {
        Status = STATUS_UNSUCCESSFUL;
        goto ArchInitializeInterruptsEnd;
    }

    LineIndex = HlpInterruptGetIpiLineIndex(IpiTypeClock);
    HlIpiKInterrupt[LineIndex] = HlpCreateAndConnectInternalInterrupt(
                                                        VECTOR_CLOCK_INTERRUPT,
                                                        RunLevelClock,
                                                        NULL,
                                                        NULL);

    if (HlIpiKInterrupt[LineIndex] == NULL) {
        Status = STATUS_UNSUCCESSFUL;
        goto ArchInitializeInterruptsEnd;
    }

    //
    // Loop through and initialize every built in hardware module.
    //

    ModuleIndex = 0;
    while (HlBuiltinModules[ModuleIndex] != NULL) {
        ModuleEntry = HlBuiltinModules[ModuleIndex];
        ModuleEntry(&HlHardwareModuleServices);
        ModuleIndex += 1;
    }

    Status = STATUS_SUCCESS;

ArchInitializeInterruptsEnd:
    return Status;
}

ULONG
HlpInterruptGetIpiVector (
    IPI_TYPE IpiType
    )

/*++

Routine Description:

    This routine determines the architecture-specific hardware vector to use
    for the given IPI type.

Arguments:

    IpiType - Supplies the IPI type to send.

Return Value:

    Returns the vector that the given IPI type runs on.

--*/

{

    switch (IpiType) {
    case IpiTypePacket:
        return VECTOR_IPI_INTERRUPT;

    case IpiTypeTlbFlush:
        return VECTOR_TLB_IPI;

    case IpiTypeNmi:
        return VECTOR_NMI;

    case IpiTypeProfiler:
        return VECTOR_PROFILER_INTERRUPT;

    case IpiTypeClock:
        return VECTOR_CLOCK_INTERRUPT;

    default:
        break;
    }

    ASSERT(FALSE);

    return 0;
}

ULONG
HlpInterruptGetRequiredIpiLineCount (
    VOID
    )

/*++

Routine Description:

    This routine determines the number of "software only" interrupt lines that
    are required for normal system operation. This routine is architecture
    dependent.

Arguments:

    None.

Return Value:

    Returns the number of software IPI lines needed for system operation.

--*/

{

    return REQUIRED_IPI_LINE_COUNT;
}

ULONG
HlpInterruptGetIpiLineIndex (
    IPI_TYPE IpiType
    )

/*++

Routine Description:

    This routine determines which of the IPI lines should be used for the
    given IPI type.

Arguments:

    IpiType - Supplies the type of IPI to be sent.

Return Value:

    Returns the IPI line index corresponding to the given IPI type.

--*/

{

    //
    // Each IPI type has its own line.
    //

    ASSERT(IpiType - 1 < REQUIRED_IPI_LINE_COUNT);

    return IpiType - 1;
}

VOID
HlpInterruptGetStandardCpuLine (
    PINTERRUPT_LINE Line
    )

/*++

Routine Description:

    This routine determines the architecture-specific standard CPU interrupt
    line that most interrupts get routed to.

Arguments:

    Line - Supplies a pointer where the standard CPU interrupt line will be
        returned.

Return Value:

    None.

--*/

{

    Line->Type = InterruptLineControllerSpecified;
    Line->U.Local.Controller = INTERRUPT_CPU_IDENTIFIER;
    Line->U.Local.Line = INTERRUPT_CPU_IRQ_PIN;
    return;
}

INTERRUPT_CAUSE
HlpInterruptAcknowledge (
    PINTERRUPT_CONTROLLER *ProcessorController,
    PULONG Vector,
    PULONG MagicCandy
    )

/*++

Routine Description:

    This routine begins an interrupt, acknowledging its receipt into the
    processor.

Arguments:

    ProcessorController - Supplies a pointer where on input the interrupt
        controller that owns this processor will be supplied. This pointer may
        pointer to NULL, in which case the interrupt controller that fired the
        interrupt will be returned.

    Vector - Supplies a pointer to the vector on input. For non-vectored
        architectures, the vector corresponding to the interrupt that fired
        will be returned.

    MagicCandy - Supplies a pointer where an opaque token regarding the
        interrupt will be returned. This token is only used by the interrupt
        controller hardware module.

Return Value:

    Returns the cause of the interrupt.

--*/

{

    INTERRUPT_CAUSE Cause;
    PINTERRUPT_CONTROLLER Controller;
    ULONG ControllerCount;
    ULONG ControllerIndex;
    INTERRUPT_LINE Line;
    PINTERRUPT_LINES Lines;
    ULONG Offset;
    KSTATUS Status;

    //
    // If there is a controller associated with this processor, use it.
    //

    Controller = *ProcessorController;
    if (Controller != NULL) {
        Cause = Controller->FunctionTable.BeginInterrupt(
                                                     Controller->PrivateContext,
                                                     &Line,
                                                     MagicCandy);

        if ((Cause == InterruptCauseSpuriousInterrupt) ||
            (Cause == InterruptCauseNoInterruptHere)) {

            return Cause;
        }

    //
    // There is no controller, so loop through all the controllers seeing if
    // anyone responds.
    //

    } else {
        ControllerCount = HlInterruptControllerCount;
        for (ControllerIndex = 0;
             ControllerIndex < ControllerCount;
             ControllerIndex += 1) {

            Controller = HlInterruptControllers[ControllerIndex];
            if (Controller == NULL) {
                continue;
            }

            Cause = Controller->FunctionTable.BeginInterrupt(
                                                     Controller->PrivateContext,
                                                     &Line,
                                                     MagicCandy);

            if (Cause == InterruptCauseLineFired) {
                break;
            }
        }

        if (ControllerIndex == ControllerCount) {
            return InterruptCauseNoInterruptHere;
        }
    }

    //
    // Determine the vector corresponding to the interrupt lines that fired.
    //

    ASSERT(Line.Type == InterruptLineControllerSpecified);

    Status = HlpInterruptFindLines(&Line, ProcessorController, &Lines, &Offset);

    ASSERT(KSUCCESS(Status));

    *Vector = Lines->State[Offset].PublicState.Vector;

    //
    // Ensure all writes to the interrupt controller complete before interrupts
    // are enabled at the processor.
    //

    ArSerializeExecution();
    return TRUE;
}

PKINTERRUPT
HlpInterruptGetClockKInterrupt (
    VOID
    )

/*++

Routine Description:

    This routine returns the clock timer's KINTERRUPT structure.

Arguments:

    None.

Return Value:

    Returns a pointer to the clock KINTERRUPT.

--*/

{

    ULONG LineIndex;

    LineIndex = HlpInterruptGetIpiLineIndex(IpiTypeClock);
    return HlIpiKInterrupt[LineIndex];
}

PKINTERRUPT
HlpInterruptGetProfilerKInterrupt (
    VOID
    )

/*++

Routine Description:

    This routine returns the profiler timer's KINTERRUPT structure.

Arguments:

    None.

Return Value:

    Returns a pointer to the profiler KINTERRUPT.

--*/

{

    ULONG IpiIndex;

    IpiIndex = HlpInterruptGetIpiLineIndex(IpiTypeProfiler);
    return HlIpiKInterrupt[IpiIndex];
}

//
// --------------------------------------------------------- Internal Functions
//

INTERRUPT_STATUS
HlpNmiServiceRoutine (
    PVOID Context
    )

/*++

Routine Description:

    This routine represents the interrupt service routine for NMI interrupts.

Arguments:

    Context - Supplies a pointer to the current trap frame.

Return Value:

    Claimed always.

--*/

{

    KdNmiHandler(Context);
    return InterruptStatusClaimed;
}
