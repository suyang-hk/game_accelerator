#ifndef __KVSTORE_H__
#define __KVSTORE_H__

#include<stdio.h>
#include<assert.h>
#include<string.h>
#include<stdlib.h>
#include<unistd.h>


#define BUFFER_LENGTH 1024


#ifdef ENABLE_LOG

#define LOG(_fmt, ...) \
    fprintf(stdout, "[%s:%d]: "_fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#else

#define LOG(...) ((void)0)

#endif

typedef int(*RCALLBACK)(int fd);

struct conn_item
{
    int fd;

    char rbuffer[BUFFER_LENGTH];
    char wbuffer[BUFFER_LENGTH];
    char resource[BUFFER_LENGTH];
    int rlen_;
    int wlen_;

    union
    {
        RCALLBACK accept_callback;
        RCALLBACK recv_callback;
    } recv_t;
    RCALLBACK send_callback;
};

int epoll_entry(void);
int ntyco_entry(void);

int kvstore_request(struct conn_item *item);

void *kvstore_malloc(size_t size);
void kvstore_free(void *ptr);

#define NETWORK_EPOLL		0
#define NETWORK_NTYCO		1

#define ENABLE_NETWORK_SELECT NETWORK_NTYCO

#define ENABLE_ARRAY_KVENGINE 0
#define ENABLE_RBTREE_KVENGINE 0
#define ENABLE_HASH_KVENGINE	1


#if ENABLE_ARRAY_KVENGINE  

struct kvs_array_item {
    char *key;
    char *value;
};

typedef struct array_s {
    struct kvs_array_item *array_table;
    int array_idx;
} array_t;

extern array_t array;

#define KVS_ARRAY_SIZE 1024

int kvstore_array_create(array_t *arr);
void kvstore_array_destory(array_t *arr);
int kvstore_array_set(array_t *arr, char *key, char *value);
char *kvstore_array_get(array_t *arr, char *key);
int kvstore_array_delete(array_t *arr, char *key);
int kvstore_array_modify(array_t *arr, char *key, char *value);

#endif //array

#ifdef ENABLE_RBTREE_KVENGINE

typedef struct _rbtree rbtree_t;

extern rbtree_t Tree;

int kvstore_rbtree_create(rbtree_t *tree);

void kvstore_rbtree_destory(rbtree_t *tree);

int kvs_rbtree_set(rbtree_t *tree, char *key, char *value);

char *kvs_rbtree_get(rbtree_t *tree, char *key);

int kvs_rbtree_delete(rbtree_t *tree, char *key);

int kvs_rbtree_modify(rbtree_t *tree, char *key, char *value);

int kvs_rbtree_count(rbtree_t *tree);
#endif // rbtree


#ifdef ENABLE_HASH_KVENGINE

typedef struct hashtable_s hashtable_t;

extern hashtable_t Hash;


int kvstore_hash_create(hashtable_t *hash);
void kvstore_hash_destory(hashtable_t *hash);
int kvs_hash_set(hashtable_t *hash, char *key, char *value);
char *kvs_hash_get(hashtable_t *hash, char *key);
int kvs_hash_delete(hashtable_t *hash, char *key);
int kvs_hash_modify(hashtable_t *hash, char *key, char *value);
int kvs_hash_count(hashtable_t *hash);

#endif //hash

#endif