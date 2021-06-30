// SPDX-FileCopyrightText: 2021 Keno Hassler
//
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef __QUEUE_H__
#define __QUEUE_H__

#include <pthread.h>

typedef struct queue_elem_s
{
    void *payload;
    struct queue_elem_s *next;
} queue_elem;

typedef struct queue_s
{
    pthread_mutex_t mutex;
    size_t size;
    queue_elem *last;
    queue_elem *first;
} queue;

void queue_init(queue *q);
void queue_destroy(queue *q);

void queue_clear(queue *q);
void queue_add(queue *q, void *item);
void *queue_next(queue *q, size_t backbuffer);

#endif
