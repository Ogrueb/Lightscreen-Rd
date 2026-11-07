# One executable per test, each linking the app's sources (lightscreen_core), so a file added to the app
# goes into the root CMakeLists.txt only. Each test is built into its own directory, as with qmake:
# tst_history and tst_optimizer put files next to their executable.
find_package(Qt6 REQUIRED COMPONENTS Test)

function(lightscreen_add_test name)
    add_executable(${name} ${name}/${name}.cpp)
    target_link_libraries(${name} PRIVATE lightscreen_core Qt6::Test)
    # $<1:...> stops multi-config generators from adding a per-configuration subdirectory.
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/${name}>")
    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${name}")
endfunction()

lightscreen_add_test(tst_smoke)
lightscreen_add_test(tst_history)
lightscreen_add_test(tst_screenshot)
lightscreen_add_test(tst_settings)
lightscreen_add_test(tst_uploader)
lightscreen_add_test(tst_areadialog)
lightscreen_add_test(tst_capturegeometry)
lightscreen_add_test(tst_hdrcapture)
lightscreen_add_test(tst_os)
lightscreen_add_test(tst_hotkeywidget)
lightscreen_add_test(tst_globalhotkeys)
lightscreen_add_test(tst_mainwindow)
lightscreen_add_test(tst_updater)
lightscreen_add_test(tst_optimizer)

# A stand-in for oxipng and optipng, used by tst_optimizer. Built under both names, each into its own directory under
# tst_optimizer's stub/, so a test can put either one, both or neither on PATH (or next to the test on Windows).
set(optimizer_stub_dir "${CMAKE_CURRENT_BINARY_DIR}/tst_optimizer/stub")
foreach(tool oxipng optipng)
    add_executable(optimizer_stub_${tool} optimizer_stub/main.cpp)
    set_target_properties(optimizer_stub_${tool} PROPERTIES
        OUTPUT_NAME ${tool}
        RUNTIME_OUTPUT_DIRECTORY "$<1:${optimizer_stub_dir}/${tool}>"
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    )
    add_dependencies(tst_optimizer optimizer_stub_${tool})
endforeach()
target_compile_definitions(tst_optimizer PRIVATE OPTIMIZER_STUB_DIR="${optimizer_stub_dir}")
