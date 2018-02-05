/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WasmFaultSignalHandler.h"

#if ENABLE(WEBASSEMBLY)

#include "ExecutableAllocator.h"
#include "MachineContext.h"
#include "VM.h"
#include "WasmExceptionType.h"
#include "WasmMemory.h"
#include "WasmThunks.h"

#include <wtf/Lock.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/threads/Signals.h>

namespace JSC { namespace Wasm {

namespace {
static const bool verbose = false;
}

static StaticLock codeLocationsLock;
static LazyNeverDestroyed<HashSet<std::tuple<void*, void*>>> codeLocations; // (start, end)

#if ENABLE(WEBASSEMBLY_FAST_MEMORY)

static bool fastHandlerInstalled { false };

static SignalAction trapHandler(Signal, SigInfo& sigInfo, PlatformRegisters& context)
{
    void* faultingInstruction = MachineContext::instructionPointer(context);
    dataLogLnIf(verbose, "starting handler for fault at: ", RawPointer(faultingInstruction));

    dataLogLnIf(verbose, "JIT memory start: ", RawPointer(reinterpret_cast<void*>(startOfFixedExecutableMemoryPool)), " end: ", RawPointer(reinterpret_cast<void*>(endOfFixedExecutableMemoryPool)));
    // First we need to make sure we are in JIT code before we can aquire any locks. Otherwise,
    // we might have crashed in code that is already holding one of the locks we want to aquire.
    if (isJITPC(faultingInstruction)) {
        bool faultedInActiveFastMemory = false;
        {
            void* faultingAddress = sigInfo.faultingAddress;
            dataLogLnIf(verbose, "checking faulting address: ", RawPointer(faultingAddress), " is in an active fast memory");
            faultedInActiveFastMemory = Wasm::Memory::addressIsInActiveFastMemory(faultingAddress);
        }
        if (faultedInActiveFastMemory) {
            dataLogLnIf(verbose, "found active fast memory for faulting address");
            LockHolder locker(codeLocationsLock);
            for (auto range : codeLocations.get()) {
                void* start;
                void* end;
                std::tie(start, end) = range;
                dataLogLnIf(verbose, "function start: ", RawPointer(start), " end: ", RawPointer(end));
                if (start <= faultingInstruction && faultingInstruction < end) {
                    dataLogLnIf(verbose, "found match");
                    MacroAssemblerCodeRef exceptionStub = Thunks::singleton().existingStub(throwExceptionFromWasmThunkGenerator);
                    // If for whatever reason we don't have a stub then we should just treat this like a regular crash.
                    if (!exceptionStub)
                        break;
                    dataLogLnIf(verbose, "found stub: ", RawPointer(exceptionStub.code().executableAddress()));
                    MachineContext::argumentPointer<1>(context) = reinterpret_cast<void*>(ExceptionType::OutOfBoundsMemoryAccess);
                    MachineContext::instructionPointer(context) = exceptionStub.code().executableAddress();
                    return SignalAction::Handled;
                }
            }
        }
    }
    return SignalAction::NotHandled;
}

#endif // ENABLE(WEBASSEMBLY_FAST_MEMORY)

void registerCode(void* start, void* end)
{
    if (!fastMemoryEnabled())
        return;
    LockHolder locker(codeLocationsLock);
    codeLocations->add(std::make_tuple(start, end));
}

void unregisterCode(void* start, void* end)
{
    if (!fastMemoryEnabled())
        return;
    LockHolder locker(codeLocationsLock);
    codeLocations->remove(std::make_tuple(start, end));
}

bool fastMemoryEnabled()
{
    return fastHandlerInstalled;
}

void enableFastMemory()
{
    static std::once_flag once;
    std::call_once(once, [] {
        if (!Options::useWebAssemblyFastMemory())
            return;

#if ENABLE(WEBASSEMBLY_FAST_MEMORY)
        installSignalHandler(Signal::BadAccess, [] (Signal signal, SigInfo& sigInfo, PlatformRegisters& ucontext) {
            return trapHandler(signal, sigInfo, ucontext);
        });

        codeLocations.construct();
        fastHandlerInstalled = true;
#endif // ENABLE(WEBASSEMBLY_FAST_MEMORY)
    });
}
    
} } // namespace JSC::Wasm

#endif // ENABLE(WEBASSEMBLY)
