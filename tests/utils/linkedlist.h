#ifndef LVGLWEBSIM_LINKEDLIST_H
#define LVGLWEBSIM_LINKEDLIST_H

#include <assert.h>
#include <stdlib.h>

struct linkedlist;
struct ll_node;

typedef struct linkedlist linkedlist;
typedef struct ll_node ll_node;

typedef int (*ll_predicate_const_fn_t)(void const*, void const*);
typedef void (*ll_destructor_fn_t)(void*);

typedef enum {
  LL_OK = 0,
  LL_EMPTY,
} ll_return_code;

/* Create a linked list */
linkedlist* ll_init(void);

/* Destroy a linked list with all nodes and data */
void ll_destroy(linkedlist* ll);

/* Destroy a linked list with all nodes, use destructor to free the data
 *
 * Destructor is responsible for freeing the data stored in each node.
 */
void ll_destroy_custom(linkedlist* ll, ll_destructor_fn_t destructor);

/* Append a node to a list
 *
 * Node saves the copy of passed data.
 */
ll_return_code ll_append(linkedlist* ll, void const* data, size_t size);

/* Pop a node from the list
 *
 * Frees the memory allocated for the node, not for the node's data. So before
 * calling `ll_pop`, retrieve the data with `ll_last` and `ll_data`:
 *
 * ```
 * void* data = ll_data(ll_last(lst));
 * ll_pop(lst);
 * free(data);
 * ```
 */
ll_return_code ll_pop(linkedlist* ll);

/* Pop the first node of the list
 *
 * Frees the memory allocated for the node, not for the node's data. So before
 * calling `ll_pop_front`, retrive the data pointer with `ll_first` and
 * `ll_data`:
 *
 * ```
 * void* data = ll_data(ll_first(lst));
 * ll_pop_front(lst);
 * free(data);
 * ```
 */
ll_return_code ll_pop_front(linkedlist* ll);

/* Pop a node from the list and free the underlying data
 *
 * Unlike `ll_pop`, does not require the calling side to clean up stored data.
 */
ll_return_code ll_pop_discard(linkedlist* ll);

/* Pop the first node from the list and free the underlying data
 *
 * Unlike `ll_pop_front`, does not require the calling side to clean up stored
 * data.
 */
ll_return_code ll_pop_front_discard(linkedlist* ll);

/* Find and return the node on which data the predicate returned true
 *
 * Returns NULL if the node not found.
 */
ll_node* ll_find(linkedlist* ll, ll_predicate_const_fn_t pred,
                 void const* data);

/* Get the first node of the list or NULL if the list is empty */
ll_node* ll_first(linkedlist* ll);

/* Get the last node of the list or NULL if the list is empty */
ll_node* ll_last(linkedlist* ll);

/* Get the next node of the current node or NULL if it is the last node */
ll_node* ll_next(ll_node* node);

/* Get the previous node of the current node or NULL if it is the first node */
ll_node* ll_prev(ll_node* node);

/* Get the data stored in the node */
void* ll_data(ll_node* node);

/* Check if the node is the first */
int ll_is_first(ll_node* node);

/* Check if the node is the last */
int ll_is_last(ll_node* node);

#endif
