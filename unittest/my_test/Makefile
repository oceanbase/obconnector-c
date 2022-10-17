CFLAGS= -I../../include
CCFLAGS= -I../../include
LDFLAGS= -g -L ../../libmariadb/ -lobclnt -lz -lpthread -lgtest
BIN=ob_mysql_ps_test ob_mysql_ps_cursor_test ob_mysql_text_test ob_oracle_ps_test \
ob_oracle_datetype_test ob_new_ps_test ob_new_ps_select_notable ob_new_ps_select_test \
ob_mysql_ssl_test ob_oracle_fetch_cursor_test ob_oracle_array_binding_test \
ob_oracle_pl_test
EXEC=env LD_LIBRARY_PATH=../../libmariadb/
all:${BIN} run
.PHONY:clean
ob_mysql_ps_cursor_test:ob_mysql_ps_cursor_test.c
ob_mysql_ps_test:ob_mysql_ps_test.c
ob_mysql_text_test:ob_mysql_text_test.c
ob_oracle_ps_test:ob_oracle_ps_test.c
ob_oracle_datetype_test:ob_oracle_datetype_test.c
ob_new_ps_test:ob_new_ps_test.c
ob_new_ps_select_notable:ob_new_ps_select_notable.c
ob_new_ps_select_test:ob_new_ps_select_test.c
ob_mysql_ssl_test:ob_mysql_ssl_test.c
ob_oracle_fetch_cursor_test:ob_oracle_fetch_cursor_test.c
ob_oracle_array_binding_test:ob_oracle_array_binding_test.c
ob_oracle_pl_test:ob_oracle_pl_test.c
run:
	$(EXEC) ./ob_mysql_ps_test
	$(EXEC) ./ob_mysql_ps_cursor_test
	$(EXEC) ./ob_mysql_text_test
	$(EXEC) ./ob_oracle_ps_test
	$(EXEC) ./ob_oracle_datetype_test
	$(EXEC) ./ob_new_ps_test
	$(EXEC) ./ob_new_ps_select_notable
	$(EXEC) ./ob_new_ps_select_test
	$(EXEC) ./ob_oracle_fetch_cursor_test
	$(EXEC) ./ob_oracle_array_binding_test
clean:
	rm -rf ${BIN} *.o
