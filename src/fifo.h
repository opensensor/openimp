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

void *fifo_alloc(int count, int element_size);
int fifo_free(void *fifo);
int fifo_put(void *fifo, const void *data);
int fifo_get(void *fifo, void *data);
int fifo_clear(void *fifo);
int fifo_pre_get(void *fifo, void *data);
int fifo_pre_get_ptr(void *fifo, int index, void **data_ptr);
int fifo_num(void *fifo);
int fifo_print(void *fifo);
int fifo_head(void *fifo, void **node, void **data_ptr);
int fifo_node_next(void *fifo, void **node, void **data_ptr);

#ifdef __cplusplus
}
#endif

#endif /* FIFO_H */
