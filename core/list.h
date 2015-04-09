/*
 * Astra Core
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2015, Andrey Dyldin <and@cesbo.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ASC_LIST_H_
#define _ASC_LIST_H_ 1

#include "base.h"

#define QMD_TRACE_ELEM(elem)
#define QMD_TRACE_HEAD(head)
#define TRACEBUF
#define TRASHIT(x)

/*
 * Tail queue declarations.
 */

#define TAILQ_HEAD(name, type)                                                                  \
struct name                                                                                     \
{                                                                                               \
    struct type *tqh_first; /* first element */                                                 \
    struct type **tqh_last; /* addr of last next element */                                     \
    TRACEBUF                                                                                    \
}

#define TAILQ_HEAD_INITIALIZER(head) { NULL, &(head).tqh_first }

#define TAILQ_ENTRY(type)                                                                       \
struct                                                                                          \
{                                                                                               \
    struct type *tqe_next;  /* next element */                                                  \
    struct type **tqe_prev; /* address of previous next element */                              \
    TRACEBUF                                                                                    \
}

/*
 * Tail queue functions.
 */

#define TAILQ_CONCAT(head1, head2, field)                                                       \
do                                                                                              \
{                                                                                               \
    if(!TAILQ_EMPTY(head2))                                                                     \
    {                                                                                           \
        *(head1)->tqh_last = (head2)->tqh_first;                                                \
        (head2)->tqh_first->field.tqe_prev = (head1)->tqh_last;                                 \
        (head1)->tqh_last = (head2)->tqh_last;                                                  \
        TAILQ_INIT((head2));                                                                    \
        QMD_TRACE_HEAD(head1);                                                                  \
        QMD_TRACE_HEAD(head2);                                                                  \
    }                                                                                           \
} while (0)

#define TAILQ_EMPTY(head) ((head)->tqh_first == NULL)

#define TAILQ_FIRST(head) ((head)->tqh_first)

#define TAILQ_FOREACH(var, head, field)                                                         \
    for((var) = TAILQ_FIRST((head))                                                             \
        ; (var)                                                                                 \
        ; (var) = TAILQ_NEXT((var), field))

#define TAILQ_FOREACH_SAFE(var, head, field, tvar)                                              \
    for((var) = TAILQ_FIRST((head))                                                             \
        ; (var) && ((tvar) = TAILQ_NEXT((var), field), 1)                                       \
        ; (var) = (tvar))

#define TAILQ_FOREACH_REVERSE(var, head, headname, field)                                       \
    for((var) = TAILQ_LAST((head), headname)                                                    \
        ; (var)                                                                                 \
        ; (var) = TAILQ_PREV((var), headname, field))

#define TAILQ_FOREACH_REVERSE_SAFE(var, head, headname, field, tvar)                            \
    for((var) = TAILQ_LAST((head), headname)                                                    \
        ; (var) && ((tvar) = TAILQ_PREV((var), headname, field), 1)                             \
        ; (var) = (tvar))

#define TAILQ_INIT(head)                                                                        \
do                                                                                              \
{                                                                                               \
    TAILQ_FIRST((head)) = NULL;                                                                 \
    (head)->tqh_last = &TAILQ_FIRST((head));                                                    \
    QMD_TRACE_HEAD(head);                                                                       \
} while (0)

#define TAILQ_INSERT_AFTER(head, listelm, elm, field)                                           \
do                                                                                              \
{                                                                                               \
    if ((TAILQ_NEXT((elm), field) = TAILQ_NEXT((listelm), field)) != NULL)                      \
        TAILQ_NEXT((elm), field)->field.tqe_prev = &TAILQ_NEXT((elm), field);                   \
    else                                                                                        \
    {                                                                                           \
        (head)->tqh_last = &TAILQ_NEXT((elm), field);                                           \
        QMD_TRACE_HEAD(head);                                                                   \
    }                                                                                           \
    TAILQ_NEXT((listelm), field) = (elm);                                                       \
    (elm)->field.tqe_prev = &TAILQ_NEXT((listelm), field);                                      \
    QMD_TRACE_ELEM(&(elm)->field);                                                              \
    QMD_TRACE_ELEM(&listelm->field);                                                            \
} while (0)

#define TAILQ_INSERT_BEFORE(listelm, elm, field)                                                \
do                                                                                              \
{                                                                                               \
    (elm)->field.tqe_prev = (listelm)->field.tqe_prev;                                          \
    TAILQ_NEXT((elm), field) = (listelm);                                                       \
    *(listelm)->field.tqe_prev = (elm);                                                         \
    (listelm)->field.tqe_prev = &TAILQ_NEXT((elm), field);                                      \
    QMD_TRACE_ELEM(&(elm)->field);                                                              \
    QMD_TRACE_ELEM(&listelm->field);                                                            \
} while (0)

#define TAILQ_INSERT_HEAD(head, elm, field)                                                     \
do                                                                                              \
{                                                                                               \
    if ((TAILQ_NEXT((elm), field) = TAILQ_FIRST((head))) != NULL)                               \
        TAILQ_FIRST((head))->field.tqe_prev = &TAILQ_NEXT((elm), field);                        \
    else                                                                                        \
        (head)->tqh_last = &TAILQ_NEXT((elm), field);                                           \
    TAILQ_FIRST((head)) = (elm);                                                                \
    (elm)->field.tqe_prev = &TAILQ_FIRST((head));                                               \
    QMD_TRACE_HEAD(head);                                                                       \
    QMD_TRACE_ELEM(&(elm)->field);                                                              \
} while (0)

#define TAILQ_INSERT_TAIL(head, elm, field)                                                     \
do                                                                                              \
{                                                                                               \
    TAILQ_NEXT((elm), field) = NULL;                                                            \
    (elm)->field.tqe_prev = (head)->tqh_last;                                                   \
    *(head)->tqh_last = (elm);                                                                  \
    (head)->tqh_last = &TAILQ_NEXT((elm), field);                                               \
    QMD_TRACE_HEAD(head);                                                                       \
    QMD_TRACE_ELEM(&(elm)->field);                                                              \
} while (0)

#define TAILQ_LAST(head, headname)                                                              \
    (*(((struct headname *)((head)->tqh_last))->tqh_last))

#define TAILQ_NEXT(elm, field) ((elm)->field.tqe_next)

#define TAILQ_PREV(elm, headname, field)                                                        \
    (*(((struct headname *)((elm)->field.tqe_prev))->tqh_last))

#define TAILQ_REMOVE(head, elm, field)                                                          \
do                                                                                              \
{                                                                                               \
    if ((TAILQ_NEXT((elm), field)) != NULL)                                                     \
        TAILQ_NEXT((elm), field)->field.tqe_prev = (elm)->field.tqe_prev;                       \
    else                                                                                        \
    {                                                                                           \
        (head)->tqh_last = (elm)->field.tqe_prev;                                               \
        QMD_TRACE_HEAD(head);                                                                   \
    }                                                                                           \
    *(elm)->field.tqe_prev = TAILQ_NEXT((elm), field);                                          \
    TRASHIT((elm)->field.tqe_next);                                                             \
    TRASHIT((elm)->field.tqe_prev);                                                             \
    QMD_TRACE_ELEM(&(elm)->field);                                                              \
} while (0)

#define TAILQ_SWAP(head1, head2, type, field)                                                   \
do                                                                                              \
{                                                                                               \
    struct type *swap_first = (head1)->tqh_first;                                               \
    struct type **swap_last = (head1)->tqh_last;                                                \
    (head1)->tqh_first = (head2)->tqh_first;                                                    \
    (head1)->tqh_last = (head2)->tqh_last;                                                      \
    (head2)->tqh_first = swap_first;                                                            \
    (head2)->tqh_last = swap_last;                                                              \
    if ((swap_first = (head1)->tqh_first) != NULL)                                              \
        swap_first->field.tqe_prev = &(head1)->tqh_first;                                       \
    else                                                                                        \
        (head1)->tqh_last = &(head1)->tqh_first;                                                \
    if ((swap_first = (head2)->tqh_first) != NULL)                                              \
        swap_first->field.tqe_prev = &(head2)->tqh_first;                                       \
    else                                                                                        \
        (head2)->tqh_last = &(head2)->tqh_first;                                                \
} while (0)

typedef struct asc_list_t asc_list_t;

asc_list_t * asc_list_init(void) __wur;
void asc_list_destroy(asc_list_t *list);

void asc_list_first(asc_list_t *list);
void asc_list_next(asc_list_t *list);
bool asc_list_eol(asc_list_t *list) __wur;
void * asc_list_data(asc_list_t *list) __wur;
size_t asc_list_size(asc_list_t *list) __wur;

void asc_list_insert_head(asc_list_t *list, void *data);
void asc_list_insert_tail(asc_list_t *list, void *data);

void asc_list_remove_current(asc_list_t *list);
void asc_list_remove_item(asc_list_t *list, void *data);

#define asc_list_for(__list) \
    for(asc_list_first(__list); !asc_list_eol(__list); asc_list_next(__list))

#endif /* _ASC_LIST_H_ */
