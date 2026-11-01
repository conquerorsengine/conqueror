project(cqs)

# ConquerorScript komut satırı aracı: betik çalıştırma, REPL, testler ve kıyaslamalar
add_executable(cqs main.cpp)
target_include_directories(cqs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/src)
target_link_libraries(cqs PRIVATE Engine)
set_target_properties(cqs PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
cq_apply_sanitizers(cqs)
