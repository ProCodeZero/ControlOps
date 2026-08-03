cmake_minimum_required(VERSION 3.18)
project(RusAgroBack_Control_Operations LANGUAGES CXX)

# --- Общие настройки ---
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

option(BUILD_TESTS "Build unit tests" OFF)
option(BUILD_SHARED_LIBS "Build shared libs" ON)

# --- Полезные флаги компиляции (GCC/Clang / MSVC) ---
if(MSVC)
  add_compile_options(/W4 /permissive- /EHsc /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  # you can add -march=native for local builds:
  # add_compile_options(-march=native)
endif()

# ------------------- Зависимости -------------------
find_package(spdlog CONFIG REQUIRED)        # spdlog::spdlog_header_only или spdlog::spdlog

# boost (date_time используется в вашем коде)
find_package(Boost REQUIRED COMPONENTS date_time)

# Попробуем libpqxx через pkg-config; если не найдём — попытаемся найти вручную
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PQXX QUIET libpqxx)
endif()

if(NOT PQXX_FOUND)
  find_path(PQXX_INCLUDE_DIR NAMES pqxx/pqxx)
  find_library(PQXX_LIBRARY NAMES pqxx pqxx-static)
  if(PQXX_INCLUDE_DIR AND PQXX_LIBRARY)
    set(PQXX_FOUND TRUE)
    set(PQXX_INCLUDE_DIRS ${PQXX_INCLUDE_DIR})
    set(PQXX_LIBRARIES ${PQXX_LIBRARY})
  else()
    message(STATUS "libpqxx not found by pkg-config or find_path. If you need PostgreSQL support, install libpqxx or set PQXX_INCLUDE_DIR / PQXX_LIBRARY.")
  endif()
else()
  # pkg-config возвращает переменные, установим в cmake-переменные
  set(PQXX_INCLUDE_DIRS ${PQXX_INCLUDE_DIRS})
  set(PQXX_LIBRARIES ${PQXX_LIBRARIES})
endif()

# yaml-cpp — опционально, но если используете, лучше установить
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
  message(STATUS "yaml-cpp not found via find_package; if you parse YAML configs, install yaml-cpp (libyaml-cpp-dev / vcpkg).")
endif()

# ------------------- Источники -------------------
file(GLOB_RECURSE SRC_FILES
  ${CMAKE_SOURCE_DIR}/src/*.cpp
  ${CMAKE_SOURCE_DIR}/src/*/*.cpp
)

file(GLOB_RECURSE HDR_FILES
  ${CMAKE_SOURCE_DIR}/src/*.hpp
  ${CMAKE_SOURCE_DIR}/src/*.h
  ${CMAKE_SOURCE_DIR}/include/*.hpp
)

add_executable(RusAgroBack_Control_Operations
  ${SRC_FILES}
  ${HDR_FILES}
)

# include dirs (src чтобы текущие includes работали, плюс include/)
target_include_directories(RusAgroBack_Control_Operations PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/include
)

# ------------------- Линковка -------------------
# spdlog
target_link_libraries(RusAgroBack_Control_Operations PRIVATE
  spdlog::spdlog_header_only   # используем header-only API; если у вас установлена compiled spdlog, можно заменить на spdlog::spdlog
)

# boost includes / libs
if(Boost_FOUND)
  target_include_directories(RusAgroBack_Control_Operations PRIVATE ${Boost_INCLUDE_DIRS})
  target_link_libraries(RusAgroBack_Control_Operations PRIVATE ${Boost_LIBRARIES})
endif()

# libpqxx
if(PQXX_FOUND)
  target_include_directories(RusAgroBack_Control_Operations PRIVATE ${PQXX_INCLUDE_DIRS})
  target_link_libraries(RusAgroBack_Control_Operations PRIVATE ${PQXX_LIBRARIES})
else()
  message(STATUS "Warning: libpqxx not linked. Database features will fail at runtime if you try to use them.")
endif()

# yaml-cpp
if(yaml-cpp_FOUND)
  target_link_libraries(RusAgroBack_Control_Operations PRIVATE yaml-cpp)
endif()

# Windows specifics
if(WIN32)
  target_link_libraries(RusAgroBack_Control_Operations PRIVATE ws2_32)
endif()

# ------------------- Опции установки -------------------
install(TARGETS RusAgroBack_Control_Operations
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

# ------------------- Тесты -------------------
if(BUILD_TESTS)
  enable_testing()
  add_executable(tests
    ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_input_aggregation.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/InputAggregation.cpp
  )
  target_include_directories(tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
  )
  add_test(NAME unit_tests COMMAND tests)
endif()

# ------------------- Полезные подсказки -------------------
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
if(PQXX_FOUND)
  message(STATUS "libpqxx: linked")
endif()
if(yaml-cpp_FOUND)
  message(STATUS "yaml-cpp: linked")
endif()

# copy single config file into output/config/db_config.yml
add_custom_command(TARGET RusAgroBack_Control_Operations
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RusAgroBack_Control_Operations>/config"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/config/db_config.yml"
          "$<TARGET_FILE_DIR:RusAgroBack_Control_Operations>/config/db_config.yml"
  COMMENT "Copy config/db_config.yml to $<TARGET_FILE_DIR:RusAgroBack_Control_Operations>/config/"
)