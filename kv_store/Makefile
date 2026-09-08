CC      = gcc
CFLAGS  = -I. -I Ntyco -Wall -g
TARGET  = kv_store

# 独立程序（自带 main），不编进主程序，就列在这里
EXCLUDE = testcase.c

# 自动收集当前目录所有 .c（排除独立程序）+ Ntyco/ 子目录协程库
SRCS    = $(filter-out $(EXCLUDE), $(wildcard *.c)) $(wildcard Ntyco/*.c)
OBJS    = $(SRCS:.c=.o)
DEPS    = $(OBJS:.o=.d)

# 协程库依赖 pthread / dl
LDFLAGS = -pthread -ldl

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

# 单独构建测试客户端（连接服务器的独立程序）
testcase: testcase.o
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET) testcase testcase.o testcase.d

.PHONY: clean testcase
