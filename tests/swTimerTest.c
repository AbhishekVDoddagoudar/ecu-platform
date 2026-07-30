/******************************************************************************
 * @file   testSwTimer.c
 * @brief  Unit tests for the software timer pool (swTimer.h).
 *
 * @details
 * swTimer.c implements a reentrant, caller-owned pool of one-shot/periodic
 * timers driven by softwareTimerTick(). Handles are (index, generation)
 * pairs; a stale handle (deleted-and-reused slot) is rejected with
 * SOFTWARE_TIMER_ERROR_INVALID_HANDLE rather than silently operating on the
 * wrong timer. All status codes are SOFTWARE_TIMER_* / SOFTWARE_TIMER_SUCCESS.
 *
 * @author Abhishek Doddagoudar
 * @date   July 2026
 ******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <swTimer.h>

static int totalTests = 0;
static int passedTests = 0;

#define TEST_ASSERT(cond, desc)                               \
    do                                                        \
    {                                                         \
        totalTests++;                                         \
        if (cond)                                             \
        {                                                     \
            printf("  PASS (line %d): %s\n", __LINE__, desc); \
            passedTests++;                                    \
        }                                                     \
        else                                                  \
        {                                                     \
            printf("  FAIL (line %d): %s\n", __LINE__, desc); \
        }                                                     \
    } while (0)

/* -------------------------------------------------------------------------
 * Shared callback instrumentation
 * ---------------------------------------------------------------------- */

#define MAX_FIRE_LOG 16

static int callbackFireCount;
static TimerHandle_t firedOrder[MAX_FIRE_LOG];
static void *firedContext[MAX_FIRE_LOG];
static int firedCount;

/* Pool made visible to callbacks that need to act on the pool they belong
 * to (e.g. stop/start/delete their own or another timer from within
 * softwareTimerTick()). */
static SoftwareTimerPool_t *callbackPool;

static void resetCallbackLog(void)
{
    callbackFireCount = 0;
    firedCount = 0;
    memset(firedOrder, 0, sizeof(firedOrder));
    memset(firedContext, 0, sizeof(firedContext));
}

static void basicCallback(TimerHandle_t handle, void *context)
{
    callbackFireCount++;
    if (firedCount < MAX_FIRE_LOG)
    {
        firedOrder[firedCount] = handle;
        firedContext[firedCount] = context;
        firedCount++;
    }
}

/* Callback that stops its own timer -- exercises the "callback may
 * safely stop the timer currently firing" contract for a one-shot timer
 * (which would already be stopped) and, more importantly, for a periodic
 * timer that would otherwise keep reloading. */
static TimerHandle_t selfStopHandle;

static void selfStoppingCallback(TimerHandle_t handle, void *context)
{
    (void)context;
    callbackFireCount++;
    softwareTimerStop(callbackPool, handle);
}

/* Callback that deletes its own timer from within the firing call. */
static void selfDeletingCallback(TimerHandle_t handle, void *context)
{
    (void)context;
    callbackFireCount++;
    softwareTimerDelete(callbackPool, handle);
}

/* Callback that restarts a *different* timer, to exercise cross-timer
 * mutation from within a tick's callback dispatch. */
static TimerHandle_t otherTimerHandle;

static void restartsOtherTimerCallback(TimerHandle_t handle, void *context)
{
    (void)handle;
    (void)context;
    callbackFireCount++;
    softwareTimerStart(callbackPool, otherTimerHandle, 5U);
}

/* -------------------------------------------------------------------------
 * softwareTimerInit
 * ---------------------------------------------------------------------- */

static void testInit(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[4];

    printf("\nsoftwareTimerInit:\n");

    memset(storage, 0xFF, sizeof(storage)); /* poison storage to prove init clears it */

    TEST_ASSERT(softwareTimerInit(&pool, storage, 4U) == SOFTWARE_TIMER_SUCCESS,
                "init with valid pool and storage succeeds");
    TEST_ASSERT(pool.storage == storage, "pool storage pointer is stored correctly");
    TEST_ASSERT(pool.capacity == 4U, "pool capacity is stored correctly");
    TEST_ASSERT(storage[0].state == SOFTWARE_TIMER_STATE_UNUSED, "slot 0 is marked UNUSED after init");
    TEST_ASSERT(storage[3].state == SOFTWARE_TIMER_STATE_UNUSED, "last slot is marked UNUSED after init");
    TEST_ASSERT(storage[0].callback == NULL, "slot callback is cleared after init");
    TEST_ASSERT(storage[0].generation == 0U, "slot generation is reset to 0 after init");

    TEST_ASSERT(softwareTimerInit(NULL, storage, 4U) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "init with NULL pool pointer returns SOFTWARE_TIMER_ERROR_NULL_POINTER");
    TEST_ASSERT(softwareTimerInit(&pool, NULL, 4U) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "init with NULL storage pointer returns SOFTWARE_TIMER_ERROR_NULL_POINTER");
    TEST_ASSERT(softwareTimerInit(&pool, storage, 0U) == SOFTWARE_TIMER_ERROR_INVALID_CAPACITY,
                "init with capacity 0 returns SOFTWARE_TIMER_ERROR_INVALID_CAPACITY");
}

/* -------------------------------------------------------------------------
 * softwareTimerCreate
 * ---------------------------------------------------------------------- */

static void testCreate(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[2];
    TimerHandle_t handleA, handleB, handleC;

    printf("\nsoftwareTimerCreate:\n");

    softwareTimerInit(&pool, storage, 2U);

    TEST_ASSERT(softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, &handleA) == SOFTWARE_TIMER_SUCCESS,
                "create a one-shot timer in a pool with free slots succeeds");
    TEST_ASSERT(handleA.index == 0U, "first created timer is placed in slot 0");

    TEST_ASSERT(softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_PERIODIC, basicCallback, NULL, &handleB) == SOFTWARE_TIMER_SUCCESS,
                "create a periodic timer in the same pool succeeds");
    TEST_ASSERT(handleB.index == 1U, "second created timer is placed in slot 1");

    TEST_ASSERT(softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, &handleC) == SOFTWARE_TIMER_ERROR_POOL_FULL,
                "create in a pool with no free slots returns SOFTWARE_TIMER_ERROR_POOL_FULL");

    /* negative: invalid arguments */
    TEST_ASSERT(softwareTimerCreate(NULL, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, &handleC) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "create with NULL pool pointer returns SOFTWARE_TIMER_ERROR_NULL_POINTER");
    TEST_ASSERT(softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, NULL, NULL, &handleC) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "create with NULL callback returns SOFTWARE_TIMER_ERROR_NULL_POINTER");
    TEST_ASSERT(softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, NULL) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "create with NULL outHandle returns SOFTWARE_TIMER_ERROR_NULL_POINTER");
    TEST_ASSERT(softwareTimerCreate(&pool, (SoftwareTimerMode_t)99, basicCallback, NULL, &handleC) == SOFTWARE_TIMER_ERROR_INVALID_MODE,
                "create with an invalid mode value returns SOFTWARE_TIMER_ERROR_INVALID_MODE");
}

static void testCreateInitialStateIsStopped(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[1];
    TimerHandle_t handle;
    SoftwareTimerState_t state;
    uint32_t remaining = 1U;

    printf("\nsoftwareTimerCreate leaves the timer stopped and not counting down:\n");

    softwareTimerInit(&pool, storage, 1U);
    softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, &handle);

    softwareTimerGetState(&pool, handle, &state);
    TEST_ASSERT(state == SOFTWARE_TIMER_STATE_STOPPED, "a freshly created timer starts in the STOPPED state");

    softwareTimerGetRemainingTicks(&pool, handle, &remaining);
    TEST_ASSERT(remaining == 0U, "a freshly created timer reports 0 remaining ticks before being started");
}

/* -------------------------------------------------------------------------
 * softwareTimerStart
 * ---------------------------------------------------------------------- */

static void testStart(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[1];
    TimerHandle_t handle, badHandle;
    SoftwareTimerState_t state;
    uint32_t remaining = 0U;

    printf("\nsoftwareTimerStart:\n");

    softwareTimerInit(&pool, storage, 1U);
    softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, &handle);

    TEST_ASSERT(softwareTimerStart(&pool, handle, 10U) == SOFTWARE_TIMER_SUCCESS,
                "start with a valid handle and non-zero timeout succeeds");

    softwareTimerGetState(&pool, handle, &state);
    TEST_ASSERT(state == SOFTWARE_TIMER_STATE_RUNNING, "timer transitions to RUNNING after start");

    softwareTimerGetRemainingTicks(&pool, handle, &remaining);
    TEST_ASSERT(remaining == 10U, "remaining ticks equal the requested timeout right after start");

    /* negative cases */
    TEST_ASSERT(softwareTimerStart(&pool, handle, 0U) == SOFTWARE_TIMER_ERROR_INVALID_TIMEOUT,
                "start with a timeout of 0 returns SOFTWARE_TIMER_ERROR_INVALID_TIMEOUT");
    TEST_ASSERT(softwareTimerStart(NULL, handle, 10U) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "start with NULL pool pointer returns SOFTWARE_TIMER_ERROR_NULL_POINTER");

    badHandle.index = 0U;
    badHandle.generation = (uint8_t)(handle.generation + 1U);
    TEST_ASSERT(softwareTimerStart(&pool, badHandle, 10U) == SOFTWARE_TIMER_ERROR_INVALID_HANDLE,
                "start with a stale-generation handle returns SOFTWARE_TIMER_ERROR_INVALID_HANDLE");

    badHandle.index = (uint8_t)(pool.capacity); /* out of range */
    badHandle.generation = 0U;
    TEST_ASSERT(softwareTimerStart(&pool, badHandle, 10U) == SOFTWARE_TIMER_ERROR_INVALID_HANDLE,
                "start with an out-of-range slot index returns SOFTWARE_TIMER_ERROR_INVALID_HANDLE");
}

static void testStartRestartsRunningTimer(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[1];
    TimerHandle_t handle;
    uint32_t remaining = 0U;

    printf("\nsoftwareTimerStart on an already-running timer restarts it (not an error):\n");

    softwareTimerInit(&pool, storage, 1U);
    softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, &handle);

    softwareTimerStart(&pool, handle, 10U);
    softwareTimerTick(&pool); /* remaining now 9 */
    softwareTimerTick(&pool); /* remaining now 8 */

    TEST_ASSERT(softwareTimerStart(&pool, handle, 20U) == SOFTWARE_TIMER_SUCCESS,
                "re-starting a running timer succeeds rather than returning an error");

    softwareTimerGetRemainingTicks(&pool, handle, &remaining);
    TEST_ASSERT(remaining == 20U, "restarting resets remaining to the new timeout, discarding prior progress");
}

/* -------------------------------------------------------------------------
 * softwareTimerStop
 * ---------------------------------------------------------------------- */

static void testStop(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[1];
    TimerHandle_t handle, badHandle;
    SoftwareTimerState_t state;
    uint32_t remaining = 0U;

    printf("\nsoftwareTimerStop:\n");

    softwareTimerInit(&pool, storage, 1U);
    softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, &handle);
    softwareTimerStart(&pool, handle, 10U);

    TEST_ASSERT(softwareTimerStop(&pool, handle) == SOFTWARE_TIMER_SUCCESS, "stop on a running timer succeeds");

    softwareTimerGetState(&pool, handle, &state);
    TEST_ASSERT(state == SOFTWARE_TIMER_STATE_STOPPED, "timer transitions to STOPPED after stop");

    softwareTimerGetRemainingTicks(&pool, handle, &remaining);
    TEST_ASSERT(remaining == 0U, "remaining ticks reset to 0 after stop");

    TEST_ASSERT(softwareTimerStop(&pool, handle) == SOFTWARE_TIMER_SUCCESS,
                "stopping an already-stopped timer is an idempotent no-op, not an error");

    /* negative cases */
    TEST_ASSERT(softwareTimerStop(NULL, handle) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "stop with NULL pool pointer returns SOFTWARE_TIMER_ERROR_NULL_POINTER");

    badHandle.index = 0U;
    badHandle.generation = (uint8_t)(handle.generation + 1U);
    TEST_ASSERT(softwareTimerStop(&pool, badHandle) == SOFTWARE_TIMER_ERROR_INVALID_HANDLE,
                "stop with a stale-generation handle returns SOFTWARE_TIMER_ERROR_INVALID_HANDLE");
}

/* -------------------------------------------------------------------------
 * softwareTimerDelete
 * ---------------------------------------------------------------------- */

static void testDelete(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[1];
    TimerHandle_t handle, staleHandle, recreatedHandle;

    printf("\nsoftwareTimerDelete:\n");

    softwareTimerInit(&pool, storage, 1U);
    softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, &handle);
    softwareTimerStart(&pool, handle, 10U);

    staleHandle = handle;

    TEST_ASSERT(softwareTimerDelete(&pool, handle) == SOFTWARE_TIMER_SUCCESS, "delete on a live timer succeeds");

    TEST_ASSERT(softwareTimerStart(&pool, staleHandle, 5U) == SOFTWARE_TIMER_ERROR_INVALID_HANDLE,
                "using the now-deleted handle afterward returns SOFTWARE_TIMER_ERROR_INVALID_HANDLE");

    TEST_ASSERT(softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, &recreatedHandle) == SOFTWARE_TIMER_SUCCESS,
                "the freed slot can be reused by a subsequent create");
    TEST_ASSERT(recreatedHandle.index == staleHandle.index, "the reused slot has the same index as before");
    TEST_ASSERT(recreatedHandle.generation != staleHandle.generation,
                "the reused slot's generation differs, so the old handle stays stale even by coincidence");

    TEST_ASSERT(softwareTimerStart(&pool, staleHandle, 5U) == SOFTWARE_TIMER_ERROR_INVALID_HANDLE,
                "the original stale handle is still rejected even after the slot is reused by a new timer");

    /* negative cases */
    TEST_ASSERT(softwareTimerDelete(NULL, recreatedHandle) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "delete with NULL pool pointer returns SOFTWARE_TIMER_ERROR_NULL_POINTER");
    TEST_ASSERT(softwareTimerDelete(&pool, staleHandle) == SOFTWARE_TIMER_ERROR_INVALID_HANDLE,
                "delete with an already-deleted (stale) handle returns SOFTWARE_TIMER_ERROR_INVALID_HANDLE");
}

/* -------------------------------------------------------------------------
 * softwareTimerTick -- one-shot behavior
 * ---------------------------------------------------------------------- */

static void testTickOneShotExpiry(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[1];
    TimerHandle_t handle;
    SoftwareTimerState_t state;
    uint32_t remaining = 0U;
    int i;

    printf("\nsoftwareTimerTick: one-shot timer expiry:\n");

    resetCallbackLog();
    softwareTimerInit(&pool, storage, 1U);
    softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, &handle);
    softwareTimerStart(&pool, handle, 3U);

    for (i = 0; i < 2; i++)
    {
        TEST_ASSERT(softwareTimerTick(&pool) == SOFTWARE_TIMER_SUCCESS, "tick before expiry succeeds");
        TEST_ASSERT(callbackFireCount == 0, "callback has not fired before the timeout is reached");
    }

    softwareTimerTick(&pool); /* 3rd tick: remaining hits 0 */

    TEST_ASSERT(callbackFireCount == 1, "callback fires exactly once when remaining reaches 0");
    TEST_ASSERT(firedOrder[0].index == handle.index, "the handle passed to the callback matches the fired timer");

    softwareTimerGetState(&pool, handle, &state);
    TEST_ASSERT(state == SOFTWARE_TIMER_STATE_STOPPED, "a one-shot timer transitions to STOPPED after firing");

    softwareTimerGetRemainingTicks(&pool, handle, &remaining);
    TEST_ASSERT(remaining == 0U, "a stopped one-shot timer reports 0 remaining ticks after firing");

    softwareTimerTick(&pool);
    TEST_ASSERT(callbackFireCount == 1, "a one-shot timer does not fire again on subsequent ticks once stopped");
}

static void testTickOneShotContextPassedThrough(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[1];
    TimerHandle_t handle;
    int myContext = 42;

    printf("\nsoftwareTimerTick: context pointer is passed through to the callback:\n");

    resetCallbackLog();
    softwareTimerInit(&pool, storage, 1U);
    softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, &myContext, &handle);
    softwareTimerStart(&pool, handle, 1U);

    softwareTimerTick(&pool);

    TEST_ASSERT(callbackFireCount == 1, "callback fired once");
    TEST_ASSERT(firedContext[0] == &myContext, "the context pointer supplied at create is passed to the callback");
}

/* -------------------------------------------------------------------------
 * softwareTimerTick -- periodic behavior
 * ---------------------------------------------------------------------- */

static void testTickPeriodicReloadsAndKeepsRunning(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[1];
    TimerHandle_t handle;
    SoftwareTimerState_t state;
    uint32_t remaining = 0U;

    printf("\nsoftwareTimerTick: periodic timer reloads before the callback and stays RUNNING:\n");

    resetCallbackLog();
    softwareTimerInit(&pool, storage, 1U);
    softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_PERIODIC, basicCallback, NULL, &handle);
    softwareTimerStart(&pool, handle, 2U);

    softwareTimerTick(&pool); /* remaining: 2 -> 1 */
    softwareTimerTick(&pool); /* remaining: 1 -> 0, fires, reloads to 2 */

    TEST_ASSERT(callbackFireCount == 1, "periodic callback fires once its remaining count reaches 0");

    softwareTimerGetState(&pool, handle, &state);
    TEST_ASSERT(state == SOFTWARE_TIMER_STATE_RUNNING, "a periodic timer remains RUNNING after firing");

    softwareTimerGetRemainingTicks(&pool, handle, &remaining);
    TEST_ASSERT(remaining == 2U, "a periodic timer's remaining count is reloaded to its original timeout after firing");

    softwareTimerTick(&pool);
    softwareTimerTick(&pool);
    TEST_ASSERT(callbackFireCount == 2, "a periodic timer fires again after another full period elapses");
}

static void testTickPeriodicSelfStopFromCallback(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[1];
    TimerHandle_t handle;
    SoftwareTimerState_t state;

    printf("\nsoftwareTimerTick: a periodic timer's callback may safely stop itself:\n");

    resetCallbackLog();
    softwareTimerInit(&pool, storage, 1U);
    callbackPool = &pool;
    softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_PERIODIC, selfStoppingCallback, NULL, &handle);
    selfStopHandle = handle;
    softwareTimerStart(&pool, handle, 1U);

    softwareTimerTick(&pool); /* fires and stops itself from within the callback */

    TEST_ASSERT(callbackFireCount == 1, "the self-stopping callback fired exactly once");

    softwareTimerGetState(&pool, handle, &state);
    TEST_ASSERT(state == SOFTWARE_TIMER_STATE_STOPPED,
                "the timer is STOPPED after its own callback stops it, overriding the reload-to-RUNNING");

    softwareTimerTick(&pool);
    TEST_ASSERT(callbackFireCount == 1, "the timer no longer fires on subsequent ticks after stopping itself");
}

static void testTickSelfDeleteFromCallback(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[1];
    TimerHandle_t handle;

    printf("\nsoftwareTimerTick: a timer's callback may safely delete itself:\n");

    resetCallbackLog();
    softwareTimerInit(&pool, storage, 1U);
    callbackPool = &pool;
    softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, selfDeletingCallback, NULL, &handle);
    softwareTimerStart(&pool, handle, 1U);

    TEST_ASSERT(softwareTimerTick(&pool) == SOFTWARE_TIMER_SUCCESS,
                "tick completes successfully even when its callback deletes the firing timer");
    TEST_ASSERT(callbackFireCount == 1, "the self-deleting callback fired exactly once");

    TEST_ASSERT(softwareTimerGetState(&pool, handle, &(SoftwareTimerState_t){0}) == SOFTWARE_TIMER_ERROR_INVALID_HANDLE,
                "querying the handle after it deleted itself returns SOFTWARE_TIMER_ERROR_INVALID_HANDLE");
}

static void testTickCrossTimerMutationFromCallback(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[2];
    TimerHandle_t triggerHandle, targetHandle;
    SoftwareTimerState_t state;

    printf("\nsoftwareTimerTick: a callback may safely start a different timer in the same pool:\n");

    resetCallbackLog();
    softwareTimerInit(&pool, storage, 2U);
    callbackPool = &pool;

    softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, restartsOtherTimerCallback, NULL, &triggerHandle);
    softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, &targetHandle);
    otherTimerHandle = targetHandle;

    softwareTimerStart(&pool, triggerHandle, 1U);
    /* targetHandle is deliberately left STOPPED until the trigger fires */

    softwareTimerTick(&pool);

    softwareTimerGetState(&pool, targetHandle, &state);
    TEST_ASSERT(state == SOFTWARE_TIMER_STATE_RUNNING,
                "a timer started from within another timer's callback (same tick) is RUNNING afterward");
}

static void testTickAscendingSlotOrder(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[4];
    TimerHandle_t handles[4];
    size_t i;

    printf("\nsoftwareTimerTick: simultaneous expiries fire in ascending slot-index order:\n");

    resetCallbackLog();
    softwareTimerInit(&pool, storage, 4U);

    /* Create in reverse conceptual order but all expiring on the same tick,
     * to confirm firing order follows slot index, not creation or handle
     * order. */
    for (i = 0U; i < 4U; i++)
    {
        softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, &handles[i]);
        softwareTimerStart(&pool, handles[i], 1U);
    }

    softwareTimerTick(&pool);

    TEST_ASSERT(callbackFireCount == 4, "all four simultaneously-expiring timers fired");
    TEST_ASSERT((firedOrder[0].index == 0U) && (firedOrder[1].index == 1U) && (firedOrder[2].index == 2U) && (firedOrder[3].index == 3U),
                "callbacks fire in ascending slot-index order when multiple timers expire on the same tick");
}

static void testTickIgnoresStoppedAndUnusedSlots(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[3];
    TimerHandle_t running, stopped;

    printf("\nsoftwareTimerTick: STOPPED and UNUSED slots are not decremented or fired:\n");

    resetCallbackLog();
    softwareTimerInit(&pool, storage, 3U); /* slot 2 stays UNUSED (never created) */

    softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, &running);
    softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, &stopped);

    softwareTimerStart(&pool, running, 1U);
    /* `stopped` is created but never started -- stays SOFTWARE_TIMER_STATE_STOPPED */

    TEST_ASSERT(softwareTimerTick(&pool) == SOFTWARE_TIMER_SUCCESS, "tick with mixed slot states succeeds");
    TEST_ASSERT(callbackFireCount == 1, "only the RUNNING timer fires; STOPPED and UNUSED slots are untouched");
}

/* -------------------------------------------------------------------------
 * softwareTimerTick -- negative cases
 * ---------------------------------------------------------------------- */

static void testTickNullPointer(void)
{
    printf("\nsoftwareTimerTick negative: NULL pool:\n");

    TEST_ASSERT(softwareTimerTick(NULL) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "tick with NULL pool pointer returns SOFTWARE_TIMER_ERROR_NULL_POINTER");
}

/* -------------------------------------------------------------------------
 * softwareTimerGetState / softwareTimerGetRemainingTicks
 * ---------------------------------------------------------------------- */

static void testGetState(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[1];
    TimerHandle_t handle, badHandle;
    SoftwareTimerState_t state;

    printf("\nsoftwareTimerGetState:\n");

    softwareTimerInit(&pool, storage, 1U);
    softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, &handle);

    TEST_ASSERT(softwareTimerGetState(&pool, handle, &state) == SOFTWARE_TIMER_SUCCESS,
                "getState on a valid, live handle succeeds");
    TEST_ASSERT(state == SOFTWARE_TIMER_STATE_STOPPED, "state correctly reported as STOPPED before start");

    /* negative cases */
    TEST_ASSERT(softwareTimerGetState(&pool, handle, NULL) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "getState with NULL outState pointer returns SOFTWARE_TIMER_ERROR_NULL_POINTER");
    TEST_ASSERT(softwareTimerGetState(NULL, handle, &state) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "getState with NULL pool pointer returns SOFTWARE_TIMER_ERROR_NULL_POINTER");

    badHandle.index = 0U;
    badHandle.generation = (uint8_t)(handle.generation + 1U);
    TEST_ASSERT(softwareTimerGetState(&pool, badHandle, &state) == SOFTWARE_TIMER_ERROR_INVALID_HANDLE,
                "getState with a stale-generation handle returns SOFTWARE_TIMER_ERROR_INVALID_HANDLE");

    badHandle.index = 5U; /* out of range for capacity 1 */
    badHandle.generation = 0U;
    TEST_ASSERT(softwareTimerGetState(&pool, badHandle, &state) == SOFTWARE_TIMER_ERROR_INVALID_HANDLE,
                "getState with an out-of-range slot index returns SOFTWARE_TIMER_ERROR_INVALID_HANDLE");
}

static void testGetRemainingTicks(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[1];
    TimerHandle_t handle;
    uint32_t remaining = 0U;

    printf("\nsoftwareTimerGetRemainingTicks:\n");

    softwareTimerInit(&pool, storage, 1U);
    softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, &handle);

    softwareTimerGetRemainingTicks(&pool, handle, &remaining);
    TEST_ASSERT(remaining == 0U, "an unstarted timer reports 0 remaining ticks");

    softwareTimerStart(&pool, handle, 7U);
    softwareTimerGetRemainingTicks(&pool, handle, &remaining);
    TEST_ASSERT(remaining == 7U, "remaining ticks reflects the timeout right after start");

    softwareTimerTick(&pool);
    softwareTimerGetRemainingTicks(&pool, handle, &remaining);
    TEST_ASSERT(remaining == 6U, "remaining ticks decreases by 1 per tick while running");

    /* negative cases */
    TEST_ASSERT(softwareTimerGetRemainingTicks(&pool, handle, NULL) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "getRemainingTicks with NULL outRemaining pointer returns SOFTWARE_TIMER_ERROR_NULL_POINTER");
    TEST_ASSERT(softwareTimerGetRemainingTicks(NULL, handle, &remaining) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "getRemainingTicks with NULL pool pointer returns SOFTWARE_TIMER_ERROR_NULL_POINTER");
}

/* -------------------------------------------------------------------------
 * Cross-cutting negative test: uninitialized / zero-capacity pool
 * ---------------------------------------------------------------------- */

static void testZeroCapacityPool(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[1];

    printf("\nnegative: a pool cannot be initialized with capacity 0:\n");

    TEST_ASSERT(softwareTimerInit(&pool, storage, 0U) == SOFTWARE_TIMER_ERROR_INVALID_CAPACITY,
                "init with capacity 0 returns SOFTWARE_TIMER_ERROR_INVALID_CAPACITY");
}

/* -------------------------------------------------------------------------
 * Cross-cutting negative test: NULL pointer handling across the whole API
 * ---------------------------------------------------------------------- */

static void testNullPointerHandling(void)
{
    SoftwareTimerPool_t pool;
    TimerControlBlock_t storage[1];
    TimerHandle_t handle;
    SoftwareTimerState_t state;
    uint32_t remaining = 0U;

    printf("\nnegative: NULL pointer handling across the API:\n");

    softwareTimerInit(&pool, storage, 1U);
    softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, basicCallback, NULL, &handle);

    TEST_ASSERT(softwareTimerStart(NULL, handle, 1U) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "start with NULL pool pointer returns SOFTWARE_TIMER_ERROR_NULL_POINTER");
    TEST_ASSERT(softwareTimerStop(NULL, handle) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "stop with NULL pool pointer returns SOFTWARE_TIMER_ERROR_NULL_POINTER");
    TEST_ASSERT(softwareTimerDelete(NULL, handle) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "delete with NULL pool pointer returns SOFTWARE_TIMER_ERROR_NULL_POINTER");
    TEST_ASSERT(softwareTimerTick(NULL) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "tick with NULL pool pointer returns SOFTWARE_TIMER_ERROR_NULL_POINTER");
    TEST_ASSERT(softwareTimerGetState(NULL, handle, &state) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "getState with NULL pool pointer returns SOFTWARE_TIMER_ERROR_NULL_POINTER");
    TEST_ASSERT(softwareTimerGetRemainingTicks(NULL, handle, &remaining) == SOFTWARE_TIMER_ERROR_NULL_POINTER,
                "getRemainingTicks with NULL pool pointer returns SOFTWARE_TIMER_ERROR_NULL_POINTER");
}

int main(void)
{
    printf("Running swTimer tests...\n");

    testInit();
    testCreate();
    testCreateInitialStateIsStopped();
    testStart();
    testStartRestartsRunningTimer();
    testStop();
    testDelete();
    testTickOneShotExpiry();
    testTickOneShotContextPassedThrough();
    testTickPeriodicReloadsAndKeepsRunning();
    testTickPeriodicSelfStopFromCallback();
    testTickSelfDeleteFromCallback();
    testTickCrossTimerMutationFromCallback();
    testTickAscendingSlotOrder();
    testTickIgnoresStoppedAndUnusedSlots();
    testTickNullPointer();
    testGetState();
    testGetRemainingTicks();
    testZeroCapacityPool();
    testNullPointerHandling();

    printf("\n%d / %d tests passed.\n", passedTests, totalTests);

    return (passedTests == totalTests) ? 0 : 1;
}

/**************************************** END OF testSwTimer.c ****************************************/