option(WITH_DIRECTX "Build directx.hdll." ON)

if(WITH_DIRECTX)
    find_package(DirectX)
    if(WIN32 AND NOT SDL3_DIR)
        set(SDL3_DIR ${INCLUDES_BASE_DIR}/sdl/cmake)
    endif()
    find_package(SDL3 CONFIG QUIET)

    add_library(directx.hdll
        directx.cpp
        gamecontroller.c
        window.c
    )

    set_as_hdll(directx)

    target_link_libraries(directx.hdll
        libhl
        d3d11
        dxgi
        d3dcompiler
        xinput9_1_0
        dinput8
    )
    if(TARGET SDL3::SDL3)
        target_link_libraries(directx.hdll SDL3::SDL3)
        target_compile_definitions(directx.hdll PRIVATE HL_DIRECTX_HAS_SDL)
    endif()

    install(
        TARGETS
            directx.hdll
        DESTINATION ${HDLL_DESTINATION}
    )
endif()

option(WITH_DX12 "Build dx12.hdll." ON)

if(WITH_DX12)
    if(WIN32 AND NOT SDL3_DIR)
        set(SDL3_DIR ${INCLUDES_BASE_DIR}/sdl/cmake)
    endif()
    find_package(SDL3 CONFIG QUIET)

    add_library(dx12.hdll
        dx12.cpp
    )

    set_as_hdll(dx12)

    target_link_libraries(dx12.hdll
        libhl
        d3d12
        dxgi
        dxcompiler
    )
    if(TARGET SDL3::SDL3)
        target_link_libraries(dx12.hdll SDL3::SDL3)
        target_compile_definitions(dx12.hdll PRIVATE HL_DIRECTX_HAS_SDL)
    endif()

    install(
        TARGETS
            dx12.hdll
        DESTINATION ${HDLL_DESTINATION}
    )
endif()
