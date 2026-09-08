#include"kv_store.h"
#if ENABLE_ARRAY_KVENGINE

array_t array;

int kvstore_array_create(array_t *arr) {
    if (!arr)
        return -1;

    arr->array_table = kvstore_malloc(KVS_ARRAY_SIZE * sizeof(struct kvs_array_item));
    if (!arr->array_table)
        return -1;

    memset(arr->array_table, 0, KVS_ARRAY_SIZE * sizeof(struct kvs_array_item));

    arr->array_idx = 0;

    return 0;
}

void kvstore_array_destory(array_t *arr) {
    if (!arr)
        return -1;

    if (arr->array_table)
        kvstore_free(arr->array_table);
}

int kvstore_array_set(array_t *arr, char *key, char *value) {
if (key == NULL || value == NULL || arr == NULL)
          return -1;

      // 找第一个空位：优先复用 DEL 留下的空洞，没有才追加到末尾
      int idx = 0;
      while (idx < KVS_ARRAY_SIZE && arr->array_table[idx].key != NULL)
          idx++;
      if (idx == KVS_ARRAY_SIZE)
          return -1;   // 整个表真满了（不是 array_idx 到顶）

      char *kcopy = kvstore_malloc(strlen(key) + 1);
      if (kcopy == NULL) return -1;
      strcpy(kcopy, key);

      char *vcopy = kvstore_malloc(strlen(value) + 1);
      if (vcopy == NULL) { kvstore_free(kcopy); return -1; }
      strcpy(vcopy, value);

      arr->array_table[idx].key = kcopy;
      arr->array_table[idx].value = vcopy;

      if (idx == arr->array_idx)   // 只有真正追加到新位置才推进高水位
          arr->array_idx++;

      return 0;
#if 0
    if (key == NULL || value == NULL || array_idx == KVS_ARRAY_SIZE)
        return -1;

    char *kcopy = kvstore_malloc(strlen(key) + 1);
    if(kcopy == NULL) {
        return -1;
    }

    strcpy(kcopy, key);

    char *vcopy = kvstore_malloc(strlen(value) + 1);
    if (vcopy == NULL) {
        kvstore_free(kcopy);
        return -1;
    }

    strcpy(vcopy, value);

    array_table[array_idx].key = kcopy;
    array_table[array_idx].value = vcopy;

    array_idx++;
    return 0;
#endif
}

char *kvstore_array_get(array_t *arr, char *key) {
    for (int idx = 0; idx < arr->array_idx; idx++) {
          if (arr->array_table[idx].key == NULL)   // 空洞跳过
              continue;
          if (strcmp(arr->array_table[idx].key, key) == 0)
              return arr->array_table[idx].value;
    }
    return NULL;

#if 0
    int idx = 0;
    for (idx = 0; idx < array_idx; idx++) {
        if (strcmp(array_table[idx].key, key) == 0) {
            return array_table[idx].value;
        }
    }

    return NULL;
#endif

}


int kvstore_array_delete(array_t *arr, char *key) {
      if (key == NULL || arr == NULL) return -1;
      for (int idx = 0; idx < arr->array_idx; idx++) {
          if (arr->array_table[idx].key == NULL)
              continue;
          if (strcmp(arr->array_table[idx].key, key) == 0) {
              kvstore_free(arr->array_table[idx].value);
              kvstore_free(arr->array_table[idx].key);
              arr->array_table[idx].key = NULL;     
              arr->array_table[idx].value = NULL;
              return 0;
          }
      }
      return 1;   
#if 0
    if (key == NULL)
        return -1;

    int idx = 0;

    for (idx = 0; idx < array_idx; idx++) {
        if (strcmp(array_table[idx].key, key) == 0) {
            kvstore_free(array_table[idx].value);
            array_table[idx].value = NULL;

            kvstore_free(array_table[idx].key);
            array_table[idx].key = NULL;

            if (idx != (array_idx - 1))
            memmove(&array_table[idx], &array_table[idx + 1], (array_idx - idx - 1) * sizeof(struct kvs_array_item));
            array_idx--;
            return 0;
        }
    }
    return idx;
#endif
}

int kvstore_array_modify(array_t *arr, char *key, char *value) {
      if (arr == NULL || key == NULL || value == NULL) return -1;
      for (int idx = 0; idx < arr->array_idx; idx++) {
          if (arr->array_table[idx].key == NULL)
              continue;
          if (strcmp(arr->array_table[idx].key, key) == 0) {
              kvstore_free(arr->array_table[idx].value);
              char *vcopy = kvstore_malloc(strlen(value) + 1);   
              if (vcopy == NULL) return -1;
              strcpy(vcopy, value);
              arr->array_table[idx].value = vcopy;
              return 0;
          }
      }
      return 1;
#if 0
    if (key == NULL || value == NULL)
        return -1;

    int idx = 0;
    for (idx = 0; idx < array_idx; idx++)
    {
        if (strcmp(array_table[idx].key, key) == 0) {
            kvstore_free(array_table[idx].value);
            array_table[idx].value = NULL;

            char *vcopy = kvstore_malloc(sizeof(value) + 1);
            strcpy(vcopy, value);

            array_table[idx].value = vcopy;

            return 0;
        }
    }

    return idx;
#endif
}
#endif
