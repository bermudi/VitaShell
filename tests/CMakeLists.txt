set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

function(add_vitashell_host_test name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -Wpedantic)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_vitashell_host_test(test_refresh_core
  test_refresh_core.c
  ../refresh_core.c
)

add_vitashell_host_test(test_pfs_core
  test_pfs_core.c
  ../pfs_core.c
)
