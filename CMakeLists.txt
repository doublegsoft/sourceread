##
##                                                         __
##    _________  __  _______________  ________  ____ _____/ /
##   / ___/ __ \/ / / / ___/ ___/ _ \/ ___/ _ \/ __ `/ __  / 
##  (__  ) /_/ / /_/ / /  / /__/  __/ /  /  __/ /_/ / /_/ /  
## /____/\____/\__,_/_/   \___/\___/_/   \___/\__,_/\__,_/  
##
cmake_minimum_required(VERSION 3.16)

project(together LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(BUILD "build/darwin")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(BUILD "build/linux")
elseif (MINGW)
  set(BUILD "build/mingw")
elseif (WIN32)
  set(BUILD "build/windows")
else()
  message(FATAL_ERROR "Unsupported operating system: '${CMAKE_SYSTEM_NAME}'. Configuration halted.")
endif() 

################################################################################
##                                                                            ##
##                                   argparse                                 ##
##                                                                            ##
################################################################################
set(ARGPARSE_ROOT                     "${CMAKE_CURRENT_SOURCE_DIR}/../sourceread-3rd/argparse-1.1.0")
set(ARGPARSE_INCLUDE_DIR              "${ARGPARSE_ROOT}")
set(ARGPARSE_LIBRARY_DIR              "${ARGPARSE_ROOT}/${BUILD}")
set(ARGPARSE_LIBRARIES                argparse)

################################################################################
##                                                                            ##
##                                  tree-sitter                               ##
##                                                                            ##
################################################################################
set(TREE_SITTER_ROOT                  "${CMAKE_CURRENT_SOURCE_DIR}/../sourceread-3rd/tree-sitter-0.27.0")
set(TREE_SITTER_INCLUDE_DIR           "${TREE_SITTER_ROOT}/src"
                                      "${TREE_SITTER_ROOT}/lib/include")
set(TREE_SITTER_LIBRARY_DIR           "${TREE_SITTER_ROOT}/${BUILD}")
set(TREE_SITTER_LIBRARIES             "${TREE_SITTER_LIBRARY_DIR}/libtree-sitter.a")

################################################################################
##                                                                            ##
##                               tree-sitter-java                             ##
##                                                                            ##
################################################################################
set(TREE_SITTER_JAVA_ROOT             "${CMAKE_CURRENT_SOURCE_DIR}/../sourceread-3rd/tree-sitter-java-0.23.5")
set(TREE_SITTER_JAVA_INCLUDE_DIR      "${TREE_SITTER_JAVA_ROOT}/src")
set(TREE_SITTER_JAVA_LIBRARY_DIR      "${TREE_SITTER_JAVA_ROOT}/${BUILD}")
set(TREE_SITTER_JAVA_LIBRARIES        "${TREE_SITTER_JAVA_LIBRARY_DIR}/libtree-sitter-java.a")

################################################################################
##                                                                            ##
##                               tree-sitter-sql                              ##
##                                                                            ##
################################################################################
set(TREE_SITTER_SQL_ROOT             "${CMAKE_CURRENT_SOURCE_DIR}/../sourceread-3rd/tree-sitter-sql-0.3.11")
set(TREE_SITTER_SQL_INCLUDE_DIR      "${TREE_SITTER_SQL_ROOT}/src")
set(TREE_SITTER_SQL_LIBRARY_DIR      "${TREE_SITTER_SQL_ROOT}/${BUILD}")
set(TREE_SITTER_SQL_LIBRARIES        "${TREE_SITTER_SQL_LIBRARY_DIR}/libtree-sitter-sql.a")

################################################################################
##                                                                            ##
##                                tree-sitter-c                               ##
##                                                                            ##
################################################################################
set(TREE_SITTER_C_ROOT             "${CMAKE_CURRENT_SOURCE_DIR}/../sourceread-3rd/tree-sitter-c-0.24.2")
set(TREE_SITTER_C_INCLUDE_DIR      "${TREE_SITTER_C_ROOT}/src")
set(TREE_SITTER_C_LIBRARY_DIR      "${TREE_SITTER_C_ROOT}/${BUILD}")
set(TREE_SITTER_C_LIBRARIES        "${TREE_SITTER_C_LIBRARY_DIR}/libtree-sitter-c.a")

################################################################################
##                                                                            ##
##                                 sourceread                                 ##
##                                                                            ##
################################################################################

include_directories(
  "src"
  "${ARGPARSE_INCLUDE_DIR}"
  "${TREE_SITTER_INCLUDE_DIR}"
  "${TREE_SITTER_JAVA_INCLUDE_DIR}"
  "${TREE_SITTER_C_INCLUDE_DIR}"
)

link_directories(
  "${ARGPARSE_LIBRARY_DIR}"
  "${TREE_SITTER_LIBRARY_DIR}"
  "${TREE_SITTER_JAVA_LIBRARY_DIR}"
)

add_library(sourceread STATIC
  src/sourceread-file.c
  src/sourceread-graph.c
  src/sourceread-storage.c
  src/sourceread-treesitter.c
)

################################################################################
##                                                                            ##
##                                    tool                                    ##
##                                                                            ##
################################################################################

add_executable(sourceread4java tool/sourceread4java.c)

target_link_libraries(sourceread4java PRIVATE
  sourceread
  ${ARGPARSE_LIBRARIES}
  ${TREE_SITTER_LIBRARIES}
  ${TREE_SITTER_JAVA_LIBRARIES}
)

add_executable(sourceread4sql tool/sourceread4sql.c)

target_link_libraries(sourceread4sql PRIVATE
  sourceread
  ${ARGPARSE_LIBRARIES}
  ${TREE_SITTER_LIBRARIES}
  ${TREE_SITTER_SQL_LIBRARIES}
)

add_executable(sourceread4c tool/sourceread4c.c)

target_link_libraries(sourceread4c PRIVATE
  sourceread
  ${ARGPARSE_LIBRARIES}
  ${TREE_SITTER_LIBRARIES}
  ${TREE_SITTER_C_LIBRARIES}
)

# add_executable(sourceread-storage src/sourceread-storage.c)

# target_link_libraries(sourceread-storage PRIVATE
#   sourceread
#   ${ARGPARSE_LIBRARIES}
#   ${TREE_SITTER_LIBRARIES}
#   ${TREE_SITTER_C_LIBRARIES}
# )

