F = -std=gnu99 -Wall -g

SRCS = ext2_mkdir.c ext2_cp.c ext2_ln.c ext2_rm.c ext2_restore.c ext2_checker.c
TARGETS = ext2_mkdir ext2_cp  ext2_ln ext2_rm ext2_restore ext2_checker

all : $(TARGETS)

ext2_mkdir : ext2_mkdir.o ext2_functions.o
	gcc ${FLAGS} -o ext2_mkdir ext2_mkdir.o ext2_functions.o

ext2_cp : ext2_cp.o ext2_functions.o
	gcc ${FLAGS} -o ext2_cp ext2_cp.o ext2_functions.o

ext2_ln : ext2_ln.o ext2_functions.o
	gcc ${FLAGS} -o ext2_ln ext2_ln.o ext2_functions.o

ext2_rm : ext2_rm.o ext2_functions.o
	gcc ${FLAGS} -o ext2_rm ext2_rm.o ext2_functions.o

ext2_restore : ext2_restore.o ext2_functions.o
	gcc ${FLAGS} -o ext2_restore ext2_restore.o ext2_functions.o

ext2_checker : ext2_checker.o ext2_functions.o
	gcc ${FLAGS} -o ext2_checker ext2_checker.o ext2_functions.o

%.o : %.c ext2_functions.h ext2.h
	gcc $(CFLAGS) -c $<

.PHONY: clean
clean :
	rm -f *.o $(TARGETS) *~






