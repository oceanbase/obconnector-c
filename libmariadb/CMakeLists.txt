INCLUDE_DIRECTORIES(${CC_SOURCE_DIR}/include
                    ${CC_SOURCE_DIR}/libmariadb)

ADD_DEFINITIONS(-D HAVE_COMPRESS)
ADD_DEFINITIONS(-D LIBMARIADB)
ADD_DEFINITIONS(-D THREAD)

INCLUDE(${CC_SOURCE_DIR}/cmake/sign.cmake)

SET(MARIADB_LIB_SYMBOLS
 mariadb_cancel
 mariadb_connection
 mariadb_convert_string
 ma_pvio_register_callback
 mariadb_get_charset_by_name
 mariadb_stmt_execute_direct
 mariadb_get_charset_by_nr
 mariadb_get_info
 mariadb_get_infov
 mysql_get_timeout_value
 mysql_get_timeout_value_ms
 mysql_optionsv
 mysql_ps_fetch_functions
 mariadb_reconnect
 mysql_stmt_warning_count
 mariadb_stmt_fetch_fields
 mariadb_rpl_open
 mariadb_rpl_close
 mariadb_rpl_fetch
 mariadb_rpl_optionsv
 mariadb_rpl_get_optionsv
 mariadb_free_rpl_event
 mariadb_field_attr
)
IF(WITH_SSL)
  SET(MARIADB_LIB_SYMBOLS ${MARIADB_LIB_SYMBOLS} mariadb_deinitialize_ssl)
ENDIF()

SET(MYSQL_LIB_SYMBOLS
 mysql_affected_rows
 mysql_autocommit
 mysql_change_user
 mysql_character_set_name
 mysql_client_find_plugin
 mysql_client_register_plugin
 mysql_close
 mysql_commit
 mysql_data_seek
 mysql_debug
 mysql_dump_debug_info
 mysql_embedded
 mysql_eof
 mysql_errno
 mysql_error
 mysql_escape_string
 mysql_fetch_field
 mysql_fetch_field_direct
 mysql_fetch_fields
 mysql_fetch_params
 mysql_fetch_lengths
 mysql_fetch_row
 mysql_field_count
 mysql_field_seek
 mysql_field_tell
 mysql_free_result
 mysql_get_character_set_info
 mysql_get_charset_by_name
 mysql_get_charset_by_nr
 mysql_get_client_info
 mysql_get_client_version
 mysql_get_host_info
 mysql_get_option
 mysql_get_optionv
 mysql_get_parameters
 mysql_get_proto_info
 mysql_get_server_info
 mysql_get_server_name
 mysql_get_server_version
 mysql_get_socket
 mysql_get_ssl_cipher
 mysql_hex_string
 mysql_info
 mysql_init
 mysql_insert_id
 mysql_kill
 mysql_list_dbs
 mysql_list_fields
 mysql_list_processes
 mysql_list_tables
 mysql_load_plugin
 mysql_load_plugin_v
 mysql_more_results
 mysql_net_field_length
 mysql_net_read_packet
 mysql_next_result
 mysql_num_fields
 mysql_num_rows
 mysql_options
 mysql_options4
 mysql_ping
 mysql_query
 mysql_read_query_result
 mysql_real_connect
 mysql_real_escape_string
 mysql_real_query
 mysql_refresh
 mysql_reset_connection
 mysql_rollback
 mysql_row_seek
 mysql_row_tell
 mysql_select_db
 mysql_send_query
 mysql_server_end
 mysql_server_init
 mysql_session_track_get_next
 mysql_session_track_get_first
 mysql_set_character_set
 mysql_set_local_infile_default
 mysql_set_local_infile_handler
 mysql_set_server_option
 mysql_shutdown
 mysql_sqlstate
 mysql_ssl_set
 mysql_stat
 mysql_stmt_affected_rows
 mysql_stmt_attr_get
 mysql_stmt_attr_set
 mysql_stmt_bind_param
 mysql_stmt_bind_result
 mysql_stmt_close
 mysql_stmt_data_seek
 mysql_stmt_errno
 mysql_stmt_error
 mysql_stmt_execute
 get_support_send_fetch_flag
 get_support_plarray_bindbyname
 get_use_prepare_execute
 get_use_preapre_execute
 mysql_stmt_prepare_v2
 mysql_stmt_execute_v2
 has_added_user_fields
 is_returning_result
 stmt_pre_exe_req_ext_flag_set
 is_pl_out_result
 ob_time_to_date
 mysql_stmt_fetch
 mysql_stmt_fetch_column
 mysql_stmt_fetch_oracle_cursor
 mysql_stmt_fetch_oracle_implicit_cursor
 mysql_stmt_fetch_oracle_buffered_result
 mysql_stmt_field_count
 mysql_stmt_free_result
 mysql_stmt_init
 mysql_stmt_insert_id
 mysql_stmt_more_results
 mysql_stmt_next_result
 mysql_stmt_num_rows
 mysql_stmt_param_count
 mysql_stmt_param_metadata
 mysql_stmt_prepare
 mysql_stmt_reset
 mysql_stmt_result_metadata
 mysql_stmt_row_seek
 mysql_stmt_row_tell
 mysql_stmt_send_long_data
 mysql_stmt_send_piece_data
 mysql_stmt_read_piece_data
 mysql_stmt_sqlstate
 mysql_stmt_store_result
 mysql_store_result
 mysql_thread_end
 mysql_thread_id
 mysql_thread_init
 mysql_thread_safe
 mysql_use_result
 mysql_warning_count)

# some gcc versions fail to compile asm parts of my_context.c,
# if build type is "Release" (see CONC-133), so we need to add -g flag
IF(CMAKE_COMPILER_IS_GNUCC AND CMAKE_BUILD_TYPE MATCHES "Release")
  SET_SOURCE_FILES_PROPERTIES(my_context.c PROPERTIES COMPILE_FLAGS -g)
ENDIF()

SET(MARIADB_DYNCOL_SYMBOLS
 mariadb_dyncol_check
 mariadb_dyncol_column_cmp_named
 mariadb_dyncol_column_count
 mariadb_dyncol_create_many_named
 mariadb_dyncol_create_many_num
 mariadb_dyncol_exists_named
 mariadb_dyncol_exists_num
 mariadb_dyncol_free
 mariadb_dyncol_get_named
 mariadb_dyncol_get_num
 mariadb_dyncol_has_names
 mariadb_dyncol_json
 mariadb_dyncol_list_named
 mariadb_dyncol_list_num
 mariadb_dyncol_unpack
 mariadb_dyncol_update_many_named
 mariadb_dyncol_update_many_num
 mariadb_dyncol_val_double
 mariadb_dyncol_val_long
 mariadb_dyncol_val_str)

SET(MARIADB_NONBLOCK_SYMBOLS
 mysql_autocommit_cont
 mysql_autocommit_start
 mysql_change_user_cont
 mysql_change_user_start
 mysql_close_cont
 mysql_close_start
 mysql_commit_cont
 mysql_commit_start
 mysql_dump_debug_info_cont
 mysql_dump_debug_info_start
 mysql_fetch_row_cont
 mysql_fetch_row_start
 mysql_free_result_cont
 mysql_free_result_start
 mysql_kill_cont
 mysql_kill_start
 mysql_list_fields_cont
 mysql_list_fields_start
 mysql_next_result_cont
 mysql_next_result_start
 mysql_ping_cont
 mysql_ping_start
 mysql_reset_connection_start
 mysql_reset_connection_cont
 mysql_query_cont
 mysql_query_start
 mysql_read_query_result_cont
 mysql_read_query_result_start
 mysql_real_connect_cont
 mysql_real_connect_start
 mysql_real_query_cont
 mysql_real_query_start
 mysql_refresh_cont
 mysql_refresh_start
 mysql_rollback_cont
 mysql_rollback_start
 mysql_select_db_cont
 mysql_select_db_start
 mysql_send_query_cont
 mysql_send_query_start
 mysql_set_character_set_cont
 mysql_set_character_set_start
 mysql_set_server_option_cont
 mysql_set_server_option_start
 mysql_shutdown_cont
 mysql_shutdown_start
 mysql_stat_cont
 mysql_stat_start
 mysql_stmt_close_cont
 mysql_stmt_close_start
 mysql_stmt_execute_cont
 mysql_stmt_execute_start
 mysql_stmt_fetch_cont
 mysql_stmt_fetch_start
 mysql_stmt_free_result_cont
 mysql_stmt_free_result_start
 mysql_stmt_next_result_cont
 mysql_stmt_next_result_start
 mysql_stmt_prepare_cont
 mysql_stmt_prepare_start
 mysql_stmt_reset_cont
 mysql_stmt_reset_start
 mysql_stmt_send_long_data_cont
 mysql_stmt_send_long_data_start
 mysql_stmt_store_result_cont
 mysql_stmt_store_result_start
 mysql_store_result_cont
 mysql_store_result_start
#oci use
 skip_param_complex
 convert_type_to_complex
 ob_time_to_date
 ob_time_to_str_oracle_dfm
 str_to_ob_time_oracle_dfm
 hash_delete
 hash_insert
 hash_search
 hash_element
 hash_free
 mysql_extension_init
 _hash_init
# thread
 ob_thread_self
 ob_thread_equal
 ob_thread_attr_init
 ob_thread_attr_destroy
 ob_thread_attr_setstacksize
 ob_thread_attr_setdetachstate
 ob_thread_attr_getstacksize
 ob_thread_yield
 ob_thread_create
 ob_thread_join
 ob_thread_once
 ob_thread_cancel
 ob_thread_exit
# ob20 
 ob20_set_extra_info
# full_link_trace
 flt_set_module
 flt_set_action
 flt_set_client_info
 flt_set_identifier
 flt_get_control_level
 flt_get_control_sample_pct
 flt_get_control_record_policy
 flt_get_control_print_spct
 flt_get_control_slow_threshold

 flt_deserialize_extra_info
 flt_serialize_extra_info
 flt_get_serialize_size_extra_info
 ob_cond_init
 ob_cond_destroy
 ob_cond_timedwait
 ob_cond_wait
 ob_cond_signal
 ob_cond_broadcast
 ob_mutex_init
 ob_mutex_lock
 ob_mutex_trylock
 ob_mutex_unlock
 ob_mutex_destroy
 ob_rw_init
 ob_rw_destroy
 ob_rw_rdlock
 ob_rw_tryrdlock
 ob_rw_wrlock
 ob_rw_trywrlock
 ob_rw_unlock
 ob_create_thread_key
 ob_delete_thread_key
 ob_get_thread_key
 ob_set_thread_key
)

# handle static plugins
SET(LIBMARIADB_SOURCES ${LIBMARIADB_PLUGIN_SOURCES})
SET(SYSTEM_LIBS ${SYSTEM_LIBS} ${LIBMARIADB_PLUGIN_LIBS} ${INTERNAL_ZLIB_LIBRARY})
ADD_DEFINITIONS(${LIBMARIADB_PLUGIN_DEFS})
FOREACH(plugin ${PLUGINS_STATIC})
  SET(EXTERNAL_PLUGINS "${EXTERNAL_PLUGINS} extern struct st_mysql_client_plugin ${plugin}_client_plugin;\n")
  SET(BUILTIN_PLUGINS "${BUILTIN_PLUGINS}   (struct st_mysql_client_plugin *)&${plugin}_client_plugin,\n")
ENDFOREACH()
CONFIGURE_FILE(${CC_SOURCE_DIR}/libmariadb/ma_client_plugin.c.in
               ${CC_BINARY_DIR}/libmariadb/ma_client_plugin.c)

SET(LIBMARIADB_SOURCES ${LIBMARIADB_SOURCES}
${CC_SOURCE_DIR}/plugins/auth/my_auth.c
ma_array.c
ma_charset.c
ma_hash.c
ma_net.c
mariadb_charset.c
ma_time.c
ma_default.c
ma_errmsg.c
mariadb_lib.c
ma_list.c
ma_pvio.c
ma_tls.c
ma_alloc.c
ma_compress.c
ma_init.c
ma_password.c
ma_ll2str.c
ma_sha1.c
mariadb_stmt.c
ma_loaddata.c
ma_stmt_codec.c
ma_string.c
ma_dtoa.c
mariadb_rpl.c
ob_protocol20.c
ob_bitmap.c
ob_complex.c
ob_strtoll10.c
ob_oracle_format_models.c
ob_thread.c
ob_object.c
ob_full_link_trace.c
ob_serialize.c
ob_rwlock.c
ob_cond.c
ob_thread_key.c
${CC_BINARY_DIR}/libmariadb/ma_client_plugin.c
ma_io.c
${SSL_SOURCES}
)

IF(WIN32)
  ADD_DEFINITIONS(-DSIZEOF_CHARP=${CMAKE_SIZEOF_VOID_P})
  INCLUDE_DIRECTORIES(${CC_SOURCE_DIR}/win-iconv)
  SET(LIBMARIADB_SOURCES
    ${LIBMARIADB_SOURCES}
    ${CC_SOURCE_DIR}/win-iconv/win_iconv.c
    win32_errmsg.c
    win32_errmsg.h)
ELSE()
  IF(ICONV_INCLUDE_DIR)
     INCLUDE_DIRECTORIES(BEFORE ${ICONV_INCLUDE_DIR}) 
  ENDIF()
  IF(NOT CMAKE_SYSTEM_NAME MATCHES AIX)
    ADD_DEFINITIONS(-DLIBICONV_PLUG)
  ENDIF()
ENDIF()

IF(ZLIB_FOUND AND WITH_EXTERNAL_ZLIB)
  INCLUDE_DIRECTORIES(${ZLIB_INCLUDE_DIR})
ELSE()
  SET(ZLIB_SOURCES
    ../zlib/adler32.c 
    ../zlib/compress.c
    ../zlib/crc32.c
    ../zlib/deflate.c
    ../zlib/gzclose.c
    ../zlib/gzlib.c
    ../zlib/gzread.c
    ../zlib/gzwrite.c
    ../zlib/infback.c
    ../zlib/inffast.c
    ../zlib/inflate.c
    ../zlib/inftrees.c
    ../zlib/trees.c
    ../zlib/uncompr.c
    ../zlib/zutil.c
  )
  SET(LIBMARIADB_SOURCES ${LIBMARIADB_SOURCES} ${ZLIB_SOURCES})
  INCLUDE_DIRECTORIES(${CC_SOURCE_DIR}/zlib)
ENDIF()

IF(WITH_DYNCOL)
  MESSAGE1(WITH_DYNCOL "Dynamic column API support: ON")
  SET(MARIADB_LIB_SYMBOLS ${MARIADB_LIB_SYMBOLS} ${MARIADB_DYNCOL_SYMBOLS})
  SET(LIBMARIADB_SOURCES ${LIBMARIADB_SOURCES} mariadb_dyncol.c)
ENDIF()

SET(LIBMARIADB_SOURCES ${LIBMARIADB_SOURCES} mariadb_async.c ma_context.c)
SET(MARIADB_LIB_SYMBOLS ${MARIADB_LIB_SYMBOLS} ${MARIADB_NONBLOCK_SYMBOLS})

INCLUDE(${CC_SOURCE_DIR}/cmake/export.cmake)
IF(NOT WIN32)
  CREATE_EXPORT_FILE(WRITE obclnt.def
                   "libmysqlclient_18"
                   "${MYSQL_LIB_SYMBOLS}"
                   "libobclnt_18")
  CREATE_EXPORT_FILE(APPEND obclnt.def
                   "libmariadb_3"
                   "${MARIADB_LIB_SYMBOLS}"
                   "")
ELSE()
  CREATE_EXPORT_FILE(WRITE obclnt.def
                   "libmariadb_3"
                   "${MARIADB_LIB_SYMBOLS};${MYSQL_LIB_SYMBOLS}"
                   "")
ENDIF()


IF((NOT WIN32) AND (CMAKE_VERSION VERSION_GREATER 2.8.7))
  # CREATE OBJECT LIBRARY 
  ADD_LIBRARY(mariadb_obj OBJECT ${LIBMARIADB_SOURCES})
  IF(UNIX)
	  SET_TARGET_PROPERTIES(mariadb_obj PROPERTIES COMPILE_FLAGS "${CMAKE_SHARED_LIBRARY_C_FLAGS}")
  ENDIF()
  SET (MARIADB_OBJECTS $<TARGET_OBJECTS:mariadb_obj>)
ELSE()
  SET (MARIADB_OBJECTS ${LIBMARIADB_SOURCES})
ENDIF()

# Xcode doesn't support targets that have only object files,
# so let's add an empty file to keep Xcode happy
IF(CMAKE_GENERATOR MATCHES Xcode)
  FILE(WRITE ${CC_BINARY_DIR}/libmariadb/empty.c "")
  SET(EMPTY_FILE ${CC_BINARY_DIR}/libmariadb/empty.c)
ENDIF()

IF(WIN32)
  SET_VERSION_INFO("TARGET:libmariadb"
                   "FILE_TYPE:VFT_DLL"
                   "SOURCE_FILE:libmariadb/libmariadb.c"
                   "ORIGINAL_FILE_NAME:libmariadb.dll"
                   "FILE_DESCRIPTION:Dynamic lib for client/server communication")
ENDIF()


ADD_LIBRARY(obclnt STATIC  ${MARIADB_OBJECTS} ${EMPTY_FILE})
TARGET_LINK_LIBRARIES(obclnt ${SYSTEM_LIBS})

IF(UNIX)
  ADD_LIBRARY(libobclnt SHARED ${libmariadb_RC} ${MARIADB_OBJECTS} ${EMPTY_FILE})
  SET_TARGET_PROPERTIES(libobclnt PROPERTIES COMPILE_FLAGS "${CMAKE_SHARED_LIBRARY_C_FLAGS}")
ELSE()
  ADD_LIBRARY(libobclnt SHARED ${libmariadb_RC} obclnt.def)
  TARGET_LINK_LIBRARIES(libobclnt LINK_PRIVATE obclnt)
  SET_TARGET_PROPERTIES(libobclnt PROPERTIES LINKER_LANGUAGE C)
ENDIF()

TARGET_LINK_LIBRARIES(libobclnt LINK_PRIVATE ${SYSTEM_LIBS})

SIGN_TARGET(libobclnt)

IF(CMAKE_SIZEOF_VOID_P EQUAL 8 AND MSVC)
  SET_TARGET_PROPERTIES(obclnt PROPERTIES STATIC_LIBRARY_FLAGS "/machine:x64")
ENDIF()

IF(CMAKE_SYSTEM_NAME MATCHES "Linux" OR
   CMAKE_SYSTEM_NAME MATCHES "kFreeBSD" OR
   CMAKE_SYSTEM_NAME MATCHES "GNU")
  IF (NOT WITH_ASAN AND NOT WITH_TSAN AND NOT WITH_UBSAN AND NOT WITH_MSAN)
    TARGET_LINK_LIBRARIES (libobclnt LINK_PRIVATE "-Wl,--no-undefined")
  ENDIF()
  SET_TARGET_PROPERTIES(libobclnt PROPERTIES LINK_FLAGS "${CC_BINARY_DIR}/libmariadb/obclnt.def")
ENDIF()

SET_TARGET_PROPERTIES(obclnt PROPERTIES IMPORTED_INTERFACE_LINK_LIBRARIES "${SYSTEM_LIBS}")
SET_TARGET_PROPERTIES(libobclnt PROPERTIES IMPORTED_INTERFACE_LINK_LIBRARIES "${SYSTEM_LIBS}")

SET_TARGET_PROPERTIES(libobclnt PROPERTIES PREFIX "")

#
# Installation
#
INCLUDE(${CC_SOURCE_DIR}/cmake/symlink.cmake)

# There are still several projects which don't make use
# of the config program. To make sure these programs can
# use mariadb client library we provide libmysql symlinks

IF(WITH_MYSQLCOMPAT)
  create_symlink(libmysqlclient${CMAKE_SHARED_LIBRARY_SUFFIX} libobclnt ${INSTALL_LIBDIR})
  create_symlink(libmysqlclient_r${CMAKE_SHARED_LIBRARY_SUFFIX} libobclnt ${INSTALL_LIBDIR})
  IF(NOT CMAKE_SYSTEM_NAME MATCHES AIX)
    create_symlink(libmysqlclient${CMAKE_STATIC_LIBRARY_SUFFIX} obclnt ${INSTALL_LIBDIR})
    create_symlink(libmysqlclient_r${CMAKE_STATIC_LIBRARY_SUFFIX} obclnt ${INSTALL_LIBDIR})
  ENDIF()
ENDIF()


SET_TARGET_PROPERTIES(libobclnt PROPERTIES VERSION
 ${CPACK_PACKAGE_VERSION_MAJOR}
 SOVERSION ${CPACK_PACKAGE_VERSION_MAJOR})

IF(NOT WIN32)
  SET_TARGET_PROPERTIES(obclnt PROPERTIES OUTPUT_NAME "${LIBMARIADB_STATIC_NAME}")
ENDIF()

INSTALL(TARGETS obclnt
          COMPONENT Development
          DESTINATION ${INSTALL_LIBDIR})
INSTALL(TARGETS libobclnt 
          COMPONENT SharedLibraries
        DESTINATION ${INSTALL_LIBDIR})


IF(MSVC)
   # On Windows, install PDB
   INSTALL(FILES $<TARGET_PDB_FILE:libobclnt> DESTINATION "${INSTALL_LIBDIR}"
           CONFIGURATIONS Debug RelWithDebInfo
           COMPONENT Development)
ENDIF()
