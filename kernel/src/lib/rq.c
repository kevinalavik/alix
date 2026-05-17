#include <lib/rq.h>

void rq_init(rq_t *rq)
{
	rq->head = NULL;
	rq->tail = NULL;
	rq->len = 0;
}

void rq_node_init(rq_node_t *node, void *data)
{
	node->data = data;
	node->next = NULL;
	node->prev = NULL;
}

bool rq_node_is_linked(const rq_node_t *node)
{
	return node->next != NULL || node->prev != NULL;
}

void rq_push_front_node(rq_t *rq, rq_node_t *node)
{
	node->prev = NULL;
	node->next = rq->head;

	if (rq->head)
		rq->head->prev = node;
	else
		rq->tail = node;

	rq->head = node;
	rq->len++;
}

void rq_push_back_node(rq_t *rq, rq_node_t *node)
{
	node->next = NULL;
	node->prev = rq->tail;

	if (rq->tail)
		rq->tail->next = node;
	else
		rq->head = node;

	rq->tail = node;
	rq->len++;
}

void rq_push_front(rq_t *rq, rq_node_t *node, void *data)
{
	rq_node_init(node, data);
	rq_push_front_node(rq, node);
}

void rq_push_back(rq_t *rq, rq_node_t *node, void *data)
{
	rq_node_init(node, data);
	rq_push_back_node(rq, node);
}

rq_node_t *rq_pop_front_node(rq_t *rq)
{
	rq_node_t *node;

	if (!rq->head)
		return NULL;

	node = rq->head;
	rq->head = node->next;

	if (rq->head)
		rq->head->prev = NULL;
	else
		rq->tail = NULL;

	node->next = NULL;
	node->prev = NULL;
	rq->len--;

	return node;
}

rq_node_t *rq_pop_back_node(rq_t *rq)
{
	rq_node_t *node;

	if (!rq->tail)
		return NULL;

	node = rq->tail;
	rq->tail = node->prev;

	if (rq->tail)
		rq->tail->next = NULL;
	else
		rq->head = NULL;

	node->next = NULL;
	node->prev = NULL;
	rq->len--;

	return node;
}

void *rq_pop_front(rq_t *rq)
{
	rq_node_t *node = rq_pop_front_node(rq);

	return node ? node->data : NULL;
}

void *rq_pop_back(rq_t *rq)
{
	rq_node_t *node = rq_pop_back_node(rq);

	return node ? node->data : NULL;
}

void rq_remove_node(rq_t *rq, rq_node_t *node)
{
	if (node->prev)
		node->prev->next = node->next;
	else
		rq->head = node->next;

	if (node->next)
		node->next->prev = node->prev;
	else
		rq->tail = node->prev;

	node->next = NULL;
	node->prev = NULL;
	rq->len--;
}

rq_node_t *rq_find(const rq_t *rq, const void *data)
{
	rq_node_t *node = rq->head;

	while (node) {
		if (node->data == data)
			return node;

		node = node->next;
	}

	return NULL;
}

bool rq_contains(const rq_t *rq, const void *data)
{
	return rq_find(rq, data) != NULL;
}

bool rq_remove(rq_t *rq, const void *data)
{
	rq_node_t *node = rq_find(rq, data);

	if (!node)
		return false;

	rq_remove_node(rq, node);
	return true;
}

void rq_rotate(rq_t *rq)
{
	rq_node_t *node;

	if (rq->len < 2)
		return;

	node = rq_pop_front_node(rq);
	rq_push_back_node(rq, node);
}

void rq_clear(rq_t *rq)
{
	rq_node_t *node = rq->head;

	while (node) {
		rq_node_t *next = node->next;

		node->next = NULL;
		node->prev = NULL;

		node = next;
	}

	rq->head = NULL;
	rq->tail = NULL;
	rq->len = 0;
}