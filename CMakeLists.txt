cmake_minimum_required(VERSION 3.10)

project(HestonProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_path(QUANTLIB_INCLUDE_DIR
	NAMES ql/quantlib.hpp
	PATHS
		C:/msys64/mingw64/include
		C:/msys64/ucrt64/include
		C:/MinGW/include
)

find_library(QUANTLIB_LIBRARY
	NAMES QuantLib libQuantLib
	PATHS
		C:/msys64/mingw64/lib
		C:/msys64/mingw64/bin
		C:/msys64/ucrt64/lib
		C:/msys64/ucrt64/bin
		C:/MinGW/lib
)

	find_path(BOOST_INCLUDE_DIR
		NAMES boost/config.hpp
		PATHS
			C:/msys64/ucrt64/include
			C:/msys64/mingw64/include
			C:/MinGW/include
	)

	if(NOT QUANTLIB_INCLUDE_DIR OR NOT QUANTLIB_LIBRARY OR NOT BOOST_INCLUDE_DIR)
		message(FATAL_ERROR "QuantLib or Boost headers/library were not found. Set QUANTLIB_INCLUDE_DIR, QUANTLIB_LIBRARY, and BOOST_INCLUDE_DIR manually.")
endif()

add_executable(main
	src/main.cpp
)

target_include_directories(main PRIVATE include ${QUANTLIB_INCLUDE_DIR} ${BOOST_INCLUDE_DIR})
target_link_libraries(main PRIVATE ${QUANTLIB_LIBRARY})

add_executable(test_runner tests/test.cpp)

target_include_directories(test_runner PRIVATE include ${QUANTLIB_INCLUDE_DIR} ${BOOST_INCLUDE_DIR})
target_link_libraries(test_runner PRIVATE ${QUANTLIB_LIBRARY})
