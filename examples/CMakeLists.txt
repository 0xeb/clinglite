# clinglite examples

foreach(example basic crash_recovery repl value_types pch_generation session error_handling)
    add_executable(${example} ${example}.cpp)
    target_link_libraries(${example} PRIVATE clinglite)
    set_target_properties(${example} PROPERTIES ENABLE_EXPORTS ON)
endforeach()
