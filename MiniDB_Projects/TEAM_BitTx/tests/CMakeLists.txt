include_directories(${PROJECT_SOURCE_DIR}/src)

add_executable(test_common
 test_common.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
)
add_test(NAME test_common COMMAND test_common)
set_tests_properties(test_common PROPERTIES LABELS "common")

add_executable(test_disk_manager
 test_disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
)
add_test(NAME test_disk_manager COMMAND test_disk_manager)
set_tests_properties(test_disk_manager PROPERTIES LABELS "storage")

add_executable(test_heap_file
 test_heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
)
add_test(NAME test_heap_file COMMAND test_heap_file)
set_tests_properties(test_heap_file PROPERTIES LABELS "storage")

add_executable(test_table_heap
 test_table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
)
add_test(NAME test_table_heap COMMAND test_table_heap)
set_tests_properties(test_table_heap PROPERTIES LABELS "catalog")

add_executable(test_buffer_pool
 test_buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
)
add_test(NAME test_buffer_pool COMMAND test_buffer_pool)
set_tests_properties(test_buffer_pool PROPERTIES LABELS "buffer")

add_executable(test_lock_manager
 test_lock_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/transaction/lock_manager.cpp
)
add_test(NAME test_lock_manager COMMAND test_lock_manager)
set_tests_properties(test_lock_manager PROPERTIES LABELS "tx")

add_executable(test_log_manager
 test_log_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/recovery/log_record.cpp
 ${PROJECT_SOURCE_DIR}/src/recovery/log_manager.cpp
)
add_test(NAME test_log_manager COMMAND test_log_manager)
set_tests_properties(test_log_manager PROPERTIES LABELS "recovery")

add_executable(test_recovery
 test_recovery.cpp
 ${PROJECT_SOURCE_DIR}/src/recovery/log_record.cpp
 ${PROJECT_SOURCE_DIR}/src/recovery/log_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/recovery/recovery.cpp
 ${PROJECT_SOURCE_DIR}/src/transaction/lock_manager.cpp
)
add_test(NAME test_recovery COMMAND test_recovery)
set_tests_properties(test_recovery PROPERTIES LABELS "recovery")



add_executable(test_integration test_integration.cpp

 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/token.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/tokenizer.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/parser.cpp
 ${PROJECT_SOURCE_DIR}/src/planner/planner.cpp
 ${PROJECT_SOURCE_DIR}/src/executor/evaluator.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/execution/executor.cpp
 ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
)
add_test(NAME test_integration COMMAND test_integration)
set_tests_properties(test_integration PROPERTIES LABELS "integration")

add_executable(test_bptree
 test_bptree.cpp
${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
)
add_test(NAME test_bptree COMMAND test_bptree)
set_tests_properties(test_bptree PROPERTIES LABELS "index")

add_executable(test_catalog
 test_catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
)
add_test(NAME test_catalog COMMAND test_catalog)
set_tests_properties(test_catalog PROPERTIES LABELS "catalog")

add_executable(test_deadlock
 test_deadlock.cpp
 ${PROJECT_SOURCE_DIR}/src/transaction/lock_manager.cpp
)
add_test(NAME test_deadlock COMMAND test_deadlock)
set_tests_properties(test_deadlock PROPERTIES LABELS "tx")

add_executable(test_insert_executor
 test_insert_executor.cpp
  ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/execution/executor.cpp
  ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
)
add_test(NAME test_insert_executor COMMAND test_insert_executor)
set_tests_properties(test_insert_executor PROPERTIES LABELS "executor")

add_executable(test_seq_scan
 test_seq_scan.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/execution/executor.cpp
  ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
)
add_test(NAME test_seq_scan COMMAND test_seq_scan)
set_tests_properties(test_seq_scan PROPERTIES LABELS "executor")

add_executable(test_sql_roundtrip
 test_sql_roundtrip.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/token.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/tokenizer.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/parser.cpp
 ${PROJECT_SOURCE_DIR}/src/planner/planner.cpp
  ${PROJECT_SOURCE_DIR}/src/executor/evaluator.cpp
 ${PROJECT_SOURCE_DIR}/src/execution/executor.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
)
add_test(NAME test_sql_roundtrip COMMAND test_sql_roundtrip)
set_tests_properties(test_sql_roundtrip PROPERTIES LABELS "sql")

add_executable(test_aggregate
 test_aggregate.cpp
 ${PROJECT_SOURCE_DIR}/src/execution/executor.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
)
add_test(NAME test_aggregate COMMAND test_aggregate)
set_tests_properties(test_aggregate PROPERTIES LABELS "exec")

add_executable(test_limit
 test_limit.cpp
 ${PROJECT_SOURCE_DIR}/src/execution/executor.cpp
  ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
)
add_test(NAME test_limit COMMAND test_limit)
set_tests_properties(test_limit PROPERTIES LABELS "exec")

add_executable(test_sort
 test_sort.cpp
 ${PROJECT_SOURCE_DIR}/src/execution/executor.cpp
  ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
)
add_test(NAME test_sort COMMAND test_sort)
set_tests_properties(test_sort PROPERTIES LABELS "exec")

add_executable(test_distinct
 test_distinct.cpp
 ${PROJECT_SOURCE_DIR}/src/execution/executor.cpp
  ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
)
add_test(NAME test_distinct COMMAND test_distinct)
set_tests_properties(test_distinct PROPERTIES LABELS "exec")

add_executable(test_group_by
 test_group_by.cpp
 ${PROJECT_SOURCE_DIR}/src/execution/executor.cpp
  ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
)
add_test(NAME test_group_by COMMAND test_group_by)
set_tests_properties(test_group_by PROPERTIES LABELS "exec")

add_executable(test_join
 test_join.cpp
 ${PROJECT_SOURCE_DIR}/src/execution/executor.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
)
add_test(NAME test_join COMMAND test_join)
set_tests_properties(test_join PROPERTIES LABELS "exec")

add_executable(test_orderby
 test_orderby.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/token.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/tokenizer.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/parser.cpp
 ${PROJECT_SOURCE_DIR}/src/planner/planner.cpp
  ${PROJECT_SOURCE_DIR}/src/executor/evaluator.cpp
 ${PROJECT_SOURCE_DIR}/src/execution/executor.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
)
add_test(NAME test_orderby COMMAND test_orderby)
set_tests_properties(test_orderby PROPERTIES LABELS "sql")

add_executable(test_having_limit
 test_having_limit.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/token.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/tokenizer.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/parser.cpp
 ${PROJECT_SOURCE_DIR}/src/planner/planner.cpp
  ${PROJECT_SOURCE_DIR}/src/executor/evaluator.cpp
 ${PROJECT_SOURCE_DIR}/src/execution/executor.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
)
add_test(NAME test_having_limit COMMAND test_having_limit)
set_tests_properties(test_having_limit PROPERTIES LABELS "sql")
add_executable(test_lock_stress
 test_lock_stress.cpp
 ${PROJECT_SOURCE_DIR}/src/transaction/lock_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
)
add_test(NAME test_lock_stress COMMAND test_lock_stress)
set_tests_properties(test_lock_stress PROPERTIES LABELS "concurrency")

add_executable(test_parser_errors
 test_parser_errors.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/token.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/tokenizer.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/parser.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
)
add_test(NAME test_parser_errors COMMAND test_parser_errors)
set_tests_properties(test_parser_errors PROPERTIES LABELS "sql")

add_executable(test_planner_multi
 test_planner_multi.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/token.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/tokenizer.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/parser.cpp
 ${PROJECT_SOURCE_DIR}/src/planner/planner.cpp
  ${PROJECT_SOURCE_DIR}/src/executor/evaluator.cpp
 ${PROJECT_SOURCE_DIR}/src/execution/executor.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
)
add_test(NAME test_planner_multi COMMAND test_planner_multi)
set_tests_properties(test_planner_multi PROPERTIES LABELS "sql")

add_executable(test_recovery_restart
 test_recovery_restart.cpp
 ${PROJECT_SOURCE_DIR}/src/recovery/recovery.cpp
 ${PROJECT_SOURCE_DIR}/src/recovery/log_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/recovery/log_record.cpp
 ${PROJECT_SOURCE_DIR}/src/transaction/lock_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
)
add_test(NAME test_recovery_restart COMMAND test_recovery_restart)
set_tests_properties(test_recovery_restart PROPERTIES LABELS "recovery")

add_executable(test_buffer_pool_eviction
 test_buffer_pool_eviction.cpp
 ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
)
add_test(NAME test_buffer_pool_eviction COMMAND test_buffer_pool_eviction)
set_tests_properties(test_buffer_pool_eviction PROPERTIES LABELS "storage")

add_executable(test_multi_table_e2e
 test_multi_table_e2e.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/token.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/tokenizer.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/parser.cpp
 ${PROJECT_SOURCE_DIR}/src/planner/planner.cpp
  ${PROJECT_SOURCE_DIR}/src/executor/evaluator.cpp
 ${PROJECT_SOURCE_DIR}/src/execution/executor.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
)
add_test(NAME test_multi_table_e2e COMMAND test_multi_table_e2e)
set_tests_properties(test_multi_table_e2e PROPERTIES LABELS "sql")

add_executable(test_optimizer
 test_optimizer.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/token.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/tokenizer.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/parser.cpp
 ${PROJECT_SOURCE_DIR}/src/planner/planner.cpp
  ${PROJECT_SOURCE_DIR}/src/executor/evaluator.cpp
 ${PROJECT_SOURCE_DIR}/src/optimizer/stats.cpp
 ${PROJECT_SOURCE_DIR}/src/optimizer/optimizer.cpp
 ${PROJECT_SOURCE_DIR}/src/execution/executor.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
)
add_test(NAME test_optimizer COMMAND test_optimizer)
set_tests_properties(test_optimizer PROPERTIES LABELS "optimizer")

add_executable(test_vectorized
 test_vectorized.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/token.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/tokenizer.cpp
 ${PROJECT_SOURCE_DIR}/src/parser/parser.cpp
 ${PROJECT_SOURCE_DIR}/src/planner/planner.cpp
  ${PROJECT_SOURCE_DIR}/src/executor/evaluator.cpp
 ${PROJECT_SOURCE_DIR}/src/optimizer/stats.cpp
 ${PROJECT_SOURCE_DIR}/src/optimizer/optimizer.cpp
 ${PROJECT_SOURCE_DIR}/src/execution/executor.cpp
 ${PROJECT_SOURCE_DIR}/src/execution/vectorized.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/catalog.cpp
  ${PROJECT_SOURCE_DIR}/src/index/b_plus_tree.cpp
 ${PROJECT_SOURCE_DIR}/src/catalog/table_heap.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/heap_file.cpp
  ${PROJECT_SOURCE_DIR}/src/buffer/buffer_pool.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/columnar.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/disk_manager.cpp
 ${PROJECT_SOURCE_DIR}/src/storage/page.cpp
 ${PROJECT_SOURCE_DIR}/src/common/types.cpp
 ${PROJECT_SOURCE_DIR}/src/common/rid.cpp
 ${PROJECT_SOURCE_DIR}/src/common/tuple.cpp
)
add_test(NAME test_vectorized COMMAND test_vectorized)
set_tests_properties(test_vectorized PROPERTIES LABELS "vectorized")
