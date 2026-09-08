


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "kv_store.h"

#if ENABLE_HASH_KVENGINE

#define MAX_KEY_LEN	128
#define MAX_VALUE_LEN	512


#define MAX_TABLE_SIZE	1024

#define ENABLE_POINTER_KEY	1


typedef struct hashnode_s {
#if ENABLE_POINTER_KEY
	char *key;
	char *value;
#else
	char key[MAX_KEY_LEN];
	char value[MAX_VALUE_LEN];
#endif	
	struct hashnode_s *next;
	
} hashnode_t;


typedef struct hashtable_s {

	hashnode_t **nodes; //* change **, 

	int max_slots;
	int count;

	pthread_mutex_t lock;

} hashtable_t;


hashtable_t Hash;


//Connection 
// 'C' + 'o' + 'n'
static int _hash(char *key, int size) {

	if (!key) return -1;

	unsigned int h = 5381u;

	while (*key) {
		h = ((h << 5) + h) + (unsigned char)*key++;
	}

	return (int)(h % (unsigned int)size);

}

// 扩容：把节点搬到 2 倍大小的新表（复用节点，不重新分配 key/value）
static int resize_hashtable(hashtable_t *hash) {

	if (!hash) return -1;

	int new_size = hash->max_slots * 2;

	hashnode_t **new_nodes = (hashnode_t**)kvstore_malloc(sizeof(hashnode_t*) * new_size);
	if (!new_nodes) return -1;
	memset(new_nodes, 0, sizeof(hashnode_t*) * new_size);

	for (int i = 0; i < hash->max_slots; i ++) {
		hashnode_t *node = hash->nodes[i];
		while (node != NULL) {
			hashnode_t *next = node->next;
			int idx = _hash(node->key, new_size);
			node->next = new_nodes[idx];
			new_nodes[idx] = node;
			node = next;
		}
	}

	kvstore_free(hash->nodes);
	hash->nodes = new_nodes;
	hash->max_slots = new_size;

	return 0;
}


hashnode_t *_create_node(char *key, char *value) {

	hashnode_t *node = (hashnode_t*)kvstore_malloc(sizeof(hashnode_t));
	if (!node) return NULL;

#if ENABLE_POINTER_KEY

	node->key = kvstore_malloc(strlen(key) + 1);
	if (!node->key) {
		kvstore_free(node);
		return NULL;
	}
	strcpy(node->key, key);

	node->value = kvstore_malloc(strlen(value) + 1);
	if (!node->value) {
		kvstore_free(node->key);
		kvstore_free(node);
		return NULL;
	}
	strcpy(node->value, value);

#else

	strncpy(node->key, key, MAX_KEY_LEN);
	strncpy(node->value, value, MAX_VALUE_LEN);
	
#endif

	node->next = NULL;

	return node;
}


//
int init_hashtable(hashtable_t *hash) {

	if (!hash) return -1;

	hash->nodes = (hashnode_t**)kvstore_malloc(sizeof(hashnode_t*) * MAX_TABLE_SIZE);
	if (!hash->nodes) return -1;

	hash->max_slots = MAX_TABLE_SIZE;
	hash->count = 0; 

	pthread_mutex_init(&hash->lock, NULL);

	return 0;
}

// 
void dest_hashtable(hashtable_t *hash) {

	if (!hash) return;

	int i = 0;
	for (i = 0;i < hash->max_slots;i ++) {
		hashnode_t *node = hash->nodes[i];

		while (node != NULL) { // error

			hashnode_t *tmp = node;
			node = node->next;
			hash->nodes[i] = node;
			
			kvstore_free(tmp);
			
		}
	}

	kvstore_free(hash->nodes);
	
}



// mp
int put_kv_hashtable(hashtable_t *hash, char *key, char *value) {

	if (!hash || !key || !value) return -1;

	pthread_mutex_lock(&hash->lock);

	if (hash->count + 1 > hash->max_slots * 0.7)   // 负载因子 > 0.7 就扩容
		resize_hashtable(hash);

	int idx = _hash(key, hash->max_slots);

	hashnode_t *node = hash->nodes[idx];
	while (node != NULL) {
		if (strcmp(node->key, key) == 0) { // exist: 覆盖旧值
			kvstore_free(node->value);
			node->value = kvstore_malloc(strlen(value) + 1);
			if (node->value) {
				strcpy(node->value, value);
				pthread_mutex_unlock(&hash->lock);
				return 0;
			}
			pthread_mutex_unlock(&hash->lock);
			return -1;
		}
		node = node->next;
	}

	hashnode_t *new_node = _create_node(key, value);
	new_node->next = hash->nodes[idx];
	hash->nodes[idx] = new_node;
	
	hash->count ++;

	pthread_mutex_unlock(&hash->lock);

	return 0;
}


char * get_kv_hashtable(hashtable_t *hash, char *key) {

	if (!hash || !key) return NULL;

	int idx = _hash(key, hash->max_slots);

	pthread_mutex_lock(&hash->lock);
	hashnode_t *node = hash->nodes[idx];

	while (node != NULL) {

		if (strcmp(node->key, key) == 0) {
			pthread_mutex_unlock(&hash->lock);
			return node->value;
		}

		node = node->next;
	}

	pthread_mutex_unlock(&hash->lock);

	return NULL;

}


int count_kv_hashtable(hashtable_t *hash) {
	return hash->count;
}

int delete_kv_hashtable(hashtable_t *hash, char *key) {
	if (!hash || !key) return -2;

	int idx = _hash(key, hash->max_slots);

	pthread_mutex_lock(&hash->lock);
	hashnode_t *head = hash->nodes[idx];
	if (head == NULL) return -1; // noexist
	// head node
	if (strcmp(head->key, key) == 0) {
		hashnode_t *tmp = head->next;
		hash->nodes[idx] = tmp;

#if ENABLE_POINTER_KEY
		if (head->key) {
			kvstore_free(head->key);
		}
		if (head->value) {
			kvstore_free(head->value);
		}
		kvstore_free(head);
#else
		free(head);
#endif
		hash->count --;
		pthread_mutex_unlock(&hash->lock);
		
		return 0;
	}

	hashnode_t *cur = head;
	while (cur->next != NULL) {
		if (strcmp(cur->next->key, key) == 0) break; // search node
		
		cur = cur->next;
	}

	if (cur->next == NULL) {
		
		pthread_mutex_unlock(&hash->lock);
		return -1;
	}

	hashnode_t *tmp = cur->next;
	cur->next = tmp->next;
#if ENABLE_POINTER_KEY
	if (tmp->key) {
		kvstore_free(tmp->key);
	}
	if (tmp->value) {
		kvstore_free(tmp->value);
	}
	kvstore_free(tmp);
#else
	free(tmp);
#endif
	hash->count --;

	pthread_mutex_unlock(&hash->lock);
	
	return 0;
}


int exist_kv_hashtable(hashtable_t *hash, char *key) {

	char *value = get_kv_hashtable(hash, key);
	if (value) return 1;
	else return 0;
	
}




// 5 + 2

int kvstore_hash_create(hashtable_t *hash) {

	return init_hashtable(hash);
	
}


void kvstore_hash_destory(hashtable_t *hash) {

	return dest_hashtable(hash);

}


int kvs_hash_set(hashtable_t *hash, char *key, char *value) {

	return put_kv_hashtable(hash, key, value);

}


char *kvs_hash_get(hashtable_t *hash, char *key) {

	return get_kv_hashtable(hash, key);

}

int kvs_hash_delete(hashtable_t *hash, char *key) {

	return delete_kv_hashtable(hash, key);

}


int kvs_hash_modify(hashtable_t *hash, char *key, char *value) {

	if (!hash || !key || !value) return -1;

	int idx = _hash(key, hash->max_slots);

	hashnode_t *node = hash->nodes[idx];

	while (node != NULL) {

		if (strcmp(node->key, key) == 0) {
			kvstore_free(node->value);

			node->value = kvstore_malloc(strlen(value) + 1);
			if (node->value) {
				strcpy(node->value, value);
				return 0;
			} else 
				assert(0);
		}

		node = node->next;
	}


	return -1;

}

int kvs_hash_count(hashtable_t *hash) {
	return hash->count;
}




#endif