#mysql-mode
#obclient -h100.88.109.130 -P18815 -uadmin@mysql -padmin -Doceanbase -A
export LD_LIBRARY_PATH=../../libmariadb/:$LD_LIBRARY_PATH
export OB_MYSQL_SERVER_HOST=100.88.109.130
export OB_MYSQL_SERVER_PORT=18815
export OB_MYSQL_SERVER_DBNAME="test"
export OB_MYSQL_SERVER_USERNAME="admin@mysql"
export OB_MYSQL_SERVER_PASSWORD="admin"
#oracle-mode
#obclient -h100.88.105.197 -P30035 -utest@tt3 -ptest
export OB_ORACLE_SERVER_HOST=100.88.105.197
export OB_ORACLE_SERVER_PORT=30035
export OB_ORACLE_SERVER_DBNAME="test"
export OB_ORACLE_SERVER_USERNAME="test@tt3"
export OB_ORACLE_SERVER_PASSWORD="test"
