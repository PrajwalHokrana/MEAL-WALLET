cmake_minimum_required(VERSION 3.14)
project(MealWallet VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -pedantic -pthread)
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)

set(SOURCES
    src/main.cpp
    src/DomainImpl.cpp
    src/ConsoleAndTests.cpp
    src/repository/MealWalletService.cpp
    src/api/ApiServer.cpp
)

add_executable(mealwallet ${SOURCES})

if(WIN32)
    target_link_libraries(mealwallet PRIVATE ws2_32)
else()
    target_link_libraries(mealwallet PRIVATE pthread)
endif()
