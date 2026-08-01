find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)

# Each test compiles the sources it exercises directly, rather than linking the
# application target: the app is a QML module whose type registration pulls in
# the whole GUI stack, and none of the logic under test needs it.
#
# Tests are headless and touch neither the network nor the user's Todoist
# account, so they are safe to run anywhere.
function(ktodo_add_test name)
    qt_add_executable(${name} ${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
    )
    target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

ktodo_add_test(richtexttest
    ${CMAKE_SOURCE_DIR}/src/util/richtext.cpp
)

ktodo_add_test(colorstest
    ${CMAKE_SOURCE_DIR}/src/util/colors.cpp
)
target_link_libraries(colorstest PRIVATE Qt6::Gui)

ktodo_add_test(duedatetest
    ${CMAKE_SOURCE_DIR}/src/util/duedate.cpp
    ${CMAKE_SOURCE_DIR}/src/data/types.cpp
)
target_link_libraries(duedatetest PRIVATE KF6::I18n)

# FilterQuery declares a Repository parameter and so needs its symbols at link
# time, but every test here passes a null repository, which the evaluator
# handles explicitly. Repository drags in the SQLite layer, hence Qt6::Sql.
ktodo_add_test(filterquerytest
    ${CMAKE_SOURCE_DIR}/src/query/filterquery.cpp
    ${CMAKE_SOURCE_DIR}/src/util/duedate.cpp
    ${CMAKE_SOURCE_DIR}/src/data/types.cpp
    ${CMAKE_SOURCE_DIR}/src/data/repository.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database.cpp
    ${CMAKE_SOURCE_DIR}/src/sync/commandqueue.cpp
)
target_link_libraries(filterquerytest PRIVATE Qt6::Sql Qt6::Gui KF6::I18n)
