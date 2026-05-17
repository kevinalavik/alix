#ifndef LIB_RQ_H
#define LIB_RQ_H

#include <stdbool.h>
#include <stddef.h>

typedef struct rq_node {
	void *data;
	struct rq_node *next;
	struct rq_node *prev;
} rq_node_t;

typedef struct rq {
	rq_node_t *head;
	rq_node_t *tail;
	size_t len;
} rq_t;

#define RQ_INIT()                             \
	{                                         \
		.head = NULL, .tail = NULL, .len = 0, \
	}

#define RQ_NODE_INIT(_data)                          \
	{                                                \
		.data = (_data), .next = NULL, .prev = NULL, \
	}

#define rq_empty(rq) ((rq)->len == 0)
#define rq_len(rq) ((rq)->len)

#define rq_front(rq) ((rq)->head ? (rq)->head->data : NULL)
#define rq_back(rq) ((rq)->tail ? (rq)->tail->data : NULL)

void rq_init(rq_t *rq);
void rq_node_init(rq_node_t *node, void *data);

bool rq_node_is_linked(const rq_node_t *node);

void rq_push_front_node(rq_t *rq, rq_node_t *node);
void rq_push_back_node(rq_t *rq, rq_node_t *node);

void rq_push_front(rq_t *rq, rq_node_t *node, void *data);
void rq_push_back(rq_t *rq, rq_node_t *node, void *data);

rq_node_t *rq_pop_front_node(rq_t *rq);
rq_node_t *rq_pop_back_node(rq_t *rq);

void *rq_pop_front(rq_t *rq);
void *rq_pop_back(rq_t *rq);

void rq_remove_node(rq_t *rq, rq_node_t *node);

rq_node_t *rq_find(const rq_t *rq, const void *data);
bool rq_contains(const rq_t *rq, const void *data);
bool rq_remove(rq_t *rq, const void *data);

void rq_rotate(rq_t *rq);
void rq_clear(rq_t *rq);

#endif