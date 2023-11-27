set(AnalyzerSources
    Sarif.cpp
    SimplePass.cpp
        Analyzer.cpp
    Checker.cpp
        FuncInfo.cpp
    MLChecker.cpp
    UAFChecker.cpp
    BOFChecker.cpp)

add_library(Analyzer SHARED ${AnalyzerSources}
        ../include/SimplePass.h
        ../include/Analyzer.h
        ../include/Checker.h
        ../include/FuncInfo.h
        ../include/MLChecker.h
        ../include/UAFChecker.h)

find_package(LLVM 12 REQUIRED)

if (NOT "12" VERSION_EQUAL "${LLVM_VERSION_MAJOR}")
    message(FATAL_ERROR "Found LLVM ${LLVM_VERSION}, but need 12")
endif ()

target_include_directories(Analyzer PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_include_directories(Analyzer PRIVATE ${LLVM_INCLUDE_DIRS})

target_link_libraries(Analyzer PRIVATE ${LLVM_LIBS})
message(STATUS "LLVM version: ${LLVM_VERSION}")
