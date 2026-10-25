# The SDK's add_xbe_image (cmake/XbeRecomp.cmake) with a DLL in place of each image's executable: the image's runtime
# <target>Runtime.dll, which nightfire.exe loads, so one executable starts either image. The image's code still goes
# to <target>.dll, linked against the runtime's exports, and runtime/app/game_code.cpp builds it on the player's PC.
function(add_image_module target xbe config partitions game_dir)
    set(GEN ${CMAKE_CURRENT_SOURCE_DIR}/generated/${game_dir})
    set(GEN_SOURCES ${GEN}/x86_func_mapping.cpp)
    math(EXPR last "${partitions} - 1")
    foreach(i RANGE ${last})
        list(APPEND GEN_SOURCES ${GEN}/x86_recomp.${i}.cpp)
    endforeach()
    file(GLOB configs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/config/*.toml ${XBE_SDK_DIR}/xdk/*/*.toml)
    add_custom_command(
        OUTPUT ${GEN}/codegen.stamp
        BYPRODUCTS ${GEN_SOURCES} ${GEN}/x86_recomp_shared.h ${GEN}/image_symbols.h ${GEN}/hook_exports.h
                   ${GEN}/exports.def ${GEN}/imports.def ${GEN}/code.lib
        COMMAND xbe_recomp config/${config} --xdk ${XBE_SDK_DIR}/xdk
        COMMAND ${XBE_LLVM_BIN}/lld-link.exe /lib /machine:x64 /def:${GEN}/imports.def /out:${GEN}/code.lib
        COMMAND ${CMAKE_COMMAND} -E touch ${GEN}/codegen.stamp
        DEPENDS xbe_recomp ${configs} ${CMAKE_CURRENT_SOURCE_DIR}/assets/${xbe}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Recompiling ${xbe}"
        VERBATIM)
    add_custom_target(recompile_${target} DEPENDS ${GEN}/codegen.stamp)
    file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${game_dir}/*.cpp)
    # the xbe's SHA-1: the runtime checks the game it finds is the release it was made from
    set(xbe_file ${CMAKE_CURRENT_SOURCE_DIR}/assets/${xbe})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${xbe_file})
    file(SHA1 ${xbe_file} xbe_sha1)
    set(XBE_HASH ${CMAKE_CURRENT_BINARY_DIR}/${target}_xbe.cpp)
    file(CONFIGURE OUTPUT ${XBE_HASH} CONTENT "extern const char kImageSha1[] = \"${xbe_sha1}\";\n")
    add_library(${target} SHARED ${XBE_RUNTIME_SOURCES} ${XBE_HOST_SOURCES} ${GAME_SOURCES} ${XBE_IMGUI_SOURCES} ${XBE_HASH})
    add_dependencies(${target} recompile_${target} compiler_files)
    # the import library of what the runtime exports to the code DLL goes to the compiler folder
    set_target_properties(${target} PROPERTIES OUTPUT_NAME ${target}Runtime ARCHIVE_OUTPUT_DIRECTORY ${XBE_COMPILER_DIR})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${XBE_SDK_DIR}/runtime ${GEN})
    target_include_directories(${target} SYSTEM PRIVATE ${imgui_SOURCE_DIR})
    target_compile_options(${target} PRIVATE -fno-strict-aliasing)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(${target} PRIVATE tomlplusplus::tomlplusplus $<LINK_ONLY:SDL3::SDL3-static> d3d12 dxgi dxguid
        d3dcompiler xaudio2 ole32 windowscodecs dbghelp winmm dwmapi ws2_32)
    target_link_libraries(${target} PRIVATE ${GEN}/code.lib delayimp)
    target_link_options(${target} PRIVATE -fuse-ld=lld -g -Wl,/delayload:${target}.dll)

    # without a C runtime: the runtime passes on what the code calls
    add_library(${target}_code SHARED ${GEN_SOURCES})
    set_target_properties(${target}_code PROPERTIES OUTPUT_NAME ${target} CXX_EXTENSIONS OFF)
    set_source_files_properties(${GEN_SOURCES} PROPERTIES GENERATED TRUE)
    add_dependencies(${target}_code recompile_${target})
    target_include_directories(${target}_code PRIVATE ${XBE_SDK_DIR}/runtime ${GEN})
    target_include_directories(${target}_code SYSTEM PRIVATE ${XBE_SDK_DIR}/runtime/cpu/freestanding)
    target_compile_definitions(${target}_code PRIVATE X86_GAME_CODE)
    target_compile_options(${target}_code PRIVATE -fms-compatibility-version=19.40 -O2 -fno-strict-aliasing -nostdinc
        -nostdinc++ -w)
    target_link_libraries(${target}_code PRIVATE ${target})
    target_link_options(${target}_code PRIVATE -fuse-ld=lld -Wl,/noentry,/nodefaultlib,/opt:noicf
        -Wl,/def:${GEN}/exports.def)
    # a stamp from an earlier build on the first start would outrank this DLL
    add_custom_command(TARGET ${target}_code POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f ${XBE_COMPILER_DIR}/work/${target}/built.txt)
endfunction()
