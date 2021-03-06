cmake_minimum_required (VERSION 3.2)

project (btc_address_parser VERSION 1.0 LANGUAGES CXX)

add_compile_options(
    -Wall
    -Wcast-align
    -Wcast-qual
    -Wconversion
    -Wctor-dtor-privacy
    -Wenum-compare
    -Wfloat-equal
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Woverloaded-virtual
    -Wredundant-decls
    -Wsign-conversion
    -Wsign-promo
)

if(NOT CMAKE_CXX_EXTENSIONS)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# OpenSSL dependency
find_package( OpenSSL )
include_directories(${OPENSSL_INCLUDE_DIR})

# btcutils library
add_subdirectory(btc_utils)

# utils
add_subdirectory(addr_parser)

