/**
 * Fifo Interface
 * Based on reverse engineering of libimp.so v1.1.6
 */

#ifndef FIFO_H
#define FIFO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Query the size of the FIFO control structure (bytes) */
int Fifo_SizeOf(void);

/**
 * Initialize a FIFO queue
 * @param fifo_ptr Pointer to FIFO control structure memory (must be >= Fifo_SizeOf())
 * @param size Maximum number of elements
 */
void Fifo_Init(void *fifo_ptr, int size);

/**
 * Deinitialize a FIFO queue and free resources
 * @param fifo_ptr Pointer to FIFO control structure memory
 */
void Fifo_Deinit(void *fifo_ptr);

/**
 * Add an item to the FIFO queue
 * @param fifo_ptr Pointer to FIFO control structure memory
 * @param item Item to add
 * @param timeout_ms Timeout in milliseconds (-1 = infinite, 0 = no wait)
 * @return 1 on success, 0 on failure/timeout
 */
int Fifo_Queue(void *fifo_ptr, void *item, int timeout_ms);

/**
 * Remove and return an item from the FIFO queue
 * @param fifo_ptr Pointer to FIFO control structure memory
 * @param timeout_ms Timeout in milliseconds (-1 = infinite, 0 = no wait)
 * @return Item pointer on success, NULL on failure/timeout
 */
void *Fifo_Dequeue(void *fifo_ptr, int timeout_ms);

/**
 * Get the maximum capacity of the FIFO
 * @param fifo_ptr Pointer to FIFO control structure memory
 * @return Maximum number of elements
 */
int Fifo_GetMaxElements(void *fifo_ptr);

#ifdef __cplusplus
}
#endif

#endif /* FIFO_H */

