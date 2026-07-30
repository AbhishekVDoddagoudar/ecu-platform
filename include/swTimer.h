/******************************************************************************
 * @file    softwareTimer.h
 * @brief   Public interface for a reentrant, caller-owned software timer pool.
 *
 * @author  Abhishek Doddagoudar
 * @date    July 2026
 *
 * @details
 * Counts ticks on behalf of the application and invokes a callback when a
 * timer's requested duration has elapsed. Supports one-shot and periodic
 * timers. Does not generate its own notion of time, does not sleep or
 * block, and does not know what a tick means in real time -- see
 * docs/softwareTimer-requirements.md for the full responsibility boundary.
 *
 * See docs/softwareTimer-design.md for the full design rationale, in
 * particular:
 *  - Section 2: why timer storage is caller-owned (like ringBuffer) rather
 *    than a single library-global pool.
 *  - Section 3: why TimerHandle_t carries a generation counter.
 *  - Section 4: why periodic timers reload *before* their callback fires,
 *    and why there is no separate EXPIRED state.
 *
 * Features:
 *  - Fixed-capacity, caller-owned timer pool (no dynamic allocation)
 *  - One-shot and periodic timers
 *  - Stale-handle detection via generation counters
 *  - Restart-safe start()/idempotent stop() semantics
 *  - Deterministic same-tick callback ordering (ascending slot index)
 *
 * Thread Safety:
 *  - Reentrant. No internal or global state; all state lives in the
 *    caller-owned SoftwareTimerPool_t and its backing TimerControlBlock_t
 *    storage. Concurrent calls into the *same* pool from independent
 *    execution contexts (e.g. softwareTimerStop() from an ISR while
 *    softwareTimerTick() runs in the main loop) require the caller's own
 *    synchronization.
 *
 * Dynamic Memory:
 *  - None. The caller declares a fixed-size TimerControlBlock_t array and
 *    passes it to softwareTimerInit(); this module never allocates.
 *
 * Timer Accuracy:
 *  - A timer with timeout N expires after exactly N calls to
 *    softwareTimerTick() -- no drift is introduced by this module. Overall
 *    real-time accuracy depends entirely on the regularity of whatever
 *    external code calls softwareTimerTick().
 *
 * Example Usage:
 *
 *      TimerControlBlock_t storage[8];
 *      SoftwareTimerPool_t pool;
 *      TimerHandle_t handle;
 *
 *      softwareTimerInit(&pool, storage, 8);
 *      softwareTimerCreate(&pool, SOFTWARE_TIMER_MODE_ONE_SHOT, myCallback, NULL, &handle);
 *      softwareTimerStart(&pool, handle, 50U);
 *
 *      // once per tick period, from wherever ticks are sourced:
 *      softwareTimerTick(&pool);
 *
 ******************************************************************************/

#ifndef SW_TIMER_H
#define SW_TIMER_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Identifies a single timer within a pool.
 *
 * @details generation guards against stale-handle reuse: if the slot at
 *          index has been deleted and reused for a different timer since
 *          this handle was issued, its generation will no longer match,
 *          and any operation using this handle returns
 *          SOFTWARE_TIMER_ERROR_INVALID_HANDLE instead of silently
 *          operating on the wrong timer. See design doc Section 3.
 */
typedef struct
{
    uint8_t index;
    uint8_t generation;
} TimerHandle_t;

/**
 * @brief Timer lifecycle state.
 *
 * @details No EXPIRED state exists deliberately -- see design doc Section 4
 *          for why an "expired" moment is always an instantaneous
 *          transition within softwareTimerTick(), not an externally
 *          observable resting state.
 */
typedef enum
{
    SOFTWARE_TIMER_STATE_UNUSED = 0,
    SOFTWARE_TIMER_STATE_STOPPED,
    SOFTWARE_TIMER_STATE_RUNNING
} SoftwareTimerState_t;

/**
 * @brief One-shot vs. periodic timer behavior.
 */
typedef enum
{
    SOFTWARE_TIMER_MODE_ONE_SHOT = 0,
    SOFTWARE_TIMER_MODE_PERIODIC
} SoftwareTimerMode_t;

/**
 * @brief Software timer status codes.
 */
typedef enum
{
    SOFTWARE_TIMER_SUCCESS = 0,
    SOFTWARE_TIMER_ERROR_NULL_POINTER,
    SOFTWARE_TIMER_ERROR_INVALID_HANDLE,
    SOFTWARE_TIMER_ERROR_INVALID_TIMEOUT,
    SOFTWARE_TIMER_ERROR_INVALID_MODE,
    SOFTWARE_TIMER_ERROR_INVALID_CAPACITY,
    SOFTWARE_TIMER_ERROR_POOL_FULL
} SoftwareTimerStatus_t;

/**
 * @brief Signature for a timer expiration callback.
 *
 * @details Invoked synchronously, from within softwareTimerTick(), on the
 *          same call whose decrement brought the timer's remaining count
 *          to zero. May safely start/stop any timer in the same pool,
 *          including the one currently firing -- see design doc Section 4
 *          for why reload-before-callback ordering makes this safe for
 *          periodic timers specifically.
 *
 * @param handle  The handle of the timer that just expired.
 * @param context The context pointer supplied at softwareTimerCreate().
 */
typedef void (*SoftwareTimerCallback_t)(TimerHandle_t handle, void *context);

/**
 * @brief A single timer's control block.
 *
 * @details Caller-owned; an array of these is what backs a
 *          SoftwareTimerPool_t. Callers should treat the contents as
 *          opaque and only interact with timers via the API below --
 *          direct field access is not part of the public contract and may
 *          change between versions.
 */
typedef struct
{
    SoftwareTimerState_t state;
    SoftwareTimerMode_t mode;
    uint32_t timeout;   /**< Original duration in ticks; reload target for periodic timers. */
    uint32_t remaining; /**< Ticks left before next expiry.                                  */
    SoftwareTimerCallback_t callback;
    void *context;
    uint8_t generation;
} TimerControlBlock_t;

/**
 * @brief A fixed-capacity pool of timers, backed by caller-owned storage.
 *
 * @details The caller owns both this object and the backing
 *          TimerControlBlock_t array -- no dynamic allocation occurs. See
 *          design doc Section 2 for why storage is caller-owned rather
 *          than a single library-global pool.
 */
typedef struct
{
    TimerControlBlock_t *storage;
    size_t capacity;
} SoftwareTimerPool_t;

/**
 * @brief Initialize a timer pool.
 *
 * @details Marks every slot in storage as SOFTWARE_TIMER_STATE_UNUSED.
 *
 * @param pool     Pointer to the pool object to initialize.
 * @param storage  Caller-provided array of TimerControlBlock_t.
 * @param capacity Number of elements in storage.
 *
 * @return SoftwareTimerStatus_t Status of the operation.
 *
 * @retval SOFTWARE_TIMER_SUCCESS Initialization successful.
 * @retval SOFTWARE_TIMER_ERROR_NULL_POINTER pool or storage is NULL.
 * @retval SOFTWARE_TIMER_ERROR_INVALID_CAPACITY capacity is 0.
 */
SoftwareTimerStatus_t softwareTimerInit(SoftwareTimerPool_t *pool, TimerControlBlock_t *storage, size_t capacity);

/**
 * @brief Create a timer within a pool.
 *
 * @details Allocates a free (SOFTWARE_TIMER_STATE_UNUSED) slot and records
 *          its mode, callback, and context. The created timer starts in
 *          SOFTWARE_TIMER_STATE_STOPPED -- it does not begin counting down
 *          until softwareTimerStart() is called. See design doc Section 7
 *          for why timeout is not supplied here.
 *
 * @param pool      Pointer to the timer pool.
 * @param mode      One-shot or periodic.
 * @param callback  Function to invoke on expiry. Must not be NULL.
 * @param context   Caller-defined pointer passed to callback on expiry;
 *                  may be NULL if unused.
 * @param outHandle Pointer to store the handle of the created timer.
 *
 * @return SoftwareTimerStatus_t Status of the operation.
 *
 * @retval SOFTWARE_TIMER_SUCCESS Timer created; *outHandle is valid.
 * @retval SOFTWARE_TIMER_ERROR_NULL_POINTER pool, callback, or outHandle is NULL.
 * @retval SOFTWARE_TIMER_ERROR_INVALID_MODE mode is neither one-shot nor periodic.
 * @retval SOFTWARE_TIMER_ERROR_POOL_FULL No free slot is available.
 */
SoftwareTimerStatus_t softwareTimerCreate(SoftwareTimerPool_t *pool, SoftwareTimerMode_t mode,
                                          SoftwareTimerCallback_t callback, void *context,
                                          TimerHandle_t *outHandle);

/**
 * @brief Start (or restart) a timer.
 *
 * @details Sets remaining to timeoutTicks and transitions the timer to
 *          SOFTWARE_TIMER_STATE_RUNNING, regardless of its current state.
 *          Calling this on an already-running timer is not an error -- it
 *          restarts the countdown from timeoutTicks. See design doc
 *          Section 1 (#1) for why restart, not error, is correct here.
 *
 * @param pool         Pointer to the timer pool.
 * @param handle       Handle of the timer to start.
 * @param timeoutTicks Number of ticks before this timer next expires.
 *
 * @return SoftwareTimerStatus_t Status of the operation.
 *
 * @retval SOFTWARE_TIMER_SUCCESS Timer (re)started successfully.
 * @retval SOFTWARE_TIMER_ERROR_NULL_POINTER pool is NULL.
 * @retval SOFTWARE_TIMER_ERROR_INVALID_HANDLE handle does not refer to a
 *         currently live timer (never created, deleted, or stale
 *         generation -- see TimerHandle_t).
 * @retval SOFTWARE_TIMER_ERROR_INVALID_TIMEOUT timeoutTicks is 0.
 */
SoftwareTimerStatus_t softwareTimerStart(SoftwareTimerPool_t *pool, TimerHandle_t handle, uint32_t timeoutTicks);

/**
 * @brief Stop a timer.
 *
 * @details Transitions the timer to SOFTWARE_TIMER_STATE_STOPPED. Calling
 *          this on an already-stopped timer is not an error -- it is an
 *          idempotent no-op. See design doc Section 8.
 *
 * @param pool   Pointer to the timer pool.
 * @param handle Handle of the timer to stop.
 *
 * @return SoftwareTimerStatus_t Status of the operation.
 *
 * @retval SOFTWARE_TIMER_SUCCESS Timer is now stopped (or already was).
 * @retval SOFTWARE_TIMER_ERROR_NULL_POINTER pool is NULL.
 * @retval SOFTWARE_TIMER_ERROR_INVALID_HANDLE handle does not refer to a
 *         currently live timer.
 */
SoftwareTimerStatus_t softwareTimerStop(SoftwareTimerPool_t *pool, TimerHandle_t handle);

/**
 * @brief Delete a timer, releasing its slot for reuse.
 *
 * @details Implies stop (any state transitions directly to
 *          SOFTWARE_TIMER_STATE_UNUSED) and increments the slot's
 *          generation counter, invalidating handle and any other copies
 *          of it -- see TimerHandle_t and design doc Section 3.
 *
 * @param pool   Pointer to the timer pool.
 * @param handle Handle of the timer to delete.
 *
 * @return SoftwareTimerStatus_t Status of the operation.
 *
 * @retval SOFTWARE_TIMER_SUCCESS Timer deleted; handle is now stale.
 * @retval SOFTWARE_TIMER_ERROR_NULL_POINTER pool is NULL.
 * @retval SOFTWARE_TIMER_ERROR_INVALID_HANDLE handle does not refer to a
 *         currently live timer.
 */
SoftwareTimerStatus_t softwareTimerDelete(SoftwareTimerPool_t *pool, TimerHandle_t handle);

/**
 * @brief Advance every running timer in the pool by exactly one tick.
 *
 * @details Decrements the remaining count of every SOFTWARE_TIMER_STATE_RUNNING
 *          timer in the pool by 1. Any timer whose remaining count reaches
 *          0 as a direct result of this decrement fires synchronously,
 *          within this same call: a one-shot timer's callback fires and it
 *          transitions to SOFTWARE_TIMER_STATE_STOPPED; a periodic timer
 *          reloads (remaining = timeout, stays SOFTWARE_TIMER_STATE_RUNNING)
 *          and then its callback fires -- see design doc Section 4 for why
 *          reload happens before, not after, the callback.
 *
 *          If multiple timers expire as a result of the same call, their
 *          callbacks fire in ascending slot-index order -- see design doc
 *          Section 11.
 *
 *          A callback invoked during this call may safely start, stop, or
 *          delete any timer in this pool, including the one currently
 *          firing.
 *
 * @param pool Pointer to the timer pool.
 *
 * @return SoftwareTimerStatus_t Status of the operation.
 *
 * @retval SOFTWARE_TIMER_SUCCESS Tick processed successfully.
 * @retval SOFTWARE_TIMER_ERROR_NULL_POINTER pool is NULL.
 */
SoftwareTimerStatus_t softwareTimerTick(SoftwareTimerPool_t *pool);

/**
 * @brief Query a timer's current state.
 *
 * @param pool     Pointer to the timer pool.
 * @param handle   Handle of the timer to query.
 * @param outState Pointer to store the timer's current state.
 *
 * @return SoftwareTimerStatus_t Status of the operation.
 *
 * @retval SOFTWARE_TIMER_SUCCESS *outState has been populated.
 * @retval SOFTWARE_TIMER_ERROR_NULL_POINTER pool or outState is NULL.
 * @retval SOFTWARE_TIMER_ERROR_INVALID_HANDLE handle does not refer to a
 *         currently live timer.
 */
SoftwareTimerStatus_t softwareTimerGetState(const SoftwareTimerPool_t *pool, TimerHandle_t handle,
                                            SoftwareTimerState_t *outState);

/**
 * @brief Query the number of ticks remaining before a timer next expires.
 *
 * @param pool          Pointer to the timer pool.
 * @param handle        Handle of the timer to query.
 * @param outRemaining  Pointer to store the remaining tick count. 0 if the
 *                      timer is SOFTWARE_TIMER_STATE_STOPPED or
 *                      SOFTWARE_TIMER_STATE_UNUSED.
 *
 * @return SoftwareTimerStatus_t Status of the operation.
 *
 * @retval SOFTWARE_TIMER_SUCCESS *outRemaining has been populated.
 * @retval SOFTWARE_TIMER_ERROR_NULL_POINTER pool or outRemaining is NULL.
 * @retval SOFTWARE_TIMER_ERROR_INVALID_HANDLE handle does not refer to a
 *         currently live timer.
 */
SoftwareTimerStatus_t softwareTimerGetRemainingTicks(const SoftwareTimerPool_t *pool, TimerHandle_t handle,
                                                     uint32_t *outRemaining);

#endif /* SOFTWARE_TIMER_H */

/******************************* END OF FILE **********************************/