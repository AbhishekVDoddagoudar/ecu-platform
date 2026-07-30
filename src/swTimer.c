/******************************************************************************
 * @file   softwareTimer.c
 * @brief  Implementation of the reentrant, caller-owned software timer pool.
 *
 * @author Abhishek Doddagoudar
 * @date   July 2026
 ******************************************************************************/

#include <swTimer.h>

/**
 * @internal
 * @brief Validates a handle against a pool and, on success, returns the slot it refers to.
 *
 * @details Checks: pool (and its backing storage) is non-NULL, handle.index
 *          is within pool->capacity, the slot at that index is not
 *          SOFTWARE_TIMER_STATE_UNUSED, and the slot's current generation
 *          matches handle.generation (rejecting a stale handle left over
 *          from a deleted-and-reused slot -- see design doc Section 3).
 *
 * @param pool     Pool to validate against.
 * @param handle   Handle to validate.
 * @param outSlot  On success, set to point at the slot handle refers to.
 *
 * @return SoftwareTimerStatus_t
 * @retval SOFTWARE_TIMER_SUCCESS handle is valid; *outSlot is set.
 * @retval SOFTWARE_TIMER_ERROR_NULL_POINTER pool or pool->storage is NULL.
 * @retval SOFTWARE_TIMER_ERROR_INVALID_HANDLE index out of range, slot
 *         unused, or generation mismatch.
 */
static SoftwareTimerStatus_t validateHandle(const SoftwareTimerPool_t *pool, TimerHandle_t handle,
                                            TimerControlBlock_t **outSlot)
{
    TimerControlBlock_t *slot;

    if ((pool == NULL) || (pool->storage == NULL) || outSlot == NULL)
    {
        return SOFTWARE_TIMER_ERROR_NULL_POINTER;
    }

    if (handle.index >= pool->capacity)
    {
        return SOFTWARE_TIMER_ERROR_INVALID_HANDLE;
    }

    slot = &pool->storage[handle.index];

    if ((slot->state == SOFTWARE_TIMER_STATE_UNUSED) || (slot->generation != handle.generation))
    {
        return SOFTWARE_TIMER_ERROR_INVALID_HANDLE;
    }

    *outSlot = slot;
    return SOFTWARE_TIMER_SUCCESS;
}

/**
 * @brief Initialize a timer pool.
 *
 * @details Marks every slot in storage as SOFTWARE_TIMER_STATE_UNUSED and
 *          zeroes the remaining fields, so no uninitialized memory is ever
 *          reachable through the public API regardless of what garbage
 *          storage held before this call.
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
SoftwareTimerStatus_t softwareTimerInit(SoftwareTimerPool_t *pool, TimerControlBlock_t *storage, size_t capacity)
{
    size_t i;

    if ((pool == NULL) || (storage == NULL))
    {
        return SOFTWARE_TIMER_ERROR_NULL_POINTER;
    }

    if (capacity == 0U)
    {
        return SOFTWARE_TIMER_ERROR_INVALID_CAPACITY;
    }

    for (i = 0U; i < capacity; i++)
    {
        storage[i].state = SOFTWARE_TIMER_STATE_UNUSED;
        storage[i].mode = SOFTWARE_TIMER_MODE_ONE_SHOT;
        storage[i].timeout = 0U;
        storage[i].remaining = 0U;
        storage[i].callback = NULL;
        storage[i].context = NULL;
        storage[i].generation = 0U;
    }

    pool->storage = storage;
    pool->capacity = capacity;

    return SOFTWARE_TIMER_SUCCESS;
}

/**
 * @brief Create a timer within a pool.
 *
 * @details Scans for a free (SOFTWARE_TIMER_STATE_UNUSED) slot and records
 *          its mode, callback, and context. The created timer starts in
 *          SOFTWARE_TIMER_STATE_STOPPED -- it does not begin counting down
 *          until softwareTimerStart() is called. The handle's generation is
 *          read from whatever the slot currently holds (0 for a
 *          freshly-initialized slot, or whatever softwareTimerDelete() last
 *          left it at for a reused slot) -- generation is only ever bumped
 *          by delete, never touched here. See design doc Section 7 for why
 *          timeout is not supplied at creation.
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
 * @retval SOFTWARE_TIMER_ERROR_NULL_POINTER pool, pool->storage, callback,
 *         or outHandle is NULL.
 * @retval SOFTWARE_TIMER_ERROR_INVALID_MODE mode is neither one-shot nor periodic.
 * @retval SOFTWARE_TIMER_ERROR_POOL_FULL No free slot is available.
 */
SoftwareTimerStatus_t softwareTimerCreate(SoftwareTimerPool_t *pool, SoftwareTimerMode_t mode,
                                          SoftwareTimerCallback_t callback, void *context,
                                          TimerHandle_t *outHandle)
{
    size_t i;

    if ((pool == NULL) || (pool->storage == NULL) || (callback == NULL) || (outHandle == NULL))
    {
        return SOFTWARE_TIMER_ERROR_NULL_POINTER;
    }

    if ((mode != SOFTWARE_TIMER_MODE_ONE_SHOT) && (mode != SOFTWARE_TIMER_MODE_PERIODIC))
    {
        return SOFTWARE_TIMER_ERROR_INVALID_MODE;
    }

    for (i = 0U; i < pool->capacity; i++)
    {
        TimerControlBlock_t *slot = &pool->storage[i];

        if (slot->state == SOFTWARE_TIMER_STATE_UNUSED)
        {
            slot->mode = mode;
            slot->callback = callback;
            slot->context = context;
            slot->timeout = 0U;
            slot->remaining = 0U;
            slot->state = SOFTWARE_TIMER_STATE_STOPPED;

            outHandle->index = (uint8_t)i;
            outHandle->generation = slot->generation;

            return SOFTWARE_TIMER_SUCCESS;
        }
    }

    return SOFTWARE_TIMER_ERROR_POOL_FULL;
}

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
 * @retval SOFTWARE_TIMER_ERROR_NULL_POINTER pool or pool->storage is NULL.
 * @retval SOFTWARE_TIMER_ERROR_INVALID_HANDLE handle does not refer to a
 *         currently live timer (never created, deleted, or stale
 *         generation -- see TimerHandle_t).
 * @retval SOFTWARE_TIMER_ERROR_INVALID_TIMEOUT timeoutTicks is 0.
 */
SoftwareTimerStatus_t softwareTimerStart(SoftwareTimerPool_t *pool, TimerHandle_t handle, uint32_t timeoutTicks)
{
    TimerControlBlock_t *slot = NULL;
    SoftwareTimerStatus_t status = validateHandle(pool, handle, &slot);

    if (status != SOFTWARE_TIMER_SUCCESS)
    {
        return status;
    }

    if (timeoutTicks == 0U)
    {
        return SOFTWARE_TIMER_ERROR_INVALID_TIMEOUT;
    }

    slot->timeout = timeoutTicks;
    slot->remaining = timeoutTicks;
    slot->state = SOFTWARE_TIMER_STATE_RUNNING;

    return SOFTWARE_TIMER_SUCCESS;
}

/**
 * @brief Stop a timer.
 *
 * @details Transitions the timer to SOFTWARE_TIMER_STATE_STOPPED and resets
 *          remaining to 0. Calling this on an already-stopped timer is not
 *          an error -- it is an idempotent no-op. See design doc Section 8
 *          for why "valid timer already in the target state" is treated
 *          differently from "invalid handle."
 *
 * @param pool   Pointer to the timer pool.
 * @param handle Handle of the timer to stop.
 *
 * @return SoftwareTimerStatus_t Status of the operation.
 *
 * @retval SOFTWARE_TIMER_SUCCESS Timer is now stopped (or already was).
 * @retval SOFTWARE_TIMER_ERROR_NULL_POINTER pool or pool->storage is NULL.
 * @retval SOFTWARE_TIMER_ERROR_INVALID_HANDLE handle does not refer to a
 *         currently live timer.
 */
SoftwareTimerStatus_t softwareTimerStop(SoftwareTimerPool_t *pool, TimerHandle_t handle)
{
    TimerControlBlock_t *slot = NULL;
    SoftwareTimerStatus_t status = validateHandle(pool, handle, &slot);

    if (status != SOFTWARE_TIMER_SUCCESS)
    {
        return status;
    }

    slot->state = SOFTWARE_TIMER_STATE_STOPPED;
    slot->remaining = 0U;

    return SOFTWARE_TIMER_SUCCESS;
}

/**
 * @brief Delete a timer, releasing its slot for reuse.
 *
 * @details Implies stop (any state transitions directly to
 *          SOFTWARE_TIMER_STATE_UNUSED) and increments the slot's
 *          generation counter, which is what invalidates handle and any
 *          other outstanding copies of it -- see TimerHandle_t and design
 *          doc Section 3. generation is a uint8_t, so after 256 delete
 *          cycles on the same slot it wraps and could in principle collide
 *          with a very old stale handle again; this is a bounded, known
 *          limitation of a 1-byte generation counter, not a defect.
 *
 * @param pool   Pointer to the timer pool.
 * @param handle Handle of the timer to delete.
 *
 * @return SoftwareTimerStatus_t Status of the operation.
 *
 * @retval SOFTWARE_TIMER_SUCCESS Timer deleted; handle is now stale.
 * @retval SOFTWARE_TIMER_ERROR_NULL_POINTER pool or pool->storage is NULL.
 * @retval SOFTWARE_TIMER_ERROR_INVALID_HANDLE handle does not refer to a
 *         currently live timer.
 */
SoftwareTimerStatus_t softwareTimerDelete(SoftwareTimerPool_t *pool, TimerHandle_t handle)
{
    TimerControlBlock_t *slot = NULL;
    SoftwareTimerStatus_t status = validateHandle(pool, handle, &slot);

    if (status != SOFTWARE_TIMER_SUCCESS)
    {
        return status;
    }

    slot->state = SOFTWARE_TIMER_STATE_UNUSED;
    slot->timeout = 0U;
    slot->remaining = 0U;
    slot->callback = NULL;
    slot->context = NULL;
    slot->generation++;

    return SOFTWARE_TIMER_SUCCESS;
}

/**
 * @brief Advance every running timer in the pool by exactly one tick.
 *
 * @details Decrements the remaining count of every SOFTWARE_TIMER_STATE_RUNNING
 *          timer by 1. Any timer whose remaining count reaches 0 as a
 *          direct result of that decrement fires synchronously, within this
 *          same call: reload (periodic) or stop (one-shot) happens BEFORE
 *          the callback is invoked, not after -- see design doc Section 4
 *          for why this ordering is what makes it safe for a periodic
 *          timer's callback to call softwareTimerStop() on itself.
 *
 *          callback and context are captured into local variables before
 *          any state mutation and before the call itself, so the call
 *          remains safe even if the callback deletes this exact slot (its
 *          own timer) from within its own invocation.
 *
 *          If multiple timers expire as a result of the same call, their
 *          callbacks fire in ascending slot-index order, a direct
 *          consequence of the single forward scan -- see design doc
 *          Section 11.
 *
 * @param pool Pointer to the timer pool.
 *
 * @return SoftwareTimerStatus_t Status of the operation.
 *
 * @retval SOFTWARE_TIMER_SUCCESS Tick processed successfully.
 * @retval SOFTWARE_TIMER_ERROR_NULL_POINTER pool or pool->storage is NULL.
 */
SoftwareTimerStatus_t softwareTimerTick(SoftwareTimerPool_t *pool)
{
    size_t i;

    if ((pool == NULL) || (pool->storage == NULL))
    {
        return SOFTWARE_TIMER_ERROR_NULL_POINTER;
    }

    for (i = 0U; i < pool->capacity; i++)
    {
        TimerControlBlock_t *slot = &pool->storage[i];

        if (slot->state == SOFTWARE_TIMER_STATE_RUNNING)
        {
            if (slot->remaining > 0U)
            {
                slot->remaining--;
            }

            if (slot->remaining == 0U)
            {
                SoftwareTimerCallback_t callback = slot->callback;
                void *context = slot->context;
                TimerHandle_t firedHandle;

                firedHandle.index = (uint8_t)i;
                firedHandle.generation = slot->generation;

                if (slot->mode == SOFTWARE_TIMER_MODE_PERIODIC)
                {
                    slot->remaining = slot->timeout;
                    /* state stays SOFTWARE_TIMER_STATE_RUNNING */
                }
                else
                {
                    slot->state = SOFTWARE_TIMER_STATE_STOPPED;
                }

                if (callback != NULL) /* defensive: Create requires non-NULL callback; this can't actually be NULL */
                {
                    callback(firedHandle, context);
                }
            }
        }
    }

    return SOFTWARE_TIMER_SUCCESS;
}

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
 * @retval SOFTWARE_TIMER_ERROR_NULL_POINTER outState is NULL, or pool or
 *         pool->storage is NULL.
 * @retval SOFTWARE_TIMER_ERROR_INVALID_HANDLE handle does not refer to a
 *         currently live timer.
 */
SoftwareTimerStatus_t softwareTimerGetState(const SoftwareTimerPool_t *pool, TimerHandle_t handle,
                                            SoftwareTimerState_t *outState)
{
    TimerControlBlock_t *slot = NULL;
    SoftwareTimerStatus_t status;

    if (outState == NULL)
    {
        return SOFTWARE_TIMER_ERROR_NULL_POINTER;
    }

    status = validateHandle(pool, handle, &slot);
    if (status != SOFTWARE_TIMER_SUCCESS)
    {
        return status;
    }

    *outState = slot->state;

    return SOFTWARE_TIMER_SUCCESS;
}

/**
 * @brief Query the number of ticks remaining before a timer next expires.
 *
 * @details Returns 0 if the timer is SOFTWARE_TIMER_STATE_STOPPED (either
 *          explicitly stopped, or a freshly created timer that has never
 *          been started) -- this falls out naturally from softwareTimerStop()
 *          and softwareTimerCreate() both leaving remaining at 0, with no
 *          special-case logic needed here.
 *
 * @param pool          Pointer to the timer pool.
 * @param handle        Handle of the timer to query.
 * @param outRemaining  Pointer to store the remaining tick count.
 *
 * @return SoftwareTimerStatus_t Status of the operation.
 *
 * @retval SOFTWARE_TIMER_SUCCESS *outRemaining has been populated.
 * @retval SOFTWARE_TIMER_ERROR_NULL_POINTER outRemaining is NULL, or pool
 *         or pool->storage is NULL.
 * @retval SOFTWARE_TIMER_ERROR_INVALID_HANDLE handle does not refer to a
 *         currently live timer.
 */
SoftwareTimerStatus_t softwareTimerGetRemainingTicks(const SoftwareTimerPool_t *pool, TimerHandle_t handle,
                                                     uint32_t *outRemaining)
{
    TimerControlBlock_t *slot = NULL;
    SoftwareTimerStatus_t status;

    if (outRemaining == NULL)
    {
        return SOFTWARE_TIMER_ERROR_NULL_POINTER;
    }

    status = validateHandle(pool, handle, &slot);
    if (status != SOFTWARE_TIMER_SUCCESS)
    {
        return status;
    }

    *outRemaining = slot->remaining;

    return SOFTWARE_TIMER_SUCCESS;
}

/*****************************************End of softwareTimer.c*****************************************/