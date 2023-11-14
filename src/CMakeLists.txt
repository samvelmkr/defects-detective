set(AnalyzerSources
    Sarif.cpp
    SimplePass.cpp
    Checker.cpp
    FuncAnalyzer.cpp)

add_library(Analyzer SHARED ${AnalyzerSources}
        ../include/SimplePass.h
        ../include/Checker.h
        ../include/FuncAnalyzer.h)

find_package(LLVM 12 REQUIRED)

if (NOT "12" VERSION_EQUAL "${LLVM_VERSION_MAJOR}")
    message(FATAL_ERROR "Found LLVM ${LLVM_VERSION}, but need 12")
endif ()

target_include_directories(Analyzer PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_include_directories(Analyzer PRIVATE ${LLVM_INCLUDE_DIRS})

target_link_libraries(Analyzer PRIVATE ${LLVM_LIBS})
message(STATUS "LLVM version: ${LLVM_VERSION}")
