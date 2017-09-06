/*
 * Copyright (C) 2012-2017 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "SlotVisitor.h"

#include "CPU.h"
#include "ConservativeRoots.h"
#include "GCSegmentedArrayInlines.h"
#include "HeapCellInlines.h"
#include "HeapProfiler.h"
#include "HeapSnapshotBuilder.h"
#include "JSArray.h"
#include "JSDestructibleObject.h"
#include "JSObject.h"
#include "JSString.h"
#include "JSCInlines.h"
#include "SlotVisitorInlines.h"
#include "SuperSampler.h"
#include "VM.h"
#include <wtf/Lock.h>

namespace JSC {

#if ENABLE(GC_VALIDATION)
static void validate(JSCell* cell)
{
    RELEASE_ASSERT(cell);

    if (!cell->structure()) {
        dataLogF("cell at %p has a null structure\n" , cell);
        CRASH();
    }

    // Both the cell's structure, and the cell's structure's structure should be the Structure Structure.
    // I hate this sentence.
    if (cell->structure()->structure()->JSCell::classInfo() != cell->structure()->JSCell::classInfo()) {
        const char* parentClassName = 0;
        const char* ourClassName = 0;
        if (cell->structure()->structure() && cell->structure()->structure()->JSCell::classInfo())
            parentClassName = cell->structure()->structure()->JSCell::classInfo()->className;
        if (cell->structure()->JSCell::classInfo())
            ourClassName = cell->structure()->JSCell::classInfo()->className;
        dataLogF("parent structure (%p <%s>) of cell at %p doesn't match cell's structure (%p <%s>)\n",
            cell->structure()->structure(), parentClassName, cell, cell->structure(), ourClassName);
        CRASH();
    }

    // Make sure we can walk the ClassInfo chain
    const ClassInfo* info = cell->classInfo();
    do { } while ((info = info->parentClass));
}
#endif

SlotVisitor::SlotVisitor(Heap& heap)
    : m_bytesVisited(0)
    , m_visitCount(0)
    , m_isInParallelMode(false)
    , m_markingVersion(MarkedSpace::initialVersion)
    , m_heap(heap)
#if !ASSERT_DISABLED
    , m_isCheckingForDefaultMarkViolation(false)
    , m_isDraining(false)
#endif
{
}

SlotVisitor::~SlotVisitor()
{
    clearMarkStacks();
}

void SlotVisitor::didStartMarking()
{
    if (heap()->collectionScope() == CollectionScope::Full)
        ASSERT(m_opaqueRoots.isEmpty()); // Should have merged by now.
    else
        reset();

    if (HeapProfiler* heapProfiler = vm().heapProfiler())
        m_heapSnapshotBuilder = heapProfiler->activeSnapshotBuilder();
    
    m_markingVersion = heap()->objectSpace().markingVersion();
}

void SlotVisitor::reset()
{
    RELEASE_ASSERT(!m_opaqueRoots.size());
    m_bytesVisited = 0;
    m_visitCount = 0;
    m_heapSnapshotBuilder = nullptr;
    RELEASE_ASSERT(!m_currentCell);
}

void SlotVisitor::clearMarkStacks()
{
    m_collectorStack.clear();
    m_mutatorStack.clear();
}

void SlotVisitor::append(ConservativeRoots& conservativeRoots)
{
    HeapCell** roots = conservativeRoots.roots();
    size_t size = conservativeRoots.size();
    for (size_t i = 0; i < size; ++i)
        appendJSCellOrAuxiliary(roots[i]);
}

void SlotVisitor::appendJSCellOrAuxiliary(HeapCell* heapCell)
{
    if (!heapCell)
        return;
    
    ASSERT(!m_isCheckingForDefaultMarkViolation);
    
    auto validateCell = [&] (JSCell* jsCell) {
        StructureID structureID = jsCell->structureID();
        
        auto die = [&] (const char* text) {
            WTF::dataFile().atomically(
                [&] (PrintStream& out) {
                    out.print(text);
                    out.print("GC type: ", heap()->collectionScope(), "\n");
                    out.print("Object at: ", RawPointer(jsCell), "\n");
#if USE(JSVALUE64)
                    out.print("Structure ID: ", structureID, " (0x", format("%x", structureID), ")\n");
                    out.print("Structure ID table size: ", heap()->structureIDTable().size(), "\n");
#else
                    out.print("Structure: ", RawPointer(structureID), "\n");
#endif
                    out.print("Object contents:");
                    for (unsigned i = 0; i < 2; ++i)
                        out.print(" ", format("0x%016llx", bitwise_cast<uint64_t*>(jsCell)[i]));
                    out.print("\n");
                    CellContainer container = jsCell->cellContainer();
                    out.print("Is marked: ", container.isMarked(jsCell), "\n");
                    out.print("Is newly allocated: ", container.isNewlyAllocated(jsCell), "\n");
                    if (container.isMarkedBlock()) {
                        MarkedBlock& block = container.markedBlock();
                        out.print("Block: ", RawPointer(&block), "\n");
                        block.handle().dumpState(out);
                        out.print("\n");
                        out.print("Is marked raw: ", block.isMarkedRaw(jsCell), "\n");
                        out.print("Marking version: ", block.markingVersion(), "\n");
                        out.print("Heap marking version: ", heap()->objectSpace().markingVersion(), "\n");
                        out.print("Is newly allocated raw: ", block.handle().isNewlyAllocated(jsCell), "\n");
                        out.print("Newly allocated version: ", block.handle().newlyAllocatedVersion(), "\n");
                        out.print("Heap newly allocated version: ", heap()->objectSpace().newlyAllocatedVersion(), "\n");
                    }
                    UNREACHABLE_FOR_PLATFORM();
                });
        };
        
        // It's not OK for the structure to be null at any GC scan point. We must not GC while
        // an object is not fully initialized.
        if (!structureID)
            die("GC scan found corrupt object: structureID is zero!\n");
        
        // It's not OK for the structure to be nuked at any GC scan point.
        if (isNuked(structureID))
            die("GC scan found object in bad state: structureID is nuked!\n");
        
#if USE(JSVALUE64)
        // This detects the worst of the badness.
        if (structureID >= heap()->structureIDTable().size())
            die("GC scan found corrupt object: structureID is out of bounds!\n");
#endif
    };
    
    // In debug mode, we validate before marking since this makes it clearer what the problem
    // was. It's also slower, so we don't do it normally.
    if (!ASSERT_DISABLED && heapCell->cellKind() == HeapCell::JSCell)
        validateCell(static_cast<JSCell*>(heapCell));
    
    if (Heap::testAndSetMarked(m_markingVersion, heapCell))
        return;
    
    switch (heapCell->cellKind()) {
    case HeapCell::JSCell: {
        // We have ample budget to perform validation here.
    
        JSCell* jsCell = static_cast<JSCell*>(heapCell);
        validateCell(jsCell);
        
        jsCell->setCellState(CellState::PossiblyGrey);

        appendToMarkStack(jsCell);
        return;
    }
        
    case HeapCell::Auxiliary: {
        noteLiveAuxiliaryCell(heapCell);
        return;
    } }
}

void SlotVisitor::appendUnbarriered(JSValue value)
{
    if (!value || !value.isCell())
        return;

    if (UNLIKELY(m_heapSnapshotBuilder))
        m_heapSnapshotBuilder->appendEdge(m_currentCell, value.asCell());

    setMarkedAndAppendToMarkStack(value.asCell());
}

void SlotVisitor::appendHidden(JSValue value)
{
    if (!value || !value.isCell())
        return;

    setMarkedAndAppendToMarkStack(value.asCell());
}

void SlotVisitor::setMarkedAndAppendToMarkStack(JSCell* cell)
{
    SuperSamplerScope superSamplerScope(false);
    
    ASSERT(!m_isCheckingForDefaultMarkViolation);
    if (!cell)
        return;

#if ENABLE(GC_VALIDATION)
    validate(cell);
#endif
    
    if (cell->isLargeAllocation())
        setMarkedAndAppendToMarkStack(cell->largeAllocation(), cell);
    else
        setMarkedAndAppendToMarkStack(cell->markedBlock(), cell);
}

template<typename ContainerType>
ALWAYS_INLINE void SlotVisitor::setMarkedAndAppendToMarkStack(ContainerType& container, JSCell* cell)
{
    container.aboutToMark(m_markingVersion);
    
    if (container.testAndSetMarked(cell))
        return;
    
    ASSERT(cell->structure());
    
    // Indicate that the object is grey and that:
    // In case of concurrent GC: it's the first time it is grey in this GC cycle.
    // In case of eden collection: it's a new object that became grey rather than an old remembered object.
    cell->setCellState(CellState::PossiblyGrey);
    
    appendToMarkStack(container, cell);
}

void SlotVisitor::appendToMarkStack(JSCell* cell)
{
    if (cell->isLargeAllocation())
        appendToMarkStack(cell->largeAllocation(), cell);
    else
        appendToMarkStack(cell->markedBlock(), cell);
}

template<typename ContainerType>
ALWAYS_INLINE void SlotVisitor::appendToMarkStack(ContainerType& container, JSCell* cell)
{
    ASSERT(Heap::isMarkedConcurrently(cell));
    ASSERT(!cell->isZapped());
    
    container.noteMarked();
    
    m_visitCount++;
    m_bytesVisited += container.cellSize();
    
    m_collectorStack.append(cell);
}

void SlotVisitor::appendToMutatorMarkStack(const JSCell* cell)
{
    m_mutatorStack.append(cell);
}

void SlotVisitor::markAuxiliary(const void* base)
{
    HeapCell* cell = bitwise_cast<HeapCell*>(base);
    
    ASSERT(cell->heap() == heap());
    
    if (Heap::testAndSetMarked(m_markingVersion, cell))
        return;
    
    noteLiveAuxiliaryCell(cell);
}

void SlotVisitor::noteLiveAuxiliaryCell(HeapCell* cell)
{
    // We get here once per GC under these circumstances:
    //
    // Eden collection: if the cell was allocated since the last collection and is live somehow.
    //
    // Full collection: if the cell is live somehow.
    
    CellContainer container = cell->cellContainer();
    
    container.assertValidCell(vm(), cell);
    container.noteMarked();
    
    m_visitCount++;
    m_bytesVisited += container.cellSize();
}

class SetCurrentCellScope {
public:
    SetCurrentCellScope(SlotVisitor& visitor, const JSCell* cell)
        : m_visitor(visitor)
    {
        ASSERT(!m_visitor.m_currentCell);
        m_visitor.m_currentCell = const_cast<JSCell*>(cell);
    }

    ~SetCurrentCellScope()
    {
        ASSERT(m_visitor.m_currentCell);
        m_visitor.m_currentCell = nullptr;
    }

private:
    SlotVisitor& m_visitor;
};

ALWAYS_INLINE void SlotVisitor::visitChildren(const JSCell* cell)
{
    ASSERT(Heap::isMarkedConcurrently(cell));
    
    SetCurrentCellScope currentCellScope(*this, cell);
    
    if (false) {
        dataLog("Visiting ", RawPointer(cell));
        if (!m_isFirstVisit)
            dataLog(" (subsequent)");
        dataLog("\n");
    }
    
    // Funny story: it's possible for the object to be black already, if we barrier the object at
    // about the same time that it's marked. That's fine. It's a gnarly and super-rare race. It's
    // not clear to me that it would be correct or profitable to bail here if the object is already
    // black.
    
    cell->setCellState(CellState::PossiblyBlack);
    
    WTF::storeLoadFence();
    
    switch (cell->type()) {
    case StringType:
        JSString::visitChildren(const_cast<JSCell*>(cell), *this);
        break;
        
    case FinalObjectType:
        JSFinalObject::visitChildren(const_cast<JSCell*>(cell), *this);
        break;

    case ArrayType:
        JSArray::visitChildren(const_cast<JSCell*>(cell), *this);
        break;
        
    default:
        // FIXME: This could be so much better.
        // https://bugs.webkit.org/show_bug.cgi?id=162462
        cell->methodTable(vm())->visitChildren(const_cast<JSCell*>(cell), *this);
        break;
    }
    
    if (UNLIKELY(m_heapSnapshotBuilder)) {
        if (m_isFirstVisit)
            m_heapSnapshotBuilder->appendNode(const_cast<JSCell*>(cell));
    }
}

void SlotVisitor::visitAsConstraint(const JSCell* cell)
{
    m_isFirstVisit = false;
    visitChildren(cell);
}

void SlotVisitor::donateKnownParallel(MarkStackArray& from, MarkStackArray& to)
{
    // NOTE: Because we re-try often, we can afford to be conservative, and
    // assume that donating is not profitable.

    // Avoid locking when a thread reaches a dead end in the object graph.
    if (from.size() < 2)
        return;

    // If there's already some shared work queued up, be conservative and assume
    // that donating more is not profitable.
    if (to.size())
        return;

    // If we're contending on the lock, be conservative and assume that another
    // thread is already donating.
    std::unique_lock<Lock> lock(m_heap.m_markingMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return;

    // Otherwise, assume that a thread will go idle soon, and donate.
    from.donateSomeCellsTo(to);

    m_heap.m_markingConditionVariable.notifyAll();
}

void SlotVisitor::donateKnownParallel()
{
    donateKnownParallel(m_collectorStack, *m_heap.m_sharedCollectorMarkStack);
    donateKnownParallel(m_mutatorStack, *m_heap.m_sharedMutatorMarkStack);
}

void SlotVisitor::updateMutatorIsStopped(const AbstractLocker&)
{
    m_mutatorIsStopped = (m_heap.collectorBelievesThatTheWorldIsStopped() & m_canOptimizeForStoppedMutator);
}

void SlotVisitor::updateMutatorIsStopped()
{
    if (mutatorIsStoppedIsUpToDate())
        return;
    updateMutatorIsStopped(holdLock(m_rightToRun));
}

bool SlotVisitor::hasAcknowledgedThatTheMutatorIsResumed() const
{
    return !m_mutatorIsStopped;
}

bool SlotVisitor::mutatorIsStoppedIsUpToDate() const
{
    return m_mutatorIsStopped == (m_heap.collectorBelievesThatTheWorldIsStopped() & m_canOptimizeForStoppedMutator);
}

void SlotVisitor::optimizeForStoppedMutator()
{
    m_canOptimizeForStoppedMutator = true;
}

void SlotVisitor::drain(MonotonicTime timeout)
{
    ASSERT(m_isInParallelMode);
    
    auto locker = holdLock(m_rightToRun);
    
    while (!hasElapsed(timeout)) {
        updateMutatorIsStopped(locker);
        if (!m_collectorStack.isEmpty()) {
            m_collectorStack.refill();
            m_isFirstVisit = true;
            for (unsigned countdown = Options::minimumNumberOfScansBetweenRebalance(); m_collectorStack.canRemoveLast() && countdown--;)
                visitChildren(m_collectorStack.removeLast());
        } else if (!m_mutatorStack.isEmpty()) {
            m_mutatorStack.refill();
            // We know for sure that we are visiting objects because of the barrier, not because of
            // marking. Marking will visit an object exactly once. The barrier will visit it
            // possibly many times, and always after it was already marked.
            m_isFirstVisit = false;
            for (unsigned countdown = Options::minimumNumberOfScansBetweenRebalance(); m_mutatorStack.canRemoveLast() && countdown--;)
                visitChildren(m_mutatorStack.removeLast());
        } else
            break;
        m_rightToRun.safepoint();
        donateKnownParallel();
    }
    
    mergeIfNecessary();
}

bool SlotVisitor::didReachTermination()
{
    LockHolder locker(m_heap.m_markingMutex);
    return didReachTermination(locker);
}

bool SlotVisitor::didReachTermination(const LockHolder&)
{
    return isEmpty()
        && !m_heap.m_numberOfActiveParallelMarkers
        && m_heap.m_sharedCollectorMarkStack->isEmpty()
        && m_heap.m_sharedMutatorMarkStack->isEmpty();
}

bool SlotVisitor::hasWork(const LockHolder&)
{
    return !m_heap.m_sharedCollectorMarkStack->isEmpty()
        || !m_heap.m_sharedMutatorMarkStack->isEmpty();
}

SlotVisitor::SharedDrainResult SlotVisitor::drainFromShared(SharedDrainMode sharedDrainMode, MonotonicTime timeout)
{
    ASSERT(m_isInParallelMode);
    
    ASSERT(Options::numberOfGCMarkers());
    
    bool isActive = false;
    while (true) {
        {
            LockHolder locker(m_heap.m_markingMutex);
            if (isActive)
                m_heap.m_numberOfActiveParallelMarkers--;
            m_heap.m_numberOfWaitingParallelMarkers++;

            if (sharedDrainMode == MasterDrain) {
                while (true) {
                    if (hasElapsed(timeout))
                        return SharedDrainResult::TimedOut;
                    
                    if (didReachTermination(locker)) {
                        m_heap.m_markingConditionVariable.notifyAll();
                        return SharedDrainResult::Done;
                    }
                    
                    if (hasWork(locker))
                        break;
                    
                    m_heap.m_markingConditionVariable.waitUntil(m_heap.m_markingMutex, timeout);
                }
            } else {
                ASSERT(sharedDrainMode == SlaveDrain);

                if (hasElapsed(timeout))
                    return SharedDrainResult::TimedOut;
                
                if (didReachTermination(locker))
                    m_heap.m_markingConditionVariable.notifyAll();

                m_heap.m_markingConditionVariable.waitUntil(
                    m_heap.m_markingMutex, timeout,
                    [&] {
                        return hasWork(locker)
                            || m_heap.m_parallelMarkersShouldExit;
                    });
                
                if (m_heap.m_parallelMarkersShouldExit)
                    return SharedDrainResult::Done;
            }

            m_collectorStack.stealSomeCellsFrom(
                *m_heap.m_sharedCollectorMarkStack, m_heap.m_numberOfWaitingParallelMarkers);
            m_mutatorStack.stealSomeCellsFrom(
                *m_heap.m_sharedMutatorMarkStack, m_heap.m_numberOfWaitingParallelMarkers);
            m_heap.m_numberOfActiveParallelMarkers++;
            m_heap.m_numberOfWaitingParallelMarkers--;
        }
        
        drain(timeout);
        isActive = true;
    }
}

SlotVisitor::SharedDrainResult SlotVisitor::drainInParallel(MonotonicTime timeout)
{
    donateAndDrain(timeout);
    return drainFromShared(MasterDrain, timeout);
}

SlotVisitor::SharedDrainResult SlotVisitor::drainInParallelPassively(MonotonicTime timeout)
{
    ASSERT(m_isInParallelMode);
    
    ASSERT(Options::numberOfGCMarkers());
    
    if (Options::numberOfGCMarkers() < 4
        || !m_heap.hasHeapAccess()
        || m_heap.collectorBelievesThatTheWorldIsStopped()) {
        // This is an optimization over drainInParallel() when we have a concurrent mutator but
        // otherwise it is not profitable.
        return drainInParallel(timeout);
    }

    LockHolder locker(m_heap.m_markingMutex);
    m_collectorStack.transferTo(*m_heap.m_sharedCollectorMarkStack);
    m_mutatorStack.transferTo(*m_heap.m_sharedMutatorMarkStack);
    m_heap.m_markingConditionVariable.notifyAll();
    
    for (;;) {
        if (hasElapsed(timeout))
            return SharedDrainResult::TimedOut;
        
        if (didReachTermination(locker)) {
            m_heap.m_markingConditionVariable.notifyAll();
            return SharedDrainResult::Done;
        }
        
        m_heap.m_markingConditionVariable.waitUntil(m_heap.m_markingMutex, timeout);
    }
}

void SlotVisitor::addOpaqueRoot(void* root)
{
    if (!root)
        return;
    
    if (m_ignoreNewOpaqueRoots)
        return;
    
    if (Options::numberOfGCMarkers() == 1) {
        // Put directly into the shared HashSet.
        m_heap.m_opaqueRoots.add(root);
        return;
    }
    // Put into the local set, but merge with the shared one every once in
    // a while to make sure that the local sets don't grow too large.
    mergeOpaqueRootsIfProfitable();
    m_opaqueRoots.add(root);
}

bool SlotVisitor::containsOpaqueRoot(void* root) const
{
    if (!root)
        return false;
    
    ASSERT(!m_isInParallelMode);
    return m_heap.m_opaqueRoots.contains(root);
}

TriState SlotVisitor::containsOpaqueRootTriState(void* root) const
{
    if (!root)
        return FalseTriState;
    
    if (m_opaqueRoots.contains(root))
        return TrueTriState;
    std::lock_guard<Lock> lock(m_heap.m_opaqueRootsMutex);
    if (m_heap.m_opaqueRoots.contains(root))
        return TrueTriState;
    return MixedTriState;
}

void SlotVisitor::mergeIfNecessary()
{
    if (m_opaqueRoots.isEmpty())
        return;
    mergeOpaqueRoots();
}

void SlotVisitor::mergeOpaqueRootsIfProfitable()
{
    if (static_cast<unsigned>(m_opaqueRoots.size()) < Options::opaqueRootMergeThreshold())
        return;
    mergeOpaqueRoots();
}
    
void SlotVisitor::donate()
{
    ASSERT(m_isInParallelMode);
    if (Options::numberOfGCMarkers() == 1)
        return;
    
    donateKnownParallel();
}

void SlotVisitor::donateAndDrain(MonotonicTime timeout)
{
    donate();
    drain(timeout);
}

void SlotVisitor::mergeOpaqueRoots()
{
    {
        std::lock_guard<Lock> lock(m_heap.m_opaqueRootsMutex);
        for (auto* root : m_opaqueRoots)
            m_heap.m_opaqueRoots.add(root);
    }
    m_opaqueRoots.clear();
}

void SlotVisitor::addWeakReferenceHarvester(WeakReferenceHarvester* weakReferenceHarvester)
{
    m_heap.m_weakReferenceHarvesters.addThreadSafe(weakReferenceHarvester);
}

void SlotVisitor::addUnconditionalFinalizer(UnconditionalFinalizer* unconditionalFinalizer)
{
    m_heap.m_unconditionalFinalizers.addThreadSafe(unconditionalFinalizer);
}

void SlotVisitor::didRace(const VisitRaceKey& race)
{
    if (Options::verboseVisitRace())
        dataLog(toCString("GC visit race: ", race, "\n"));
    
    auto locker = holdLock(heap()->m_raceMarkStackLock);
    JSCell* cell = race.cell();
    cell->setCellState(CellState::PossiblyGrey);
    heap()->m_raceMarkStack->append(cell);
}

void SlotVisitor::dump(PrintStream& out) const
{
    out.print("Collector: [", pointerListDump(collectorMarkStack()), "], Mutator: [", pointerListDump(mutatorMarkStack()), "]");
}

} // namespace JSC
