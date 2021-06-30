// SPDX-FileCopyrightText: 2021 Keno Hassler
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

void queue_init(queue *q)
{
    if (pthread_mutex_init(&q->mutex, NULL) != 0)
    {
        perror("queue: mutex init failed");
        exit(EXIT_FAILURE);
    }

    q->size = 0;
    q->last = NULL;
    q->first = NULL;
}

void queue_destroy(queue *q)
{
    // remove all entries first
    queue_clear(q);

    if (pthread_mutex_destroy(&q->mutex) != 0)
    {
        perror("queue: mutex destroy failed");
        exit(EXIT_FAILURE);
    }
}

void queue_clear(queue *q)
{
    if (pthread_mutex_lock(&q->mutex) != 0)
    {
        perror("queue: mutex lock failed");
        exit(EXIT_FAILURE);
    }

    queue_elem *elem = q->first;
    while (elem != NULL)
    {
        queue_elem *tmp = elem;
        elem = elem->next;
        free(tmp);
    }

    q->size = 0;
    q->last = NULL;
    q->first = NULL;

    if (pthread_mutex_unlock(&q->mutex) != 0)
    {
        perror("queue: mutex unlock failed");
        exit(EXIT_FAILURE);
    }
}

void queue_add(queue *q, void *item)
{
    queue_elem *elem = (queue_elem *)calloc(1, sizeof(queue_elem));
    if (elem == NULL)
    {
        perror("queue: malloc failed");
        exit(EXIT_FAILURE);
    }
    elem->payload = item;

    if (pthread_mutex_lock(&q->mutex) != 0)
    {
        perror("queue: mutex lock failed");
        exit(EXIT_FAILURE);
    }

    // is this the first element?
    if (q->first == NULL)
    {
        q->first = elem;
    }
    else
    {
        q->last->next = elem;
    }

    q->last = elem;
    q->size++;

    if (pthread_mutex_unlock(&q->mutex) != 0)
    {
        perror("queue: mutex unlock failed");
        exit(EXIT_FAILURE);
    }
}

void *queue_next(queue *q, size_t backbuffer)
{
    void *item = NULL;

    if (pthread_mutex_lock(&q->mutex) != 0)
    {
        perror("queue: mutex lock failed");
        exit(EXIT_FAILURE);
    }

    if (q->size > backbuffer)
    {
        // pop the first queue element
        queue_elem *elem = q->first;
        q->first = elem->next;

        item = elem->payload;
        free(elem);
        q->size--;

        // was this the last element?
        if (q->first == NULL)
        {
            q->last = NULL;
        }
    }

    if (pthread_mutex_unlock(&q->mutex) != 0)
    {
        perror("queue: mutex unlock failed");
        exit(EXIT_FAILURE);
    }

    return item;
}
