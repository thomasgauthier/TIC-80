################################
# bin2txt cart2prj prj2cart xplode wasmp2cart
################################

set(TOOLS_DIR ${CMAKE_SOURCE_DIR}/build/tools)

if(UNIX)
    add_executable(tic80ctl ${TOOLS_DIR}/tic80ctl.c)
    if(LINUX)
        target_link_libraries(tic80ctl m)
    endif()
    install(TARGETS tic80ctl RUNTIME DESTINATION bin)
endif()

if(EMSCRIPTEN)
    add_executable(tic80ctl-browser-core ${TOOLS_DIR}/tic80ctl_browser.c)
    set_target_properties(tic80ctl-browser-core PROPERTIES
        OUTPUT_NAME "tic80ctl-browser-core"
        LINK_FLAGS "-s WASM=1 -s ASYNCIFY=1 -s ASYNCIFY_STACK_SIZE=65536 -s MODULARIZE=1 -s EXPORT_ES6=1 -s ENVIRONMENT=web -s EXPORTED_FUNCTIONS=['_tic80ctl_browser_run_from_json'] -s EXPORTED_RUNTIME_METHODS=['ccall']"
    )
    configure_file(${CMAKE_SOURCE_DIR}/build/webapp/tic80ctl-browser.js ${CMAKE_BINARY_DIR}/bin/tic80ctl-browser.js COPYONLY)
    configure_file(${CMAKE_SOURCE_DIR}/build/webapp/tic80ctl-browser-host.mjs ${CMAKE_BINARY_DIR}/bin/tic80ctl-browser-host.mjs COPYONLY)
    configure_file(${CMAKE_SOURCE_DIR}/build/webapp/tic80ctl-browser-demo.html ${CMAKE_BINARY_DIR}/bin/tic80ctl-browser-demo.html COPYONLY)
    configure_file(${CMAKE_SOURCE_DIR}/build/webapp/tic80ctl-demo.html ${CMAKE_BINARY_DIR}/bin/tic80ctl-demo.html COPYONLY)
endif()

if(BUILD_TOOLS)

    add_executable(cart2prj ${TOOLS_DIR}/cart2prj.c ${CMAKE_SOURCE_DIR}/src/studio/project.c)
    target_include_directories(cart2prj PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(cart2prj tic80core)

    add_executable(prj2cart ${TOOLS_DIR}/prj2cart.c ${CMAKE_SOURCE_DIR}/src/studio/project.c)
    target_include_directories(prj2cart PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(prj2cart tic80core)

    add_executable(wasmp2cart ${TOOLS_DIR}/wasmp2cart.c ${CMAKE_SOURCE_DIR}/src/studio/project.c)
    target_include_directories(wasmp2cart PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(wasmp2cart tic80core)

    add_executable(bin2txt ${TOOLS_DIR}/bin2txt.c)
    target_link_libraries(bin2txt zlib)

    add_executable(xplode
        ${TOOLS_DIR}/xplode.c
        ${CMAKE_SOURCE_DIR}/src/ext/png.c
        ${CMAKE_SOURCE_DIR}/src/studio/project.c)

    target_include_directories(xplode PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(xplode tic80core png)

    if(LINUX)
        target_link_libraries(xplode m)
    endif()

endif()
