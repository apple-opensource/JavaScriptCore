/*
 *  Copyright (C) 2003-2017 Apple Inc. All rights reserved.
 *  Copyright (C) 2007 Eric Seidel <eric@webkit.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "config.h"
#include "Heap.h"

#include "CodeBlock.h"
#include "CodeBlockSetInlines.h"
#include "ConservativeRoots.h"
#include "DFGWorklistInlines.h"
#include "EdenGCActivityCallback.h"
#include "Exception.h"
#include "FullGCActivityCallback.h"
#include "GCActivityCallback.h"
#include "GCIncomingRefCountedSetInlines.h"
#include "GCSegmentedArrayInlines.h"
#include "GCTypeMap.h"
#include "HasOwnPropertyCache.h"
#include "HeapHelperPool.h"
#include "HeapIterationScope.h"
#include "HeapProfiler.h"
#include "HeapSnapshot.h"
#include "HeapStatistics.h"
#include "HeapVerifier.h"
#include "HelpingGCScope.h"
#include "IncrementalSweeper.h"
#include "Interpreter.h"
#include "JITStubRoutineSet.h"
#include "JITWorklist.h"
#include "JSCInlines.h"
#include "JSGlobalObject.h"
#include "JSLock.h"
#include "JSVirtualMachineInternal.h"
#include "MarkedSpaceInlines.h"
#include "MarkingConstraintSet.h"
#include "PreventCollectionScope.h"
#include "SamplingProfiler.h"
#include "ShadowChicken.h"
#include "SpaceTimeMutatorScheduler.h"
#include "SuperSampler.h"
#include "StochasticSpaceTimeMutatorScheduler.h"
#include "StopIfNecessaryTimer.h"
#include "SynchronousStopTheWorldMutatorScheduler.h"
#include "TypeProfilerLog.h"
#include "UnlinkedCodeBlock.h"
#include "VM.h"
#include "WeakSetInlines.h"
#include <algorithm>
#include <wtf/CurrentTime.h>
#include <wtf/MainThread.h>
#include <wtf/ParallelVectorIterator.h>
#include <wtf/ProcessID.h>
#include <wtf/RAMSize.h>
#include <wtf/SimpleStats.h>

#if USE(FOUNDATION)
#if __has_include(<objc/objc-internal.h>)
#include <objc/objc-internal.h>
#else
extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void *context);
#endif
#endif // USE(FOUNDATION)

using namespace std;

namespace JSC {

namespace {

bool verboseStop = false;

double maxPauseMS(double thisPauseMS)
{
    static double maxPauseMS;
    maxPauseMS = std::max(thisPauseMS, maxPauseMS);
    return maxPauseMS;
}

size_t minHeapSize(HeapType heapType, size_t ramSize)
{
    if (heapType == LargeHeap) {
        double result = min(
            static_cast<double>(Options::largeHeapSize()),
            ramSize * Options::smallHeapRAMFraction());
        return static_cast<size_t>(result);
    }
    return Options::smallHeapSize();
}

size_t proportionalHeapSize(size_t heapSize, size_t ramSize)
{
    if (heapSize < ramSize * Options::smallHeapRAMFraction())
        return Options::smallHeapGrowthFactor() * heapSize;
    if (heapSize < ramSize * Options::mediumHeapRAMFraction())
        return Options::mediumHeapGrowthFactor() * heapSize;
    return Options::largeHeapGrowthFactor() * heapSize;
}

bool isValidSharedInstanceThreadState(VM* vm)
{
    return vm->currentThreadIsHoldingAPILock();
}

bool isValidThreadState(VM* vm)
{
    if (vm->atomicStringTable() != wtfThreadData().atomicStringTable())
        return false;

    if (vm->isSharedInstance() && !isValidSharedInstanceThreadState(vm))
        return false;

    return true;
}

void recordType(TypeCountSet& set, JSCell* cell)
{
    const char* typeName = "[unknown]";
    const ClassInfo* info = cell->classInfo();
    if (info && info->className)
        typeName = info->className;
    set.add(typeName);
}

bool measurePhaseTiming()
{
    return false;
}

HashMap<const char*, GCTypeMap<SimpleStats>>& timingStats()
{
    static HashMap<const char*, GCTypeMap<SimpleStats>>* result;
    static std::once_flag once;
    std::call_once(
        once,
        [] {
            result = new HashMap<const char*, GCTypeMap<SimpleStats>>();
        });
    return *result;
}

SimpleStats& timingStats(const char* name, CollectionScope scope)
{
    return timingStats().add(name, GCTypeMap<SimpleStats>()).iterator->value[scope];
}

class TimingScope {
public:
    TimingScope(std::optional<CollectionScope> scope, const char* name)
        : m_scope(scope)
        , m_name(name)
    {
        if (measurePhaseTiming())
            m_before = monotonicallyIncreasingTimeMS();
    }
    
    TimingScope(Heap& heap, const char* name)
        : TimingScope(heap.collectionScope(), name)
    {
    }
    
    void setScope(std::optional<CollectionScope> scope)
    {
        m_scope = scope;
    }
    
    void setScope(Heap& heap)
    {
        setScope(heap.collectionScope());
    }
    
    ~TimingScope()
    {
        if (measurePhaseTiming()) {
            double after = monotonicallyIncreasingTimeMS();
            double timing = after - m_before;
            SimpleStats& stats = timingStats(m_name, *m_scope);
            stats.add(timing);
            dataLog("[GC:", *m_scope, "] ", m_name, " took: ", timing, "ms (average ", stats.mean(), "ms).\n");
        }
    }
private:
    std::optional<CollectionScope> m_scope;
    double m_before;
    const char* m_name;
};

} // anonymous namespace

class Heap::Thread : public AutomaticThread {
public:
    Thread(const LockHolder& locker, Heap& heap)
        : AutomaticThread(locker, heap.m_threadLock, heap.m_threadCondition)
        , m_heap(heap)
    {
    }
    
protected:
    PollResult poll(const LockHolder& locker) override
    {
        if (m_heap.m_threadShouldStop) {
            m_heap.notifyThreadStopping(locker);
            return PollResult::Stop;
        }
        if (m_heap.shouldCollectInThread(locker))
            return PollResult::Work;
        return PollResult::Wait;
    }
    
    WorkResult work() override
    {
        m_heap.collectInThread();
        return WorkResult::Continue;
    }
    
    void threadDidStart() override
    {
        WTF::registerGCThread(GCThreadType::Main);
    }

private:
    Heap& m_heap;
};

Heap::Heap(VM* vm, HeapType heapType)
    : m_heapType(heapType)
    , m_ramSize(Options::forceRAMSize() ? Options::forceRAMSize() : ramSize())
    , m_minBytesPerCycle(minHeapSize(m_heapType, m_ramSize))
    , m_sizeAfterLastCollect(0)
    , m_sizeAfterLastFullCollect(0)
    , m_sizeBeforeLastFullCollect(0)
    , m_sizeAfterLastEdenCollect(0)
    , m_sizeBeforeLastEdenCollect(0)
    , m_bytesAllocatedThisCycle(0)
    , m_bytesAbandonedSinceLastFullCollect(0)
    , m_maxEdenSize(m_minBytesPerCycle)
    , m_maxHeapSize(m_minBytesPerCycle)
    , m_shouldDoFullCollection(false)
    , m_totalBytesVisited(0)
    , m_objectSpace(this)
    , m_extraMemorySize(0)
    , m_deprecatedExtraMemorySize(0)
    , m_machineThreads(this)
    , m_collectorSlotVisitor(std::make_unique<SlotVisitor>(*this))
    , m_mutatorMarkStack(std::make_unique<MarkStackArray>())
    , m_raceMarkStack(std::make_unique<MarkStackArray>())
    , m_constraintSet(std::make_unique<MarkingConstraintSet>())
    , m_handleSet(vm)
    , m_codeBlocks(std::make_unique<CodeBlockSet>())
    , m_jitStubRoutines(std::make_unique<JITStubRoutineSet>())
    , m_isSafeToCollect(false)
    , m_vm(vm)
    // We seed with 10ms so that GCActivityCallback::didAllocate doesn't continuously 
    // schedule the timer if we've never done a collection.
    , m_lastFullGCLength(0.01)
    , m_lastEdenGCLength(0.01)
#if USE(CF)
    , m_runLoop(CFRunLoopGetCurrent())
#endif // USE(CF)
    , m_fullActivityCallback(GCActivityCallback::createFullTimer(this))
    , m_edenActivityCallback(GCActivityCallback::createEdenTimer(this))
    , m_sweeper(adoptRef(new IncrementalSweeper(this)))
    , m_stopIfNecessaryTimer(adoptRef(new StopIfNecessaryTimer(vm)))
    , m_deferralDepth(0)
#if USE(FOUNDATION)
    , m_delayedReleaseRecursionCount(0)
#endif
    , m_sharedCollectorMarkStack(std::make_unique<MarkStackArray>())
    , m_sharedMutatorMarkStack(std::make_unique<MarkStackArray>())
    , m_helperClient(&heapHelperPool())
    , m_threadLock(Box<Lock>::create())
    , m_threadCondition(AutomaticThreadCondition::create())
{
    m_worldState.store(0);
    
    if (Options::useConcurrentGC()) {
        if (Options::useStochasticMutatorScheduler())
            m_scheduler = std::make_unique<StochasticSpaceTimeMutatorScheduler>(*this);
        else
            m_scheduler = std::make_unique<SpaceTimeMutatorScheduler>(*this);
    } else {
        // We simulate turning off concurrent GC by making the scheduler say that the world
        // should always be stopped when the collector is running.
        m_scheduler = std::make_unique<SynchronousStopTheWorldMutatorScheduler>();
    }
    
    if (Options::verifyHeap())
        m_verifier = std::make_unique<HeapVerifier>(this, Options::numberOfGCCyclesToRecordForVerification());
    
    m_collectorSlotVisitor->optimizeForStoppedMutator();
    
    LockHolder locker(*m_threadLock);
    m_thread = adoptRef(new Thread(locker, *this));
}

Heap::~Heap()
{
    for (auto& slotVisitor : m_parallelSlotVisitors)
        slotVisitor->clearMarkStacks();
    m_collectorSlotVisitor->clearMarkStacks();
    m_mutatorMarkStack->clear();
    m_raceMarkStack->clear();
    
    for (WeakBlock* block : m_logicallyEmptyWeakBlocks)
        WeakBlock::destroy(*this, block);
}

bool Heap::isPagedOut(double deadline)
{
    return m_objectSpace.isPagedOut(deadline);
}

// The VM is being destroyed and the collector will never run again.
// Run all pending finalizers now because we won't get another chance.
void Heap::lastChanceToFinalize()
{
    RELEASE_ASSERT(!m_vm->entryScope);
    RELEASE_ASSERT(m_mutatorState == MutatorState::Running);
    
    if (m_collectContinuouslyThread) {
        {
            LockHolder locker(m_collectContinuouslyLock);
            m_shouldStopCollectingContinuously = true;
            m_collectContinuouslyCondition.notifyOne();
        }
        waitForThreadCompletion(m_collectContinuouslyThread);
    }
    
    // Carefully bring the thread down. We need to use waitForCollector() until we know that there
    // won't be any other collections.
    bool stopped = false;
    {
        LockHolder locker(*m_threadLock);
        stopped = m_thread->tryStop(locker);
        if (!stopped) {
            m_threadShouldStop = true;
            m_threadCondition->notifyOne(locker);
        }
    }
    if (!stopped) {
        waitForCollector(
            [&] (const LockHolder&) -> bool {
                return m_threadIsStopping;
            });
        // It's now safe to join the thread, since we know that there will not be any more collections.
        m_thread->join();
    }
    
    m_arrayBuffers.lastChanceToFinalize();
    m_codeBlocks->lastChanceToFinalize();
    m_objectSpace.stopAllocating();
    m_objectSpace.lastChanceToFinalize();
    releaseDelayedReleasedObjects();

    sweepAllLogicallyEmptyWeakBlocks();
}

void Heap::releaseDelayedReleasedObjects()
{
#if USE(FOUNDATION)
    // We need to guard against the case that releasing an object can create more objects due to the
    // release calling into JS. When those JS call(s) exit and all locks are being dropped we end up
    // back here and could try to recursively release objects. We guard that with a recursive entry
    // count. Only the initial call will release objects, recursive calls simple return and let the
    // the initial call to the function take care of any objects created during release time.
    // This also means that we need to loop until there are no objects in m_delayedReleaseObjects
    // and use a temp Vector for the actual releasing.
    if (!m_delayedReleaseRecursionCount++) {
        while (!m_delayedReleaseObjects.isEmpty()) {
            ASSERT(m_vm->currentThreadIsHoldingAPILock());

            Vector<RetainPtr<CFTypeRef>> objectsToRelease = WTFMove(m_delayedReleaseObjects);

            {
                // We need to drop locks before calling out to arbitrary code.
                JSLock::DropAllLocks dropAllLocks(m_vm);

                void* context = objc_autoreleasePoolPush();
                objectsToRelease.clear();
                objc_autoreleasePoolPop(context);
            }
        }
    }
    m_delayedReleaseRecursionCount--;
#endif
}

void Heap::reportExtraMemoryAllocatedSlowCase(size_t size)
{
    didAllocate(size);
    collectIfNecessaryOrDefer();
}

void Heap::deprecatedReportExtraMemorySlowCase(size_t size)
{
    m_deprecatedExtraMemorySize += size;
    reportExtraMemoryAllocatedSlowCase(size);
}

void Heap::reportAbandonedObjectGraph()
{
    // Our clients don't know exactly how much memory they
    // are abandoning so we just guess for them.
    size_t abandonedBytes = static_cast<size_t>(0.1 * capacity());

    // We want to accelerate the next collection. Because memory has just 
    // been abandoned, the next collection has the potential to 
    // be more profitable. Since allocation is the trigger for collection, 
    // we hasten the next collection by pretending that we've allocated more memory. 
    if (m_fullActivityCallback) {
        m_fullActivityCallback->didAllocate(
            m_sizeAfterLastCollect - m_sizeAfterLastFullCollect + m_bytesAllocatedThisCycle + m_bytesAbandonedSinceLastFullCollect);
    }
    m_bytesAbandonedSinceLastFullCollect += abandonedBytes;
}

void Heap::protect(JSValue k)
{
    ASSERT(k);
    ASSERT(m_vm->currentThreadIsHoldingAPILock());

    if (!k.isCell())
        return;

    m_protectedValues.add(k.asCell());
}

bool Heap::unprotect(JSValue k)
{
    ASSERT(k);
    ASSERT(m_vm->currentThreadIsHoldingAPILock());

    if (!k.isCell())
        return false;

    return m_protectedValues.remove(k.asCell());
}

void Heap::addReference(JSCell* cell, ArrayBuffer* buffer)
{
    if (m_arrayBuffers.addReference(cell, buffer)) {
        collectIfNecessaryOrDefer();
        didAllocate(buffer->gcSizeEstimateInBytes());
    }
}

void Heap::finalizeUnconditionalFinalizers()
{
    while (m_unconditionalFinalizers.hasNext()) {
        UnconditionalFinalizer* finalizer = m_unconditionalFinalizers.removeNext();
        finalizer->finalizeUnconditionally();
    }
}

void Heap::willStartIterating()
{
    m_objectSpace.willStartIterating();
}

void Heap::didFinishIterating()
{
    m_objectSpace.didFinishIterating();
}

void Heap::completeAllJITPlans()
{
#if ENABLE(JIT)
    JITWorklist::instance()->completeAllForVM(*m_vm);
#endif // ENABLE(JIT)
    DFG::completeAllPlansForVM(*m_vm);
}

template<typename Func>
void Heap::iterateExecutingAndCompilingCodeBlocks(const Func& func)
{
    m_codeBlocks->iterateCurrentlyExecuting(func);
    DFG::iterateCodeBlocksForGC(*m_vm, func);
}

template<typename Func>
void Heap::iterateExecutingAndCompilingCodeBlocksWithoutHoldingLocks(const Func& func)
{
    Vector<CodeBlock*, 256> codeBlocks;
    iterateExecutingAndCompilingCodeBlocks(
        [&] (CodeBlock* codeBlock) {
            codeBlocks.append(codeBlock);
        });
    for (CodeBlock* codeBlock : codeBlocks)
        func(codeBlock);
}

void Heap::assertSharedMarkStacksEmpty()
{
    bool ok = true;
    
    if (!m_sharedCollectorMarkStack->isEmpty()) {
        dataLog("FATAL: Shared collector mark stack not empty! It has ", m_sharedCollectorMarkStack->size(), " elements.\n");
        ok = false;
    }
    
    if (!m_sharedMutatorMarkStack->isEmpty()) {
        dataLog("FATAL: Shared mutator mark stack not empty! It has ", m_sharedMutatorMarkStack->size(), " elements.\n");
        ok = false;
    }
    
    RELEASE_ASSERT(ok);
}

void Heap::markToFixpoint(double gcStartTime)
{
    TimingScope markToFixpointTimingScope(*this, "Heap::markToFixpoint");
    
    if (m_collectionScope == CollectionScope::Full) {
        m_opaqueRoots.clear();
        m_collectorSlotVisitor->clearMarkStacks();
        m_mutatorMarkStack->clear();
    }

    RELEASE_ASSERT(m_raceMarkStack->isEmpty());

    beginMarking();

    m_parallelMarkersShouldExit = false;

    m_helperClient.setFunction(
        [this] () {
            SlotVisitor* slotVisitor;
            {
                LockHolder locker(m_parallelSlotVisitorLock);
                if (m_availableParallelSlotVisitors.isEmpty()) {
                    std::unique_ptr<SlotVisitor> newVisitor =
                        std::make_unique<SlotVisitor>(*this);
                    
                    if (Options::optimizeParallelSlotVisitorsForStoppedMutator())
                        newVisitor->optimizeForStoppedMutator();
                    
                    slotVisitor = newVisitor.get();
                    m_parallelSlotVisitors.append(WTFMove(newVisitor));
                } else
                    slotVisitor = m_availableParallelSlotVisitors.takeLast();
            }

            WTF::registerGCThread(GCThreadType::Helper);

            {
                ParallelModeEnabler parallelModeEnabler(*slotVisitor);
                slotVisitor->didStartMarking();
                slotVisitor->drainFromShared(SlotVisitor::SlaveDrain);
            }

            {
                LockHolder locker(m_parallelSlotVisitorLock);
                m_availableParallelSlotVisitors.append(slotVisitor);
            }
        });

    SlotVisitor& slotVisitor = *m_collectorSlotVisitor;
    slotVisitor.didStartMarking();
    m_constraintSet->didStartMarking();
    
    m_scheduler->beginCollection();
    if (Options::logGC())
        m_scheduler->log();
    
    // After this, we will almost certainly fall through all of the "slotVisitor.isEmpty()"
    // checks because bootstrap would have put things into the visitor. So, we should fall
    // through to draining.
    
    if (!slotVisitor.didReachTermination()) {
        dataLog("Fatal: SlotVisitor should think that GC should terminate before constraint solving, but it does not think this.\n");
        dataLog("slotVisitor.isEmpty(): ", slotVisitor.isEmpty(), "\n");
        dataLog("slotVisitor.collectorMarkStack().isEmpty(): ", slotVisitor.collectorMarkStack().isEmpty(), "\n");
        dataLog("slotVisitor.mutatorMarkStack().isEmpty(): ", slotVisitor.mutatorMarkStack().isEmpty(), "\n");
        dataLog("m_numberOfActiveParallelMarkers: ", m_numberOfActiveParallelMarkers, "\n");
        dataLog("m_sharedCollectorMarkStack->isEmpty(): ", m_sharedCollectorMarkStack->isEmpty(), "\n");
        dataLog("m_sharedMutatorMarkStack->isEmpty(): ", m_sharedMutatorMarkStack->isEmpty(), "\n");
        dataLog("slotVisitor.didReachTermination(): ", slotVisitor.didReachTermination(), "\n");
        RELEASE_ASSERT_NOT_REACHED();
    }
    
    for (;;) {
        if (Options::logGC())
            dataLog("v=", bytesVisited() / 1024, "kb o=", m_opaqueRoots.size(), " b=", m_barriersExecuted, " ");
        
        if (slotVisitor.didReachTermination()) {
            m_scheduler->didReachTermination();
            
            assertSharedMarkStacksEmpty();
            
            slotVisitor.mergeIfNecessary();
            for (auto& parallelVisitor : m_parallelSlotVisitors)
                parallelVisitor->mergeIfNecessary();
            
            // FIXME: Take m_mutatorDidRun into account when scheduling constraints. Most likely,
            // we don't have to execute root constraints again unless the mutator did run. At a
            // minimum, we could use this for work estimates - but it's probably more than just an
            // estimate.
            // https://bugs.webkit.org/show_bug.cgi?id=166828
            
            // FIXME: We should take advantage of the fact that we could timeout. This only comes
            // into play if we're executing constraints for the first time. But that will matter
            // when we have deep stacks or a lot of DOM stuff.
            // https://bugs.webkit.org/show_bug.cgi?id=166831
            
            // Wondering what this does? Look at Heap::addCoreConstraints(). The DOM and others can also
            // add their own using Heap::addMarkingConstraint().
            bool converged =
                m_constraintSet->executeConvergence(slotVisitor, MonotonicTime::infinity());
            if (converged && slotVisitor.isEmpty()) {
                assertSharedMarkStacksEmpty();
                break;
            }
            
            m_scheduler->didExecuteConstraints();
        }
        
        if (Options::logGC())
            dataLog(slotVisitor.collectorMarkStack().size(), "+", m_mutatorMarkStack->size() + slotVisitor.mutatorMarkStack().size(), " ");
        
        {
            ParallelModeEnabler enabler(slotVisitor);
            slotVisitor.drainInParallel(m_scheduler->timeToResume());
        }
        
        m_scheduler->synchronousDrainingDidStall();

        if (slotVisitor.didReachTermination())
            continue;
        
        if (!m_scheduler->shouldResume())
            continue;
        
        m_scheduler->willResume();
        
        if (Options::logGC()) {
            double thisPauseMS = (MonotonicTime::now() - m_stopTime).milliseconds();
            dataLog("p=", thisPauseMS, "ms (max ", maxPauseMS(thisPauseMS), ")...]\n");
        }
        
        resumeTheWorld();
        
        {
            ParallelModeEnabler enabler(slotVisitor);
            slotVisitor.drainInParallelPassively(m_scheduler->timeToStop());
        }

        stopTheWorld();
        
        if (Options::logGC())
            dataLog("[GC: ");
        
        m_scheduler->didStop();
        
        if (Options::logGC())
            m_scheduler->log();
    }
    
    m_scheduler->endCollection();

    {
        std::lock_guard<Lock> lock(m_markingMutex);
        m_parallelMarkersShouldExit = true;
        m_markingConditionVariable.notifyAll();
    }
    m_helperClient.finish();

    iterateExecutingAndCompilingCodeBlocks(
        [&] (CodeBlock* codeBlock) {
            writeBarrier(codeBlock);
        });
        
    updateObjectCounts(gcStartTime);
    endMarking();
}

void Heap::gatherStackRoots(ConservativeRoots& roots)
{
    m_machineThreads.gatherConservativeRoots(roots, *m_jitStubRoutines, *m_codeBlocks);
}

void Heap::gatherJSStackRoots(ConservativeRoots& roots)
{
#if !ENABLE(JIT)
    m_vm->interpreter->cloopStack().gatherConservativeRoots(roots, *m_jitStubRoutines, *m_codeBlocks);
#else
    UNUSED_PARAM(roots);
#endif
}

void Heap::gatherScratchBufferRoots(ConservativeRoots& roots)
{
#if ENABLE(DFG_JIT)
    m_vm->gatherConservativeRoots(roots);
#else
    UNUSED_PARAM(roots);
#endif
}

void Heap::beginMarking()
{
    TimingScope timingScope(*this, "Heap::beginMarking");
    if (m_collectionScope == CollectionScope::Full)
        m_codeBlocks->clearMarksForFullCollection();
    m_jitStubRoutines->clearMarks();
    m_objectSpace.beginMarking();
    setMutatorShouldBeFenced(true);
}

void Heap::removeDeadCompilerWorklistEntries()
{
#if ENABLE(DFG_JIT)
    for (unsigned i = DFG::numberOfWorklists(); i--;)
        DFG::existingWorklistForIndex(i).removeDeadPlans(*m_vm);
#endif
}

bool Heap::isHeapSnapshotting() const
{
    HeapProfiler* heapProfiler = m_vm->heapProfiler();
    if (UNLIKELY(heapProfiler))
        return heapProfiler->activeSnapshotBuilder();
    return false;
}

struct GatherHeapSnapshotData : MarkedBlock::CountFunctor {
    GatherHeapSnapshotData(HeapSnapshotBuilder& builder)
        : m_builder(builder)
    {
    }

    IterationStatus operator()(HeapCell* heapCell, HeapCell::Kind kind) const
    {
        if (kind == HeapCell::JSCell) {
            JSCell* cell = static_cast<JSCell*>(heapCell);
            cell->methodTable()->heapSnapshot(cell, m_builder);
        }
        return IterationStatus::Continue;
    }

    HeapSnapshotBuilder& m_builder;
};

void Heap::gatherExtraHeapSnapshotData(HeapProfiler& heapProfiler)
{
    if (HeapSnapshotBuilder* builder = heapProfiler.activeSnapshotBuilder()) {
        HeapIterationScope heapIterationScope(*this);
        GatherHeapSnapshotData functor(*builder);
        m_objectSpace.forEachLiveCell(heapIterationScope, functor);
    }
}

struct RemoveDeadHeapSnapshotNodes : MarkedBlock::CountFunctor {
    RemoveDeadHeapSnapshotNodes(HeapSnapshot& snapshot)
        : m_snapshot(snapshot)
    {
    }

    IterationStatus operator()(HeapCell* cell, HeapCell::Kind kind) const
    {
        if (kind == HeapCell::JSCell)
            m_snapshot.sweepCell(static_cast<JSCell*>(cell));
        return IterationStatus::Continue;
    }

    HeapSnapshot& m_snapshot;
};

void Heap::removeDeadHeapSnapshotNodes(HeapProfiler& heapProfiler)
{
    if (HeapSnapshot* snapshot = heapProfiler.mostRecentSnapshot()) {
        HeapIterationScope heapIterationScope(*this);
        RemoveDeadHeapSnapshotNodes functor(*snapshot);
        m_objectSpace.forEachDeadCell(heapIterationScope, functor);
        snapshot->shrinkToFit();
    }
}

void Heap::updateObjectCounts(double gcStartTime)
{
    if (Options::logGC() == GCLogging::Verbose) {
        size_t visitCount = m_collectorSlotVisitor->visitCount();
        visitCount += threadVisitCount();
        dataLogF("\nNumber of live Objects after GC %lu, took %.6f secs\n", static_cast<unsigned long>(visitCount), WTF::monotonicallyIncreasingTime() - gcStartTime);
    }
    
    if (m_collectionScope == CollectionScope::Full)
        m_totalBytesVisited = 0;

    m_totalBytesVisitedThisCycle = bytesVisited();
    
    m_totalBytesVisited += m_totalBytesVisitedThisCycle;
}

void Heap::endMarking()
{
    m_collectorSlotVisitor->reset();

    for (auto& parallelVisitor : m_parallelSlotVisitors)
        parallelVisitor->reset();

    assertSharedMarkStacksEmpty();
    m_weakReferenceHarvesters.removeAll();

    RELEASE_ASSERT(m_raceMarkStack->isEmpty());
    
    m_objectSpace.endMarking();
    setMutatorShouldBeFenced(Options::forceFencedBarrier());
}

size_t Heap::objectCount()
{
    return m_objectSpace.objectCount();
}

size_t Heap::extraMemorySize()
{
    return m_extraMemorySize + m_deprecatedExtraMemorySize + m_arrayBuffers.size();
}

size_t Heap::size()
{
    return m_objectSpace.size() + extraMemorySize();
}

size_t Heap::capacity()
{
    return m_objectSpace.capacity() + extraMemorySize();
}

size_t Heap::protectedGlobalObjectCount()
{
    size_t result = 0;
    forEachProtectedCell(
        [&] (JSCell* cell) {
            if (cell->isObject() && asObject(cell)->isGlobalObject())
                result++;
        });
    return result;
}

size_t Heap::globalObjectCount()
{
    HeapIterationScope iterationScope(*this);
    size_t result = 0;
    m_objectSpace.forEachLiveCell(
        iterationScope,
        [&] (HeapCell* heapCell, HeapCell::Kind kind) -> IterationStatus {
            if (kind != HeapCell::JSCell)
                return IterationStatus::Continue;
            JSCell* cell = static_cast<JSCell*>(heapCell);
            if (cell->isObject() && asObject(cell)->isGlobalObject())
                result++;
            return IterationStatus::Continue;
        });
    return result;
}

size_t Heap::protectedObjectCount()
{
    size_t result = 0;
    forEachProtectedCell(
        [&] (JSCell*) {
            result++;
        });
    return result;
}

std::unique_ptr<TypeCountSet> Heap::protectedObjectTypeCounts()
{
    std::unique_ptr<TypeCountSet> result = std::make_unique<TypeCountSet>();
    forEachProtectedCell(
        [&] (JSCell* cell) {
            recordType(*result, cell);
        });
    return result;
}

std::unique_ptr<TypeCountSet> Heap::objectTypeCounts()
{
    std::unique_ptr<TypeCountSet> result = std::make_unique<TypeCountSet>();
    HeapIterationScope iterationScope(*this);
    m_objectSpace.forEachLiveCell(
        iterationScope,
        [&] (HeapCell* cell, HeapCell::Kind kind) -> IterationStatus {
            if (kind == HeapCell::JSCell)
                recordType(*result, static_cast<JSCell*>(cell));
            return IterationStatus::Continue;
        });
    return result;
}

void Heap::deleteAllCodeBlocks(DeleteAllCodeEffort effort)
{
    if (m_collectionScope && effort == DeleteAllCodeIfNotCollecting)
        return;
    
    PreventCollectionScope preventCollectionScope(*this);
    
    // If JavaScript is running, it's not safe to delete all JavaScript code, since
    // we'll end up returning to deleted code.
    RELEASE_ASSERT(!m_vm->entryScope);
    RELEASE_ASSERT(!m_collectionScope);

    completeAllJITPlans();

    for (ExecutableBase* executable : m_executables)
        executable->clearCode();
}

void Heap::deleteAllUnlinkedCodeBlocks(DeleteAllCodeEffort effort)
{
    if (m_collectionScope && effort == DeleteAllCodeIfNotCollecting)
        return;
    
    PreventCollectionScope preventCollectionScope(*this);

    RELEASE_ASSERT(!m_collectionScope);
    
    for (ExecutableBase* current : m_executables) {
        if (!current->isFunctionExecutable())
            continue;
        static_cast<FunctionExecutable*>(current)->unlinkedExecutable()->clearCode();
    }
}

void Heap::clearUnmarkedExecutables()
{
    for (unsigned i = m_executables.size(); i--;) {
        ExecutableBase* current = m_executables[i];
        if (isMarked(current))
            continue;

        // Eagerly dereference the Executable's JITCode in order to run watchpoint
        // destructors. Otherwise, watchpoints might fire for deleted CodeBlocks.
        current->clearCode();
        std::swap(m_executables[i], m_executables.last());
        m_executables.removeLast();
    }

    m_executables.shrinkToFit();
}

void Heap::deleteUnmarkedCompiledCode()
{
    clearUnmarkedExecutables();
    m_codeBlocks->deleteUnmarkedAndUnreferenced(*m_lastCollectionScope);
    m_jitStubRoutines->deleteUnmarkedJettisonedStubRoutines();
}

void Heap::addToRememberedSet(const JSCell* constCell)
{
    JSCell* cell = const_cast<JSCell*>(constCell);
    ASSERT(cell);
    ASSERT(!Options::useConcurrentJIT() || !isCompilationThread());
    m_barriersExecuted++;
    if (m_mutatorShouldBeFenced) {
        WTF::loadLoadFence();
        if (!isMarkedConcurrently(cell)) {
            // During a full collection a store into an unmarked object that had surivived past
            // collections will manifest as a store to an unmarked PossiblyBlack object. If the
            // object gets marked at some time after this then it will go down the normal marking
            // path. So, we don't have to remember this object. We could return here. But we go
            // further and attempt to re-white the object.
            
            RELEASE_ASSERT(m_collectionScope == CollectionScope::Full);
            
            if (cell->atomicCompareExchangeCellStateStrong(CellState::PossiblyBlack, CellState::DefinitelyWhite) == CellState::PossiblyBlack) {
                // Now we protect against this race:
                //
                //     1) Object starts out black + unmarked.
                //     --> We do isMarkedConcurrently here.
                //     2) Object is marked and greyed.
                //     3) Object is scanned and blacked.
                //     --> We do atomicCompareExchangeCellStateStrong here.
                //
                // In this case we would have made the object white again, even though it should
                // be black. This check lets us correct our mistake. This relies on the fact that
                // isMarkedConcurrently converges monotonically to true.
                if (isMarkedConcurrently(cell)) {
                    // It's difficult to work out whether the object should be grey or black at
                    // this point. We say black conservatively.
                    cell->setCellState(CellState::PossiblyBlack);
                }
                
                // Either way, we can return. Most likely, the object was not marked, and so the
                // object is now labeled white. This means that future barrier executions will not
                // fire. In the unlikely event that the object had become marked, we can still
                // return anyway, since we proved that the object was not marked at the time that
                // we executed this slow path.
            }
            
            return;
        }
    } else
        ASSERT(Heap::isMarkedConcurrently(cell));
    // It could be that the object was *just* marked. This means that the collector may set the
    // state to DefinitelyGrey and then to PossiblyOldOrBlack at any time. It's OK for us to
    // race with the collector here. If we win then this is accurate because the object _will_
    // get scanned again. If we lose then someone else will barrier the object again. That would
    // be unfortunate but not the end of the world.
    cell->setCellState(CellState::PossiblyGrey);
    m_mutatorMarkStack->append(cell);
}

void Heap::collectAllGarbage()
{
    if (!m_isSafeToCollect)
        return;
    
    collectSync(CollectionScope::Full);

    DeferGCForAWhile deferGC(*this);
    if (UNLIKELY(Options::useImmortalObjects()))
        sweeper()->willFinishSweeping();
    else {
        double before = 0;
        if (Options::logGC()) {
            dataLog("[Full sweep: ", capacity() / 1024, "kb ");
            before = currentTimeMS();
        }
        m_objectSpace.sweep();
        m_objectSpace.shrink();
        if (Options::logGC()) {
            double after = currentTimeMS();
            dataLog("=> ", capacity() / 1024, "kb, ", after - before, "ms]\n");
        }
    }
    m_objectSpace.assertNoUnswept();

    sweepAllLogicallyEmptyWeakBlocks();
}

void Heap::collectAsync(std::optional<CollectionScope> scope)
{
    if (!m_isSafeToCollect)
        return;

    bool alreadyRequested = false;
    {
        LockHolder locker(*m_threadLock);
        for (std::optional<CollectionScope> request : m_requests) {
            if (scope) {
                if (scope == CollectionScope::Eden) {
                    alreadyRequested = true;
                    break;
                } else {
                    RELEASE_ASSERT(scope == CollectionScope::Full);
                    if (request == CollectionScope::Full) {
                        alreadyRequested = true;
                        break;
                    }
                }
            } else {
                if (!request || request == CollectionScope::Full) {
                    alreadyRequested = true;
                    break;
                }
            }
        }
    }
    if (alreadyRequested)
        return;

    requestCollection(scope);
}

void Heap::collectSync(std::optional<CollectionScope> scope)
{
    if (!m_isSafeToCollect)
        return;
    
    waitForCollection(requestCollection(scope));
}

bool Heap::shouldCollectInThread(const LockHolder&)
{
    RELEASE_ASSERT(m_requests.isEmpty() == (m_lastServedTicket == m_lastGrantedTicket));
    RELEASE_ASSERT(m_lastServedTicket <= m_lastGrantedTicket);
    
    return !m_requests.isEmpty();
}

void Heap::collectInThread()
{
    m_currentGCStartTime = MonotonicTime::now();
    
    std::optional<CollectionScope> scope;
    {
        LockHolder locker(*m_threadLock);
        RELEASE_ASSERT(!m_requests.isEmpty());
        scope = m_requests.first();
    }
    
    SuperSamplerScope superSamplerScope(false);
    TimingScope collectImplTimingScope(scope, "Heap::collectInThread");
    
#if ENABLE(ALLOCATION_LOGGING)
    dataLogF("JSC GC starting collection.\n");
#endif
    
    stopTheWorld();
    
    if (false)
        dataLog("GC START!\n");

    MonotonicTime before;
    if (Options::logGC()) {
        dataLog("[GC: START ", capacity() / 1024, "kb ");
        before = MonotonicTime::now();
    }
    
    double gcStartTime;
    
    ASSERT(m_isSafeToCollect);
    if (m_collectionScope) {
        dataLog("Collection scope already set during GC: ", *m_collectionScope, "\n");
        RELEASE_ASSERT_NOT_REACHED();
    }
    
    willStartCollection(scope);
    collectImplTimingScope.setScope(*this);
    
    gcStartTime = WTF::monotonicallyIncreasingTime();
    if (m_verifier) {
        // Verify that live objects from the last GC cycle haven't been corrupted by
        // mutators before we begin this new GC cycle.
        m_verifier->verify(HeapVerifier::Phase::BeforeGC);
            
        m_verifier->initializeGCCycle();
        m_verifier->gatherLiveObjects(HeapVerifier::Phase::BeforeMarking);
    }
    
    prepareForMarking();
    
    markToFixpoint(gcStartTime);
    
    if (m_verifier) {
        m_verifier->gatherLiveObjects(HeapVerifier::Phase::AfterMarking);
        m_verifier->verify(HeapVerifier::Phase::AfterMarking);
    }
        
    if (vm()->typeProfiler())
        vm()->typeProfiler()->invalidateTypeSetCache();
        
    reapWeakHandles();
    pruneStaleEntriesFromWeakGCMaps();
    sweepArrayBuffers();
    snapshotUnswept();
    finalizeUnconditionalFinalizers();
    removeDeadCompilerWorklistEntries();
    notifyIncrementalSweeper();
    
    m_codeBlocks->iterateCurrentlyExecuting(
        [&] (CodeBlock* codeBlock) {
            writeBarrier(codeBlock);
        });
    m_codeBlocks->clearCurrentlyExecuting();
        
    m_objectSpace.prepareForAllocation();
    updateAllocationLimits();

    didFinishCollection(gcStartTime);
    
    if (m_verifier) {
        m_verifier->trimDeadObjects();
        m_verifier->verify(HeapVerifier::Phase::AfterGC);
    }

    if (false) {
        dataLog("Heap state after GC:\n");
        m_objectSpace.dumpBits();
    }
    
    if (Options::logGC()) {
        MonotonicTime after = MonotonicTime::now();
        double thisPauseMS = (after - m_stopTime).milliseconds();
        dataLog("p=", thisPauseMS, "ms (max ", maxPauseMS(thisPauseMS), "), cycle ", (after - before).milliseconds(), "ms END]\n");
    }
    
    {
        LockHolder locker(*m_threadLock);
        m_requests.removeFirst();
        m_lastServedTicket++;
        clearMutatorWaiting();
    }
    ParkingLot::unparkAll(&m_worldState);

    if (false)
        dataLog("GC END!\n");

    setNeedFinalize();
    resumeTheWorld();
    
    m_lastGCStartTime = m_currentGCStartTime;
    m_lastGCEndTime = MonotonicTime::now();
}

void Heap::stopTheWorld()
{
    RELEASE_ASSERT(!m_collectorBelievesThatTheWorldIsStopped);
    waitWhileNeedFinalize();
    stopTheMutator();
    
    if (m_mutatorDidRun)
        m_mutatorExecutionVersion++;
    
    m_mutatorDidRun = false;
    
    suspendCompilerThreads();
    m_collectorBelievesThatTheWorldIsStopped = true;

    forEachSlotVisitor(
        [&] (SlotVisitor& slotVisitor) {
            slotVisitor.updateMutatorIsStopped(NoLockingNecessary);
        });

#if ENABLE(JIT)
    {
        DeferGCForAWhile awhile(*this);
        if (JITWorklist::instance()->completeAllForVM(*m_vm))
            setGCDidJIT();
    }
#endif // ENABLE(JIT)
    
    vm()->shadowChicken().update(*vm(), vm()->topCallFrame);
    
    m_structureIDTable.flushOldTables();
    m_objectSpace.stopAllocating();
    
    m_stopTime = MonotonicTime::now();
}

void Heap::resumeTheWorld()
{
    // Calling resumeAllocating does the Right Thing depending on whether this is the end of a
    // collection cycle or this is just a concurrent phase within a collection cycle:
    // - At end of collection cycle: it's a no-op because prepareForAllocation already cleared the
    //   last active block.
    // - During collection cycle: it reinstates the last active block.
    m_objectSpace.resumeAllocating();
    
    m_barriersExecuted = 0;
    
    RELEASE_ASSERT(m_collectorBelievesThatTheWorldIsStopped);
    m_collectorBelievesThatTheWorldIsStopped = false;
    
    // FIXME: This could be vastly improved: we want to grab the locks in the order in which they
    // become available. We basically want a lockAny() method that will lock whatever lock is available
    // and tell you which one it locked. That would require teaching ParkingLot how to park on multiple
    // queues at once, which is totally achievable - it would just require memory allocation, which is
    // suboptimal but not a disaster. Alternatively, we could replace the SlotVisitor rightToRun lock
    // with a DLG-style handshake mechanism, but that seems not as general.
    Vector<SlotVisitor*, 8> slotVisitorsToUpdate;

    forEachSlotVisitor(
        [&] (SlotVisitor& slotVisitor) {
            slotVisitorsToUpdate.append(&slotVisitor);
        });
    
    for (unsigned countdown = 40; !slotVisitorsToUpdate.isEmpty() && countdown--;) {
        for (unsigned index = 0; index < slotVisitorsToUpdate.size(); ++index) {
            SlotVisitor& slotVisitor = *slotVisitorsToUpdate[index];
            bool remove = false;
            if (slotVisitor.hasAcknowledgedThatTheMutatorIsResumed())
                remove = true;
            else if (auto locker = tryHoldLock(slotVisitor.rightToRun())) {
                slotVisitor.updateMutatorIsStopped(locker);
                remove = true;
            }
            if (remove) {
                slotVisitorsToUpdate[index--] = slotVisitorsToUpdate.last();
                slotVisitorsToUpdate.takeLast();
            }
        }
        std::this_thread::yield();
    }
    
    for (SlotVisitor* slotVisitor : slotVisitorsToUpdate)
        slotVisitor->updateMutatorIsStopped();
    
    resumeCompilerThreads();
    resumeTheMutator();
}

void Heap::stopTheMutator()
{
    for (;;) {
        unsigned oldState = m_worldState.load();
        if ((oldState & stoppedBit)
            && (oldState & shouldStopBit))
            return;
        
        // Note: We could just have the mutator stop in-place like we do when !hasAccessBit. We could
        // switch to that if it turned out to be less confusing, but then it would not give the
        // mutator the opportunity to react to the world being stopped.
        if (oldState & mutatorWaitingBit) {
            if (m_worldState.compareExchangeWeak(oldState, oldState & ~mutatorWaitingBit))
                ParkingLot::unparkAll(&m_worldState);
            continue;
        }
        
        if (!(oldState & hasAccessBit)
            || (oldState & stoppedBit)) {
            // We can stop the world instantly.
            if (m_worldState.compareExchangeWeak(oldState, oldState | stoppedBit | shouldStopBit))
                return;
            continue;
        }
        
        RELEASE_ASSERT(oldState & hasAccessBit);
        RELEASE_ASSERT(!(oldState & stoppedBit));
        m_worldState.compareExchangeStrong(oldState, oldState | shouldStopBit);
        m_stopIfNecessaryTimer->scheduleSoon();
        ParkingLot::compareAndPark(&m_worldState, oldState | shouldStopBit);
    }
}

void Heap::resumeTheMutator()
{
    for (;;) {
        unsigned oldState = m_worldState.load();
        RELEASE_ASSERT(oldState & shouldStopBit);
        
        if (!(oldState & hasAccessBit)) {
            // We can resume the world instantly.
            if (m_worldState.compareExchangeWeak(oldState, oldState & ~(stoppedBit | shouldStopBit))) {
                ParkingLot::unparkAll(&m_worldState);
                return;
            }
            continue;
        }
        
        // We can tell the world to resume.
        if (m_worldState.compareExchangeWeak(oldState, oldState & ~shouldStopBit)) {
            ParkingLot::unparkAll(&m_worldState);
            return;
        }
    }
}

void Heap::stopIfNecessarySlow()
{
    while (stopIfNecessarySlow(m_worldState.load())) { }
    
    RELEASE_ASSERT(m_worldState.load() & hasAccessBit);
    RELEASE_ASSERT(!(m_worldState.load() & stoppedBit));
    
    handleGCDidJIT();
    handleNeedFinalize();
    m_mutatorDidRun = true;
}

bool Heap::stopIfNecessarySlow(unsigned oldState)
{
    RELEASE_ASSERT(oldState & hasAccessBit);
    
    // It's possible for us to wake up with finalization already requested but the world not yet
    // resumed. If that happens, we can't run finalization yet.
    if (!(oldState & stoppedBit)
        && handleNeedFinalize(oldState))
        return true;
    
    if (!(oldState & shouldStopBit) && !m_scheduler->shouldStop()) {
        if (!(oldState & stoppedBit))
            return false;
        m_worldState.compareExchangeStrong(oldState, oldState & ~stoppedBit);
        return true;
    }
    
    sanitizeStackForVM(m_vm);

    if (verboseStop) {
        dataLog("Stopping!\n");
        WTFReportBacktrace();
    }
    m_worldState.compareExchangeStrong(oldState, oldState | stoppedBit);
    ParkingLot::unparkAll(&m_worldState);
    ParkingLot::compareAndPark(&m_worldState, oldState | stoppedBit);
    return true;
}

template<typename Func>
void Heap::waitForCollector(const Func& func)
{
    for (;;) {
        bool done;
        {
            LockHolder locker(*m_threadLock);
            done = func(locker);
            if (!done) {
                setMutatorWaiting();
                // At this point, the collector knows that we intend to wait, and he will clear the
                // waiting bit and then unparkAll when the GC cycle finishes. Clearing the bit
                // prevents us from parking except if there is also stop-the-world. Unparking after
                // clearing means that if the clearing happens after we park, then we will unpark.
            }
        }

        // If we're in a stop-the-world scenario, we need to wait for that even if done is true.
        unsigned oldState = m_worldState.load();
        if (stopIfNecessarySlow(oldState))
            continue;
        
        if (done) {
            clearMutatorWaiting(); // Clean up just in case.
            return;
        }
        
        // If mutatorWaitingBit is still set then we want to wait.
        ParkingLot::compareAndPark(&m_worldState, oldState | mutatorWaitingBit);
    }
}

void Heap::acquireAccessSlow()
{
    for (;;) {
        unsigned oldState = m_worldState.load();
        RELEASE_ASSERT(!(oldState & hasAccessBit));
        
        if (oldState & shouldStopBit) {
            RELEASE_ASSERT(oldState & stoppedBit);
            if (verboseStop) {
                dataLog("Stopping in acquireAccess!\n");
                WTFReportBacktrace();
            }
            // Wait until we're not stopped anymore.
            ParkingLot::compareAndPark(&m_worldState, oldState);
            continue;
        }
        
        RELEASE_ASSERT(!(oldState & stoppedBit));
        unsigned newState = oldState | hasAccessBit;
        if (m_worldState.compareExchangeWeak(oldState, newState)) {
            handleGCDidJIT();
            handleNeedFinalize();
            m_mutatorDidRun = true;
            return;
        }
    }
}

void Heap::releaseAccessSlow()
{
    for (;;) {
        unsigned oldState = m_worldState.load();
        RELEASE_ASSERT(oldState & hasAccessBit);
        RELEASE_ASSERT(!(oldState & stoppedBit));
        
        if (handleNeedFinalize(oldState))
            continue;
        
        if (oldState & shouldStopBit) {
            unsigned newState = (oldState & ~hasAccessBit) | stoppedBit;
            if (m_worldState.compareExchangeWeak(oldState, newState)) {
                ParkingLot::unparkAll(&m_worldState);
                return;
            }
            continue;
        }
        
        RELEASE_ASSERT(!(oldState & shouldStopBit));
        
        if (m_worldState.compareExchangeWeak(oldState, oldState & ~hasAccessBit))
            return;
    }
}

bool Heap::handleGCDidJIT(unsigned oldState)
{
    RELEASE_ASSERT(oldState & hasAccessBit);
    if (!(oldState & gcDidJITBit))
        return false;
    if (m_worldState.compareExchangeWeak(oldState, oldState & ~gcDidJITBit)) {
        WTF::crossModifyingCodeFence();
        return true;
    }
    return true;
}

bool Heap::handleNeedFinalize(unsigned oldState)
{
    RELEASE_ASSERT(oldState & hasAccessBit);
    RELEASE_ASSERT(!(oldState & stoppedBit));
    
    if (!(oldState & needFinalizeBit))
        return false;
    if (m_worldState.compareExchangeWeak(oldState, oldState & ~needFinalizeBit)) {
        finalize();
        // Wake up anyone waiting for us to finalize. Note that they may have woken up already, in
        // which case they would be waiting for us to release heap access.
        ParkingLot::unparkAll(&m_worldState);
        return true;
    }
    return true;
}

void Heap::handleGCDidJIT()
{
    while (handleGCDidJIT(m_worldState.load())) { }
}

void Heap::handleNeedFinalize()
{
    while (handleNeedFinalize(m_worldState.load())) { }
}

void Heap::setGCDidJIT()
{
    m_worldState.transaction(
        [&] (unsigned& state) {
            RELEASE_ASSERT(state & stoppedBit);
            state |= gcDidJITBit;
        });
}

void Heap::setNeedFinalize()
{
    m_worldState.exchangeOr(needFinalizeBit);
    ParkingLot::unparkAll(&m_worldState);
    m_stopIfNecessaryTimer->scheduleSoon();
}

void Heap::waitWhileNeedFinalize()
{
    for (;;) {
        unsigned oldState = m_worldState.load();
        if (!(oldState & needFinalizeBit)) {
            // This means that either there was no finalize request or the main thread will finalize
            // with heap access, so a subsequent call to stopTheWorld() will return only when
            // finalize finishes.
            return;
        }
        ParkingLot::compareAndPark(&m_worldState, oldState);
    }
}

void Heap::setMutatorWaiting()
{
    m_worldState.exchangeOr(mutatorWaitingBit);
}

void Heap::clearMutatorWaiting()
{
    m_worldState.exchangeAnd(~mutatorWaitingBit);
}

void Heap::notifyThreadStopping(const LockHolder&)
{
    m_threadIsStopping = true;
    clearMutatorWaiting();
    ParkingLot::unparkAll(&m_worldState);
}

void Heap::finalize()
{
    {
        HelpingGCScope helpingGCScope(*this);
        deleteUnmarkedCompiledCode();
        deleteSourceProviderCaches();
        sweepLargeAllocations();
    }
    
    if (HasOwnPropertyCache* cache = vm()->hasOwnPropertyCache())
        cache->clear();
}

Heap::Ticket Heap::requestCollection(std::optional<CollectionScope> scope)
{
    stopIfNecessary();
    
    ASSERT(vm()->currentThreadIsHoldingAPILock());
    RELEASE_ASSERT(vm()->atomicStringTable() == wtfThreadData().atomicStringTable());
    
    LockHolder locker(*m_threadLock);
    m_requests.append(scope);
    m_lastGrantedTicket++;
    m_threadCondition->notifyOne(locker);
    return m_lastGrantedTicket;
}

void Heap::waitForCollection(Ticket ticket)
{
    waitForCollector(
        [&] (const LockHolder&) -> bool {
            return m_lastServedTicket >= ticket;
        });
}

void Heap::sweepLargeAllocations()
{
    m_objectSpace.sweepLargeAllocations();
}

void Heap::suspendCompilerThreads()
{
#if ENABLE(DFG_JIT)
    // We ensure the worklists so that it's not possible for the mutator to start a new worklist
    // after we have suspended the ones that he had started before. That's not very expensive since
    // the worklists use AutomaticThreads anyway.
    for (unsigned i = DFG::numberOfWorklists(); i--;)
        DFG::ensureWorklistForIndex(i).suspendAllThreads();
#endif
}

void Heap::willStartCollection(std::optional<CollectionScope> scope)
{
    if (Options::logGC())
        dataLog("=> ");
    
    if (shouldDoFullCollection(scope)) {
        m_collectionScope = CollectionScope::Full;
        m_shouldDoFullCollection = false;
        if (Options::logGC())
            dataLog("FullCollection, ");
        if (false)
            dataLog("Full collection!\n");
    } else {
        m_collectionScope = CollectionScope::Eden;
        if (Options::logGC())
            dataLog("EdenCollection, ");
        if (false)
            dataLog("Eden collection!\n");
    }
    if (m_collectionScope == CollectionScope::Full) {
        m_sizeBeforeLastFullCollect = m_sizeAfterLastCollect + m_bytesAllocatedThisCycle;
        m_extraMemorySize = 0;
        m_deprecatedExtraMemorySize = 0;
#if ENABLE(RESOURCE_USAGE)
        m_externalMemorySize = 0;
#endif

        if (m_fullActivityCallback)
            m_fullActivityCallback->willCollect();
    } else {
        ASSERT(m_collectionScope == CollectionScope::Eden);
        m_sizeBeforeLastEdenCollect = m_sizeAfterLastCollect + m_bytesAllocatedThisCycle;
    }

    if (m_edenActivityCallback)
        m_edenActivityCallback->willCollect();

    for (auto* observer : m_observers)
        observer->willGarbageCollect();
}

void Heap::prepareForMarking()
{
    m_objectSpace.prepareForMarking();
}

void Heap::reapWeakHandles()
{
    m_objectSpace.reapWeakSets();
}

void Heap::pruneStaleEntriesFromWeakGCMaps()
{
    if (m_collectionScope != CollectionScope::Full)
        return;
    for (auto& pruneCallback : m_weakGCMaps.values())
        pruneCallback();
}

void Heap::sweepArrayBuffers()
{
    m_arrayBuffers.sweep();
}

void Heap::snapshotUnswept()
{
    TimingScope timingScope(*this, "Heap::snapshotUnswept");
    m_objectSpace.snapshotUnswept();
}

void Heap::deleteSourceProviderCaches()
{
    m_vm->clearSourceProviderCaches();
}

void Heap::notifyIncrementalSweeper()
{
    if (m_collectionScope == CollectionScope::Full) {
        if (!m_logicallyEmptyWeakBlocks.isEmpty())
            m_indexOfNextLogicallyEmptyWeakBlockToSweep = 0;
    }

    m_sweeper->startSweeping();
}

void Heap::updateAllocationLimits()
{
    static const bool verbose = false;
    
    if (verbose) {
        dataLog("\n");
        dataLog("bytesAllocatedThisCycle = ", m_bytesAllocatedThisCycle, "\n");
    }
    
    // Calculate our current heap size threshold for the purpose of figuring out when we should
    // run another collection. This isn't the same as either size() or capacity(), though it should
    // be somewhere between the two. The key is to match the size calculations involved calls to
    // didAllocate(), while never dangerously underestimating capacity(). In extreme cases of
    // fragmentation, we may have size() much smaller than capacity().
    size_t currentHeapSize = 0;

    // For marked space, we use the total number of bytes visited. This matches the logic for
    // MarkedAllocator's calls to didAllocate(), which effectively accounts for the total size of
    // objects allocated rather than blocks used. This will underestimate capacity(), and in case
    // of fragmentation, this may be substantial. Fortunately, marked space rarely fragments because
    // cells usually have a narrow range of sizes. So, the underestimation is probably OK.
    currentHeapSize += m_totalBytesVisited;
    if (verbose)
        dataLog("totalBytesVisited = ", m_totalBytesVisited, ", currentHeapSize = ", currentHeapSize, "\n");

    // It's up to the user to ensure that extraMemorySize() ends up corresponding to allocation-time
    // extra memory reporting.
    currentHeapSize += extraMemorySize();

    if (verbose)
        dataLog("extraMemorySize() = ", extraMemorySize(), ", currentHeapSize = ", currentHeapSize, "\n");
    
    if (Options::gcMaxHeapSize() && currentHeapSize > Options::gcMaxHeapSize())
        HeapStatistics::exitWithFailure();

    if (m_collectionScope == CollectionScope::Full) {
        // To avoid pathological GC churn in very small and very large heaps, we set
        // the new allocation limit based on the current size of the heap, with a
        // fixed minimum.
        m_maxHeapSize = max(minHeapSize(m_heapType, m_ramSize), proportionalHeapSize(currentHeapSize, m_ramSize));
        if (verbose)
            dataLog("Full: maxHeapSize = ", m_maxHeapSize, "\n");
        m_maxEdenSize = m_maxHeapSize - currentHeapSize;
        if (verbose)
            dataLog("Full: maxEdenSize = ", m_maxEdenSize, "\n");
        m_sizeAfterLastFullCollect = currentHeapSize;
        if (verbose)
            dataLog("Full: sizeAfterLastFullCollect = ", currentHeapSize, "\n");
        m_bytesAbandonedSinceLastFullCollect = 0;
        if (verbose)
            dataLog("Full: bytesAbandonedSinceLastFullCollect = ", 0, "\n");
    } else {
        ASSERT(currentHeapSize >= m_sizeAfterLastCollect);
        // Theoretically, we shouldn't ever scan more memory than the heap size we planned to have.
        // But we are sloppy, so we have to defend against the overflow.
        m_maxEdenSize = currentHeapSize > m_maxHeapSize ? 0 : m_maxHeapSize - currentHeapSize;
        if (verbose)
            dataLog("Eden: maxEdenSize = ", m_maxEdenSize, "\n");
        m_sizeAfterLastEdenCollect = currentHeapSize;
        if (verbose)
            dataLog("Eden: sizeAfterLastEdenCollect = ", currentHeapSize, "\n");
        double edenToOldGenerationRatio = (double)m_maxEdenSize / (double)m_maxHeapSize;
        double minEdenToOldGenerationRatio = 1.0 / 3.0;
        if (edenToOldGenerationRatio < minEdenToOldGenerationRatio)
            m_shouldDoFullCollection = true;
        // This seems suspect at first, but what it does is ensure that the nursery size is fixed.
        m_maxHeapSize += currentHeapSize - m_sizeAfterLastCollect;
        if (verbose)
            dataLog("Eden: maxHeapSize = ", m_maxHeapSize, "\n");
        m_maxEdenSize = m_maxHeapSize - currentHeapSize;
        if (verbose)
            dataLog("Eden: maxEdenSize = ", m_maxEdenSize, "\n");
        if (m_fullActivityCallback) {
            ASSERT(currentHeapSize >= m_sizeAfterLastFullCollect);
            m_fullActivityCallback->didAllocate(currentHeapSize - m_sizeAfterLastFullCollect);
        }
    }

    m_sizeAfterLastCollect = currentHeapSize;
    if (verbose)
        dataLog("sizeAfterLastCollect = ", m_sizeAfterLastCollect, "\n");
    m_bytesAllocatedThisCycle = 0;

    if (Options::logGC())
        dataLog("=> ", currentHeapSize / 1024, "kb, ");
}

void Heap::didFinishCollection(double gcStartTime)
{
    double gcEndTime = WTF::monotonicallyIncreasingTime();
    CollectionScope scope = *m_collectionScope;
    if (scope == CollectionScope::Full)
        m_lastFullGCLength = gcEndTime - gcStartTime;
    else
        m_lastEdenGCLength = gcEndTime - gcStartTime;

#if ENABLE(RESOURCE_USAGE)
    ASSERT(externalMemorySize() <= extraMemorySize());
#endif

    if (Options::recordGCPauseTimes())
        HeapStatistics::recordGCPauseTime(gcStartTime, gcEndTime);

    if (Options::useZombieMode())
        zombifyDeadObjects();

    if (Options::dumpObjectStatistics())
        HeapStatistics::dumpObjectStatistics(this);

    if (HeapProfiler* heapProfiler = m_vm->heapProfiler()) {
        gatherExtraHeapSnapshotData(*heapProfiler);
        removeDeadHeapSnapshotNodes(*heapProfiler);
    }

    RELEASE_ASSERT(m_collectionScope);
    m_lastCollectionScope = m_collectionScope;
    m_collectionScope = std::nullopt;

    for (auto* observer : m_observers)
        observer->didGarbageCollect(scope);
}

void Heap::resumeCompilerThreads()
{
#if ENABLE(DFG_JIT)
    for (unsigned i = DFG::numberOfWorklists(); i--;)
        DFG::existingWorklistForIndex(i).resumeAllThreads();
#endif
}

GCActivityCallback* Heap::fullActivityCallback()
{
    return m_fullActivityCallback.get();
}

GCActivityCallback* Heap::edenActivityCallback()
{
    return m_edenActivityCallback.get();
}

IncrementalSweeper* Heap::sweeper()
{
    return m_sweeper.get();
}

void Heap::setGarbageCollectionTimerEnabled(bool enable)
{
    if (m_fullActivityCallback)
        m_fullActivityCallback->setEnabled(enable);
    if (m_edenActivityCallback)
        m_edenActivityCallback->setEnabled(enable);
}

void Heap::didAllocate(size_t bytes)
{
    if (m_edenActivityCallback)
        m_edenActivityCallback->didAllocate(m_bytesAllocatedThisCycle + m_bytesAbandonedSinceLastFullCollect);
    m_bytesAllocatedThisCycle += bytes;
}

bool Heap::isValidAllocation(size_t)
{
    if (!isValidThreadState(m_vm))
        return false;

    if (isCurrentThreadBusy())
        return false;
    
    return true;
}

void Heap::addFinalizer(JSCell* cell, Finalizer finalizer)
{
    WeakSet::allocate(cell, &m_finalizerOwner, reinterpret_cast<void*>(finalizer)); // Balanced by FinalizerOwner::finalize().
}

void Heap::FinalizerOwner::finalize(Handle<Unknown> handle, void* context)
{
    HandleSlot slot = handle.slot();
    Finalizer finalizer = reinterpret_cast<Finalizer>(context);
    finalizer(slot->asCell());
    WeakSet::deallocate(WeakImpl::asWeakImpl(slot));
}

void Heap::addExecutable(ExecutableBase* executable)
{
    m_executables.append(executable);
}

void Heap::collectAllGarbageIfNotDoneRecently()
{
    if (!m_fullActivityCallback) {
        collectAllGarbage();
        return;
    }

    if (m_fullActivityCallback->didSyncGCRecently()) {
        // A synchronous GC was already requested recently so we merely accelerate next collection.
        reportAbandonedObjectGraph();
        return;
    }

    m_fullActivityCallback->setDidSyncGCRecently();
    collectAllGarbage();
}

class Zombify : public MarkedBlock::VoidFunctor {
public:
    inline void visit(HeapCell* cell) const
    {
        void** current = reinterpret_cast_ptr<void**>(cell);

        // We want to maintain zapped-ness because that's how we know if we've called 
        // the destructor.
        if (cell->isZapped())
            current++;

        void* limit = static_cast<void*>(reinterpret_cast<char*>(cell) + cell->cellSize());
        for (; current < limit; current++)
            *current = zombifiedBits;
    }
    IterationStatus operator()(HeapCell* cell, HeapCell::Kind) const
    {
        visit(cell);
        return IterationStatus::Continue;
    }
};

void Heap::zombifyDeadObjects()
{
    // Sweep now because destructors will crash once we're zombified.
    m_objectSpace.sweep();
    HeapIterationScope iterationScope(*this);
    m_objectSpace.forEachDeadCell(iterationScope, Zombify());
}

bool Heap::shouldDoFullCollection(std::optional<CollectionScope> scope) const
{
    if (!Options::useGenerationalGC())
        return true;

    if (!scope)
        return m_shouldDoFullCollection;
    return *scope == CollectionScope::Full;
}

void Heap::addLogicallyEmptyWeakBlock(WeakBlock* block)
{
    m_logicallyEmptyWeakBlocks.append(block);
}

void Heap::sweepAllLogicallyEmptyWeakBlocks()
{
    if (m_logicallyEmptyWeakBlocks.isEmpty())
        return;

    m_indexOfNextLogicallyEmptyWeakBlockToSweep = 0;
    while (sweepNextLogicallyEmptyWeakBlock()) { }
}

bool Heap::sweepNextLogicallyEmptyWeakBlock()
{
    if (m_indexOfNextLogicallyEmptyWeakBlockToSweep == WTF::notFound)
        return false;

    WeakBlock* block = m_logicallyEmptyWeakBlocks[m_indexOfNextLogicallyEmptyWeakBlockToSweep];

    block->sweep();
    if (block->isEmpty()) {
        std::swap(m_logicallyEmptyWeakBlocks[m_indexOfNextLogicallyEmptyWeakBlockToSweep], m_logicallyEmptyWeakBlocks.last());
        m_logicallyEmptyWeakBlocks.removeLast();
        WeakBlock::destroy(*this, block);
    } else
        m_indexOfNextLogicallyEmptyWeakBlockToSweep++;

    if (m_indexOfNextLogicallyEmptyWeakBlockToSweep >= m_logicallyEmptyWeakBlocks.size()) {
        m_indexOfNextLogicallyEmptyWeakBlockToSweep = WTF::notFound;
        return false;
    }

    return true;
}

size_t Heap::threadVisitCount()
{       
    unsigned long result = 0;
    for (auto& parallelVisitor : m_parallelSlotVisitors)
        result += parallelVisitor->visitCount();
    return result;
}

size_t Heap::bytesVisited()
{
    return m_collectorSlotVisitor->bytesVisited() + threadBytesVisited();
}

size_t Heap::threadBytesVisited()
{       
    size_t result = 0;
    for (auto& parallelVisitor : m_parallelSlotVisitors)
        result += parallelVisitor->bytesVisited();
    return result;
}

void Heap::forEachCodeBlockImpl(const ScopedLambda<bool(CodeBlock*)>& func)
{
    // We don't know the full set of CodeBlocks until compilation has terminated.
    completeAllJITPlans();

    return m_codeBlocks->iterate(func);
}

void Heap::forEachCodeBlockIgnoringJITPlansImpl(const ScopedLambda<bool(CodeBlock*)>& func)
{
    return m_codeBlocks->iterate(func);
}

void Heap::writeBarrierSlowPath(const JSCell* from)
{
    if (UNLIKELY(mutatorShouldBeFenced())) {
        // In this case, the barrierThreshold is the tautological threshold, so from could still be
        // not black. But we can't know for sure until we fire off a fence.
        WTF::storeLoadFence();
        if (from->cellState() != CellState::PossiblyBlack)
            return;
    }
    
    addToRememberedSet(from);
}

bool Heap::isCurrentThreadBusy()
{
    return mayBeGCThread() || mutatorState() != MutatorState::Running;
}

void Heap::reportExtraMemoryVisited(size_t size)
{
    size_t* counter = &m_extraMemorySize;
    
    for (;;) {
        size_t oldSize = *counter;
        if (WTF::atomicCompareExchangeWeakRelaxed(counter, oldSize, oldSize + size))
            return;
    }
}

#if ENABLE(RESOURCE_USAGE)
void Heap::reportExternalMemoryVisited(size_t size)
{
    size_t* counter = &m_externalMemorySize;

    for (;;) {
        size_t oldSize = *counter;
        if (WTF::atomicCompareExchangeWeakRelaxed(counter, oldSize, oldSize + size))
            return;
    }
}
#endif

void Heap::collectIfNecessaryOrDefer(GCDeferralContext* deferralContext)
{
    ASSERT(!DisallowGC::isGCDisallowedOnCurrentThread());

    if (!m_isSafeToCollect)
        return;
    if (mutatorState() == MutatorState::HelpingGC)
        return;
    if (!Options::useGC())
        return;
    
    if (mayNeedToStop()) {
        if (deferralContext)
            deferralContext->m_shouldGC = true;
        else if (isDeferred())
            m_didDeferGCWork = true;
        else {
            stopIfNecessary();
            // FIXME: Check if the scheduler wants us to stop.
            // https://bugs.webkit.org/show_bug.cgi?id=166827
        }
    }
    
    if (UNLIKELY(Options::gcMaxHeapSize())) {
        if (m_bytesAllocatedThisCycle <= Options::gcMaxHeapSize())
            return;
    } else {
        if (m_bytesAllocatedThisCycle <= m_maxEdenSize)
            return;
    }

    if (deferralContext)
        deferralContext->m_shouldGC = true;
    else if (isDeferred())
        m_didDeferGCWork = true;
    else
        collectAsync();
}

void Heap::decrementDeferralDepthAndGCIfNeededSlow()
{
    // Can't do anything if we're still deferred.
    if (m_deferralDepth)
        return;
    
    ASSERT(!isDeferred());
    
    m_didDeferGCWork = false;
    // FIXME: Bring back something like the DeferGCProbability mode.
    // https://bugs.webkit.org/show_bug.cgi?id=166627
    collectIfNecessaryOrDefer();
}

void Heap::registerWeakGCMap(void* weakGCMap, std::function<void()> pruningCallback)
{
    m_weakGCMaps.add(weakGCMap, WTFMove(pruningCallback));
}

void Heap::unregisterWeakGCMap(void* weakGCMap)
{
    m_weakGCMaps.remove(weakGCMap);
}

void Heap::didAllocateBlock(size_t capacity)
{
#if ENABLE(RESOURCE_USAGE)
    m_blockBytesAllocated += capacity;
#else
    UNUSED_PARAM(capacity);
#endif
}

void Heap::didFreeBlock(size_t capacity)
{
#if ENABLE(RESOURCE_USAGE)
    m_blockBytesAllocated -= capacity;
#else
    UNUSED_PARAM(capacity);
#endif
}

#if USE(CF)
void Heap::setRunLoop(CFRunLoopRef runLoop)
{
    m_runLoop = runLoop;
    m_fullActivityCallback->setRunLoop(runLoop);
    m_edenActivityCallback->setRunLoop(runLoop);
    m_sweeper->setRunLoop(runLoop);
}
#endif // USE(CF)

void Heap::addCoreConstraints()
{
    m_constraintSet->add(
        "Cs", "Conservative Scan",
        [this] (SlotVisitor& slotVisitor, const VisitingTimeout&) {
            TimingScope preConvergenceTimingScope(*this, "Constraint: conservative scan");
            m_objectSpace.prepareForConservativeScan();
            ConservativeRoots conservativeRoots(*this);
            SuperSamplerScope superSamplerScope(false);
            gatherStackRoots(conservativeRoots);
            gatherJSStackRoots(conservativeRoots);
            gatherScratchBufferRoots(conservativeRoots);
            slotVisitor.append(conservativeRoots);
        },
        ConstraintVolatility::GreyedByExecution);
    
    m_constraintSet->add(
        "Msr", "Misc Small Roots",
        [this] (SlotVisitor& slotVisitor, const VisitingTimeout&) {
#if JSC_OBJC_API_ENABLED
            scanExternalRememberedSet(*m_vm, slotVisitor);
#endif

            if (m_vm->smallStrings.needsToBeVisited(*m_collectionScope))
                m_vm->smallStrings.visitStrongReferences(slotVisitor);
            
            for (auto& pair : m_protectedValues)
                slotVisitor.appendUnbarriered(pair.key);
            
            if (m_markListSet && m_markListSet->size())
                MarkedArgumentBuffer::markLists(slotVisitor, *m_markListSet);
            
            slotVisitor.appendUnbarriered(m_vm->exception());
            slotVisitor.appendUnbarriered(m_vm->lastException());
        },
        ConstraintVolatility::GreyedByExecution);
    
    m_constraintSet->add(
        "Sh", "Strong Handles",
        [this] (SlotVisitor& slotVisitor, const VisitingTimeout&) {
            m_handleSet.visitStrongHandles(slotVisitor);
            m_handleStack.visit(slotVisitor);
        },
        ConstraintVolatility::GreyedByExecution);
    
    m_constraintSet->add(
        "D", "Debugger",
        [this] (SlotVisitor& slotVisitor, const VisitingTimeout&) {
#if ENABLE(SAMPLING_PROFILER)
            if (SamplingProfiler* samplingProfiler = m_vm->samplingProfiler()) {
                LockHolder locker(samplingProfiler->getLock());
                samplingProfiler->processUnverifiedStackTraces();
                samplingProfiler->visit(slotVisitor);
                if (Options::logGC() == GCLogging::Verbose)
                    dataLog("Sampling Profiler data:\n", slotVisitor);
            }
#endif // ENABLE(SAMPLING_PROFILER)
            
            if (m_vm->typeProfiler())
                m_vm->typeProfilerLog()->visit(slotVisitor);
            
            m_vm->shadowChicken().visitChildren(slotVisitor);
        },
        ConstraintVolatility::GreyedByExecution);
    
    m_constraintSet->add(
        "Jsr", "JIT Stub Routines",
        [this] (SlotVisitor& slotVisitor, const VisitingTimeout&) {
            m_jitStubRoutines->traceMarkedStubRoutines(slotVisitor);
        },
        ConstraintVolatility::GreyedByExecution);
    
    m_constraintSet->add(
        "Ws", "Weak Sets",
        [this] (SlotVisitor& slotVisitor, const VisitingTimeout&) {
            m_objectSpace.visitWeakSets(slotVisitor);
        },
        ConstraintVolatility::GreyedByMarking);
    
    m_constraintSet->add(
        "Wrh", "Weak Reference Harvesters",
        [this] (SlotVisitor& slotVisitor, const VisitingTimeout&) {
            for (WeakReferenceHarvester* current = m_weakReferenceHarvesters.head(); current; current = current->next())
                current->visitWeakReferences(slotVisitor);
        },
        ConstraintVolatility::GreyedByMarking);
    
#if ENABLE(DFG_JIT)
    m_constraintSet->add(
        "Dw", "DFG Worklists",
        [this] (SlotVisitor& slotVisitor, const VisitingTimeout&) {
            for (unsigned i = DFG::numberOfWorklists(); i--;)
                DFG::existingWorklistForIndex(i).visitWeakReferences(slotVisitor);
            
            // FIXME: This is almost certainly unnecessary.
            // https://bugs.webkit.org/show_bug.cgi?id=166829
            DFG::iterateCodeBlocksForGC(
                *m_vm,
                [&] (CodeBlock* codeBlock) {
                    slotVisitor.appendUnbarriered(codeBlock);
                });
            
            if (Options::logGC() == GCLogging::Verbose)
                dataLog("DFG Worklists:\n", slotVisitor);
        },
        ConstraintVolatility::GreyedByMarking);
#endif
    
    m_constraintSet->add(
        "Cb", "CodeBlocks",
        [this] (SlotVisitor& slotVisitor, const VisitingTimeout&) {
            iterateExecutingAndCompilingCodeBlocksWithoutHoldingLocks(
                [&] (CodeBlock* codeBlock) {
                    // Visit the CodeBlock as a constraint only if it's black.
                    if (Heap::isMarked(codeBlock)
                        && codeBlock->cellState() == CellState::PossiblyBlack)
                        slotVisitor.visitAsConstraint(codeBlock);
                });
        },
        ConstraintVolatility::SeldomGreyed);
    
    m_constraintSet->add(
        "Mrms", "Mutator+Race Mark Stack",
        [this] (SlotVisitor& slotVisitor, const VisitingTimeout&) {
            // Indicate to the fixpoint that we introduced work!
            size_t size = m_mutatorMarkStack->size() + m_raceMarkStack->size();
            slotVisitor.addToVisitCount(size);
            
            if (Options::logGC())
                dataLog("(", size, ")");
            
            m_mutatorMarkStack->transferTo(slotVisitor.mutatorMarkStack());
            m_raceMarkStack->transferTo(slotVisitor.mutatorMarkStack());
        },
        [this] (SlotVisitor&) -> double {
            return m_mutatorMarkStack->size() + m_raceMarkStack->size();
        },
        ConstraintVolatility::GreyedByExecution);
}

void Heap::addMarkingConstraint(std::unique_ptr<MarkingConstraint> constraint)
{
    PreventCollectionScope preventCollectionScope(*this);
    m_constraintSet->add(WTFMove(constraint));
}

void Heap::notifyIsSafeToCollect()
{
    addCoreConstraints();
    
    m_isSafeToCollect = true;
    
    if (Options::collectContinuously()) {
        m_collectContinuouslyThread = createThread(
            "JSC DEBUG Continuous GC",
            [this] () {
                MonotonicTime initialTime = MonotonicTime::now();
                Seconds period = Seconds::fromMilliseconds(Options::collectContinuouslyPeriodMS());
                while (!m_shouldStopCollectingContinuously) {
                    {
                        LockHolder locker(*m_threadLock);
                        if (m_requests.isEmpty()) {
                            m_requests.append(std::nullopt);
                            m_lastGrantedTicket++;
                            m_threadCondition->notifyOne(locker);
                        }
                    }
                    
                    {
                        LockHolder locker(m_collectContinuouslyLock);
                        Seconds elapsed = MonotonicTime::now() - initialTime;
                        Seconds elapsedInPeriod = elapsed % period;
                        MonotonicTime timeToWakeUp =
                            initialTime + elapsed - elapsedInPeriod + period;
                        while (!hasElapsed(timeToWakeUp) && !m_shouldStopCollectingContinuously) {
                            m_collectContinuouslyCondition.waitUntil(
                                m_collectContinuouslyLock, timeToWakeUp);
                        }
                    }
                }
            });
    }
}

void Heap::preventCollection()
{
    if (!m_isSafeToCollect)
        return;
    
    // This prevents the collectContinuously thread from starting a collection.
    m_collectContinuouslyLock.lock();
    
    // Wait for all collections to finish.
    waitForCollector(
        [&] (const LockHolder&) -> bool {
            ASSERT(m_lastServedTicket <= m_lastGrantedTicket);
            return m_lastServedTicket == m_lastGrantedTicket;
        });
    
    // Now a collection can only start if this thread starts it.
    RELEASE_ASSERT(!m_collectionScope);
}

void Heap::allowCollection()
{
    if (!m_isSafeToCollect)
        return;
    
    m_collectContinuouslyLock.unlock();
}

template<typename Func>
void Heap::forEachSlotVisitor(const Func& func)
{
    auto locker = holdLock(m_parallelSlotVisitorLock);
    func(*m_collectorSlotVisitor);
    for (auto& slotVisitor : m_parallelSlotVisitors)
        func(*slotVisitor);
}

void Heap::setMutatorShouldBeFenced(bool value)
{
    m_mutatorShouldBeFenced = value;
    m_barrierThreshold = value ? tautologicalThreshold : blackThreshold;
}
    
} // namespace JSC
