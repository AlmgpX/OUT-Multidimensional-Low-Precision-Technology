# SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
# Copyright (c) 2026 Алексей Савельев (Alex Merqury). Source review only; see LICENSE.
cmake_minimum_required(VERSION 3.20)

project(OUTMotorNative LANGUAGES CXX)

add_library(OUTMotorNative SHARED OUTMotorNative.cpp)

target_compile_features(OUTMotorNative PRIVATE cxx_std_17)

find_package(Threads REQUIRED)

target_link_libraries(OUTMotorNative PRIVATE Threads::Threads)

if(MSVC)

 target_compile_options(OUTMotorNative PRIVATE /O2 /fp:precise)

 set_property(TARGET OUTMotorNative PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

else()

 target_compile_options(OUTMotorNative PRIVATE -O3 -fno-fast-math)

endif()
