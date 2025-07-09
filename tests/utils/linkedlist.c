#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "linkedlist.h"

#define ll_foreach(_NODE, _LIST) for (ll_node* _NODE = ll_first(_LIST); _NODE != NULL; _NODE = ll_next(_NODE))

struct linkedlist {
  ll_node* first;
  ll_node* last;
};

struct ll_node {
  void* data;
  ll_node* next;
  ll_node* prev;
};

static ll_node* ll_create_node(void const* data, size_t size);

linkedlist* ll_init(void) {
  linkedlist* ll;
  assert((ll = malloc(sizeof(*ll))) != NULL);
  ll->first = ll->last = NULL;
  return ll;
}

void ll_destroy(linkedlist* ll) {
  assert(ll);
  ll_node* n;
  void* data;
  while ((n = ll_last(ll)) != NULL) {
    data = ll_data(n);
    free(data);
    ll_pop(ll);
  }
  free(ll);
}

void ll_destroy_custom(linkedlist* ll, void (*destructor)(void*)) {
  assert(ll);
  assert(destructor);
  ll_node* n;
  void* data;
  while ((n = ll_last(ll)) != NULL) {
    data = ll_data(n);
    destructor(data);
    ll_pop(ll);
  }
  free(ll);
}

ll_return_code ll_append(linkedlist* ll, void const* data, size_t size) {
  assert(ll);
  ll_node* n = ll_create_node(data, size);
  if (!ll->first) {
    ll->last = ll->first = n;
  } else {
    n->prev = ll->last;
    ll->last->next = n;
    ll->last = n;
  }
  return LL_OK;
}

ll_return_code ll_pop(linkedlist* ll) {
  assert(ll);
  if (ll->last == NULL) {
    return LL_EMPTY;
  }
  ll_node* pre_last = ll->last->prev;
  free(ll->last);
  if (pre_last) {
    pre_last->next = NULL;
  } else {
    ll->first = NULL;
  }
  ll->last = pre_last;
  return LL_OK;
}

ll_return_code ll_pop_front(linkedlist* ll) {
  assert(ll);
  if (ll->first == NULL) {
    return LL_EMPTY;
  }
  ll_node* next = ll->first->next;
  free(ll->first);
  if (next) {
    next->prev = NULL;
  } else {
    ll->last = NULL;
  }
  ll->first = next;
  return LL_OK;
}

ll_return_code ll_pop_discard(linkedlist* ll) {
  assert(ll);
  if (ll->last == NULL) {
    return LL_EMPTY;
  }
  ll_node* pre_last = ll->last->prev;
  free(ll->last->data);
  free(ll->last);
  if (pre_last) {
    pre_last->next = NULL;
  } else {
    ll->first = NULL;
  }
  ll->last = pre_last;
  return LL_OK;
}

ll_return_code ll_pop_front_discard(linkedlist* ll) {
  assert(ll);
  if (ll->first == NULL) {
    return LL_EMPTY;
  }
  ll_node* next = ll->first->next;
  free(ll->first->data);
  free(ll->first);
  if (next) {
    next->prev = NULL;
  } else {
    ll->last = NULL;
  }
  ll->first = next;
  return LL_OK;
}

ll_node* ll_find(linkedlist* ll, ll_predicate_const_fn_t pred, void const* data) {
  assert(ll);
  assert(pred);
  ll_foreach(n, ll) {
    if (pred(n->data, data)) {
      return n;
    }
  }
  return NULL;
}

ll_node* ll_first(linkedlist* ll) {
  assert(ll);
  return ll->first;
}

ll_node* ll_last(linkedlist* ll) {
  assert(ll);
  return ll->last;
}

ll_node* ll_next(ll_node* node) {
  assert(node);
  return node->next;
}

ll_node* ll_prev(ll_node* node) {
  assert(node);
  return node->prev;
}

void* ll_data(ll_node* node) {
  assert(node);
  return node->data;
}

int ll_is_first(ll_node* node) {
  assert(node);
  return node->prev == NULL;
}

int ll_is_last(ll_node* node) {
  assert(node);
  return node->next == NULL;
}

static ll_node* ll_create_node(void const* data, size_t size) {
  assert(data);
  ll_node* n;
  assert((n = malloc(sizeof(*n))) != NULL);
  assert((n->data = malloc(size)) != NULL);
  memcpy(n->data, data, size);
  n->prev = n->next = NULL;
  return n;
}
