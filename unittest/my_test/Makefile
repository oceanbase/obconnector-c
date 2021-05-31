CFLAGS= -I../../include
CCFLAGS= -I../../include
LDFLAGS= -g -L ../../libmariadb/ -lobclnt -lz -lpthread -lgtest
BIN=ob_mysql_ps_test ob_mysql_text_test ob_oracle_ps_test
EXEC=env LD_LIBRARY_PATH=../../libmariadb/
all:${BIN} run
.PHONY:clean
ob_mysql_ps_test:ob_mysql_ps_test.c
ob_mysql_text_test:ob_mysql_text_test.c
ob_oracle_ps_test:ob_oracle_ps_test.c
run:
	$(EXEC) ./ob_mysql_ps_test
	$(EXEC) ./ob_mysql_text_test
	$(EXEC) ./ob_oracle_ps_test
clean:
	rm -rf ${BIN} *.o
