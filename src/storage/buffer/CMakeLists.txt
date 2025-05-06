add_library_unity(
  duckdb_storage_buffer
  OBJECT
  file_buffer_handle.cpp
  file_block_handle.cpp
  block_manager.cpp
  file_buffer_pool.cpp
  buffer_pool_reservation.cpp)
set(ALL_OBJECT_FILES
    ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:duckdb_storage_buffer>
    PARENT_SCOPE)
