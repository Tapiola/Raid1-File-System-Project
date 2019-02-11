#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cash.h"



struct cnode* create_data (const char* path, char* buf, size_t size, off_t offset, int fd) 
{
	struct cnode* node = (struct cnode*) malloc (sizeof(struct cnode));
	node->path = malloc (strlen(path));
	strcpy (node->path, path);
	node->buf = malloc (size);
	strcpy (node->buf, buf);
	node->size = size;
	node->offset = offset;
	node->fd = fd;
	node->prev = NULL;
	node->next = NULL;

	return node;
}

struct cqueue* init_queue (int capacity) 
{
	struct cqueue* queue = (struct cqueue*) malloc (sizeof(struct cqueue));
	queue->count = 0;
	queue->q_size = 0;
	queue->capacity = capacity;
	queue->head = NULL;
	queue->tail = NULL;		
	return queue;
}


int node_compar (struct cnode* node, const char* path, off_t offset) 
{
	if (strcmp (node->path, path) == 0 &&
					node->offset == offset)
		return 0;
	else return -1;
}


struct cnode* find_node (struct cqueue* queue, const char* path, off_t offset) 
{	
	if (queue->count == 0) return NULL;
	struct cnode* node = queue->head;
	if (node_compar (node, path, offset) == 0) 
		return node;
	int found = -1;
	int i;
	for (i = 1; i < queue->count; i++) {
		node = node->next;
		if (node_compar (node, path, offset) == 0) {
			found = 0;
			node->prev->next = node->next;
			if (node->next != NULL) {
				node->next->prev = node->prev;
			} else {
				queue->tail = node->prev;
				queue->tail->next = NULL;
			}
			move_forward (queue, node);
			break;
		}
	}
	if (found == -1) return NULL;
	return queue->head;
}

int add_front (struct cqueue* queue, const char* path, char* buf, size_t size, off_t offset, int fd) 
{
	struct cnode* node = create_data (path, buf, size, offset, fd);

	while (queue->capacity <= queue->q_size + size) {
		remove_rear (queue);
	}
	if (queue->count == 0) {
		queue->head = node;
		queue->tail = node;
	} else if (queue->count > 0 ) {
		move_forward (queue, node);
	}
	queue->q_size += size;
	queue->count ++;
	return 0;
}

int free_node (struct cnode* node) {
	free (node->path);
	free (node->buf);
	free (node);
	return 0;
}

int remove_rear (struct cqueue* queue) 
{	
	if (queue->count == 0) return -1;
	if (queue->count == 1) {
		queue->head = NULL;
	}
	
	struct cnode* rear = (struct cnode*) malloc (sizeof (struct cnode));
	memcpy (rear, queue->tail, sizeof (struct cnode));
	queue->q_size -= rear->size;
	queue->tail = queue->tail->prev;
	queue->tail->next = NULL;
	free_node (rear);

	queue->count --;
	return 0;
}

int move_forward (struct cqueue* queue, struct cnode* node) 
{
	queue->head->prev = node;
	node->next = queue->head;
	node->prev = NULL;
	queue->head = node;	
	return 0;
}

int delete_by_path (struct cqueue* queue, const char* path) 
{	
	if (queue->count == 0) return 0;
	if (queue->count == 1 && strcmp (queue->head->path, path) == 0) {
		remove_rear (queue);
		return 0;
	}
	struct cnode* node = queue->head;
	if (strcmp (node->path, path) == 0) {
		queue->head = node->next;
		queue->q_size -= node->size;
		queue->count--;
		free_node (node);
	}
		
	int cnt_copy = queue->count;
	int i;
	for (i = 1; i < cnt_copy; i++) {
		node = node->next;
		if (strcmp (node->path, path) == 0) {
			node->prev->next = node->next;
			if (node->next != NULL) {
				node->next->prev = node->prev;
			} 
			if (node->prev != NULL) {
				node->prev->next = node->next;
			}
			queue->q_size -= node->size;
			queue->count--;
			free_node (node);
		}
	}
	
	return 0;
}

int free_queue (struct cqueue* queue) 
{
	int i;
	for (i = 0; i < queue->count; i++) {
		remove_rear (queue);
	}
	free (queue);
	queue->count = 0;
	return 0;
}


int print_queue (struct cqueue* queue) 
{
	struct cnode* node = queue->head;
	printf("queue: %s, %d;", node->path, (int)node->offset);
	int i;
	for (i = 1; i < queue->count; i++) {
		node = node->next;
		printf(" %s, %d;", node->path, (int)node->offset);
	}
	printf("\n");
	return 0;
}