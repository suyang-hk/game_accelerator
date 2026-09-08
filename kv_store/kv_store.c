#include"kv_store.h"


#define KVSTORE_MAX_TOKENS 128

const char *commands[] = {
    "SET", "GET", "DEL", "MOD",
};


enum
{
    KVS_CMD_START = 0,
    KVS_CMD_SET = KVS_CMD_START,
    KVS_CMD_GET,
    KVS_CMD_DEL,
    KVS_CMD_MOD,
    KVS_CMD_SIZE,
};

void *kvstore_malloc(size_t size) {
     return malloc(size);
}

void kvstore_free(void *ptr) {
    return free(ptr);
}

//kvengine
int kvstore_set(char *key, char *value){

#if ENABLE_ARRAY_KVENGINE
    return kvstore_array_set(&array, key, value);
#elif ENABLE_RBTREE_KVENGINE
    return kvs_rbtree_set(&Tree, key, value);
#elif ENABLE_HASH_KVENGINE
    return kvs_hash_set(&Hash, key, value);
#endif
}

char *kvstore_get(char *key) {

#if ENABLE_ARRAY_KVENGINE
    return kvstore_array_get(&array, key);
#elif ENABLE_RBTREE_KVENGINE
    return kvs_rbtree_get(&Tree, key);
#elif ENABLE_HASH_KVENGINE
    return kvs_hash_get(&Hash, key);
#endif
}

int kvstore_delete(char *key) {

#if ENABLE_ARRAY_KVENGINE
    return kvstore_array_delete(&array, key);
#elif ENABLE_RBTREE_KVENGINE
    return kvs_rbtree_delete(&Tree, key);
#elif ENABLE_HASH_KVENGINE
    return kvs_hash_delete(&Hash, key);
#endif
}

int kvstore_modify(char *key, char *value) {

#if ENABLE_ARRAY_KVENGINE
    return kvstore_array_modify(&array, key, value);
#elif ENABLE_RBTREE_KVENGINE
    return kvs_rbtree_modify(&Tree, key, value);
#elif ENABLE_HASH_KVENGINE
    return kvs_hash_modify(&Hash, key, value);
#endif
}



int kvstore_split_token(char *msg, char **tokens) {
    
    if (msg == NULL || tokens == NULL) {
        return -1;
    }

    int idx = 0;

    char *token = strtok(msg, " ");

    while(token != NULL) {
        tokens[idx++] = token;
        token = strtok(NULL, " ");
    }

    return idx;
}

int kvstore_parser_protocol(struct conn_item *item, char **tokens, int count) {
    if (item == NULL || tokens == NULL || count == 0)
        return -1;

    int cmd = KVS_CMD_START;

    for (cmd = KVS_CMD_START; cmd < KVS_CMD_SIZE; cmd++)
    {
        if (strcmp(commands[cmd], tokens[0]) == 0) {
            break;
        }
    }

    char *msg = item->wbuffer;
    memset(msg, 0, BUFFER_LENGTH);

    switch( cmd ) {
        case KVS_CMD_SET:
        {
            int ret = kvstore_set(tokens[1], tokens[2]);
            if (!ret) {
            snprintf(msg, BUFFER_LENGTH, "SUCCESS");
            } else {
                snprintf(msg, BUFFER_LENGTH, "FAILER");
            }
            break;
        }
        case KVS_CMD_GET:
        {
            char *value = kvstore_get(tokens[1]);
            if (value != NULL) {
                snprintf(msg, BUFFER_LENGTH, "%s", value);
            } else {
                snprintf(msg, BUFFER_LENGTH, "NO EXIST");
            }
            break;
        }
        case KVS_CMD_DEL:
        {
            int ret = kvstore_delete(tokens[1]);

            if (ret < 0) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
            }
            else if (ret == 0) {
                snprintf(msg, BUFFER_LENGTH, "SUCCESS");
            } else {
                snprintf(msg, BUFFER_LENGTH, "DEL_FAILER");
            }
            break;
        }
        case KVS_CMD_MOD:
        {
            int ret = kvstore_modify(tokens[1], tokens[2]);
            if (ret < 0) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
            }
            else if (ret == 0) {
                snprintf(msg, BUFFER_LENGTH, "SUCCESS");
            } else {
                snprintf(msg, BUFFER_LENGTH, "MOD_FAILER");
            }
            break;
        }
        default:
            assert(0);
        }

        return 1;
}

int kvstore_request(struct conn_item *item) {

    LOG("recv: %s\n", item->rbuffer);

    char *msg = item->rbuffer;
    char *tokens[KVSTORE_MAX_TOKENS];

    int count = kvstore_split_token(msg, tokens);


    int idx = 0;
    for (idx = 0; idx < count; ++idx) {
        LOG("token: %s\n", tokens[idx]);
    }

    kvstore_parser_protocol(item, tokens, count);

    return 0;
}

int init_kvengine(void) {
#if ENABLE_ARRAY_KVENGINE
    kvstore_array_create(&array);
#endif

#if ENABLE_RBTREE_KVENGINE
    kvstore_rbtree_create(&Tree);
#endif

#if ENABLE_HASH_KVENGINE
    kvstore_hash_create(&Hash);
#endif
    return 0;
}

int exit_kvengine(void) {

#if ENABLE_ARRAY_KVENGINE
	kvstore_array_destory(&array);
#endif

#if ENABLE_RBTREE_KVENGINE
	kvstore_rbtree_destory(&Tree);
#endif

#if ENABLE_HASH_KVENGINE
    kvstore_hash_destory(&Hash);
#endif

    return 0;
}

int main() {

    init_kvengine();

#if (ENABLE_NETWORK_SELECT == NETWORK_EPOLL)

    epoll_entry();

#elif (ENABLE_NETWORK_SELECT == NETWORK_NTYCO)
    ntyco_entry();

#endif
    exit_kvengine();
}