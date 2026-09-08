```
编译： make
编译测试用例 make testcase
运行测试用例 ./testcase -s 127.0.0.1 -p 9096 -m 1 
```

```
结构
引擎层 array  ---  rbtree ----- hash
接口层kvs_hash_set()	kvs_has_get()	kvs_hash_delete()
	kvs_hash_modify()	kvs_hash_create()	kvs_hash_destory()
适配层 kvstore_set() kvstore_get() kvstore_delete() kvstore_modify()
	  init_kvengine() exit_kvengine()
核心层 kv_store
协议层 SET KEY VALUE
网络层 reactor ---- ntyco ---- 
```

```
测试
qps WSL2上为8k，预计linux上为2-3w左右
```



```
存储部分，后续可仿照hash添加skip_table, B/B+tree
eg: kvs_hash_set()
	kvs_has_get()
	kvs_hash_delete()
	kvs_hash_modify()
	kvs_hash_create()
	kvs_hash_destory()
通过宏定义添加进模块化接口中
int kvstore_set(char *key, char *value){

#if ENABLE_ARRAY_KVENGINE
    return kvstore_array_set(&array, key, value);
    
#elif ENABLE_RBTREE_KVENGINE
    return kvs_rbtree_set(&Tree, key, value);
    
#elif ENABLE_HASH_KVENGINE
    return kvs_hash_set(&Hash, key, value);
    
#endif
}


```

