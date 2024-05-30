ifdef CROSS_COMPILE
CC=$(CROSS_COMPILE)gcc
else
CC=gcc
endif


.PHONY:clean all
all: test_m test_old

test_m:
	$(CC)  pthread_pool.c  ./test/test.c  -I . -lpthread -O2 -o  ./out/t1
	$(CC)  pthread_pool.c  ./test/test2.c -I . -lpthread -O2 -o  ./out/t2
test_old:
	$(CC)  ./old/pthread_pool.c  ./test/test.c  -I . -lpthread -O2 -o  ./out/oldt1
	$(CC)  ./old/pthread_pool.c  ./test/test2.c -I . -lpthread -O2 -o  ./out/oldt2
clean:
	rm ./out/t1
	rm ./out/t2
	rm ./out/oldt1
	rm ./out/oldt2