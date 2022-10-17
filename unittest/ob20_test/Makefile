CFLAGS= -I../../include
CCFLAGS= -I../../include
LDFLAGS= -g -L ../../libmariadb/ -lobclnt -lz -lpthread -lgtest
BIN=ob20_appinfo_test ob20_trans_test ob20_clientinfo_test ob20_slowquery_test ob20_bigpacket_response_test ob20_bigpacket_request_test
EXEC=env LD_LIBRARY_PATH=../../libmariadb/:${LD_LIBRARY_PATH}
all:${BIN}
.PHONY:clean
ob20_appinfo_test:ob20_appinfo_test.c
ob20_trans_test:ob20_trans_test.c
ob20_clientinfo_test:ob20_clientinfo_test.c
ob20_slowquery_test:ob20_slowquery_test.c
ob20_bigpacket_response_test:ob20_bigpacket_response_test.c
ob20_bigpacket_request_test:ob20_bigpacket_request_test.c
# run:
# 	$(EXEC) ./ob_oracle_array_binding_test
clean:
	rm -rf ${BIN} *.o
