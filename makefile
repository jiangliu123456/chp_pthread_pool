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
	$(CC)  ./old/pthread_pool.c   ./test/test.c  -I . -lpthread -O2 -o  ./out/ov1_1
	$(CC)  ./old/pthread_pool.c   ./test/test2.c -I . -lpthread -O2 -o  ./out/ov1_2
	$(CC)  ./old/pthread_pool2.c  ./test/test.c  -I . -lpthread -O2 -o  ./out/ov2_1
	$(CC)  ./old/pthread_pool2.c  ./test/test2.c -I . -lpthread -O2 -o  ./out/ov2_2
	$(CC)  ./old/pthread_pool3.c  ./test/test.c  -I . -lpthread -O2 -o  ./out/ov3_1
	$(CC)  ./old/pthread_pool3.c  ./test/test2.c -I . -lpthread -O2 -o  ./out/ov3_2
	$(CC)  ./old/pthread_pool4.c  ./test/test.c  -I . -lpthread -O2 -o  ./out/ov4_1
	$(CC)  ./old/pthread_pool4.c  ./test/test2.c -I . -lpthread -O2 -o  ./out/ov4_2
	g++    ./old/hm.cpp  -std=c++11 -I . -lpthread -O2 -o  ./out/hm.out
	g++    ./old/hm2.cpp  -std=c++11 -I . -lpthread -O2 -o  ./out/hm2.out
clean:
	rm ./out/t1
	rm ./out/t2
	rm ./out/oldt1
	rm ./out/oldt2