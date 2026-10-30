find_program(NOGRAPHICSAPI_SLANGC NAMES slangc REQUIRED)
if(NOGRAPHICSAPI_USE_WEBGPU)
    set(NOGRAPHICSAPI_SHADER_EXTENSION wgsl CACHE INTERNAL "Native shader artifact extension" FORCE)
    set(NOGRAPHICSAPI_SLANG_MINIMUM 2026.18.2)
elseif(APPLE)
    find_program(NOGRAPHICSAPI_XCRUN NAMES xcrun REQUIRED)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(NOGRAPHICSAPI_METAL_SDK iphoneos)
        set(NOGRAPHICSAPI_METAL_TARGET "air64-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET}")
    else()
        set(NOGRAPHICSAPI_METAL_SDK macosx)
        set(NOGRAPHICSAPI_METAL_TARGET "air64-apple-macos${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
    set(NOGRAPHICSAPI_SHADER_EXTENSION metallib CACHE INTERNAL "Native shader artifact extension" FORCE)
    set(NOGRAPHICSAPI_SLANG_MINIMUM 2026.18.2)
else()
    find_program(NOGRAPHICSAPI_SPIRV_VAL NAMES spirv-val REQUIRED)
    set(NOGRAPHICSAPI_SHADER_EXTENSION spv CACHE INTERNAL "Native shader artifact extension" FORCE)
    set(NOGRAPHICSAPI_SLANG_MINIMUM 2026.14.1)
endif()

function(NoGraphicsAPI_require_tool_version program argument name minimum pattern)
    execute_process(
        COMMAND ${program} ${argument}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    string(STRIP "${output}${error}" version_text)
    string(REGEX MATCH "${pattern}" match "${version_text}")
    set(version "${CMAKE_MATCH_1}")
    if(NOT result EQUAL 0 OR
       NOT match OR
       version VERSION_LESS minimum)
        message(FATAL_ERROR
            "NoGraphicsAPI examples require ${name} ${minimum} or newer; "
            "found '${version_text}' at ${program}")
    endif()
endfunction()

NoGraphicsAPI_require_tool_version(
    "${NOGRAPHICSAPI_SLANGC}" -version Slang ${NOGRAPHICSAPI_SLANG_MINIMUM}
    "([0-9]+\\.[0-9]+(\\.[0-9]+)?)")
if(NOT APPLE AND NOT EMSCRIPTEN)
    NoGraphicsAPI_require_tool_version(
        "${NOGRAPHICSAPI_SPIRV_VAL}" --version SPIRV-Tools 2026.3
        "SPIRV-Tools v([0-9]+\\.[0-9]+)")
endif()

function(NoGraphicsAPI_compile_slang output source entry stage)
    cmake_parse_arguments(SLANG "" "" "DEPENDS;DEFINES;OPTIONS" ${ARGN})
    if(NOGRAPHICSAPI_USE_WEBGPU AND stage STREQUAL "mesh")
        set(stage "vertex")
    endif()
    get_filename_component(output_dir "${output}" DIRECTORY)
    set(dependencies ${source} ${SLANG_DEPENDS}
        ${PROJECT_SOURCE_DIR}/include/NoGraphicsAPI/types.h
        ${PROJECT_SOURCE_DIR}/include/NoGraphicsAPI/shader.slang
        ${PROJECT_SOURCE_DIR}/utility/include/NoGraphicsAPIUtility/shader_types.h)
    set(common_options
        -entry ${entry} -stage ${stage}
        -fvk-use-c-layout -matrix-layout-row-major
        -I ${CMAKE_CURRENT_SOURCE_DIR}
        -I ${PROJECT_SOURCE_DIR}/include
        -I ${PROJECT_SOURCE_DIR}/utility/include)
    foreach(define IN LISTS SLANG_DEFINES)
        list(APPEND common_options -D${define})
    endforeach()
    list(APPEND common_options ${SLANG_OPTIONS})
    if(EMSCRIPTEN)
        list(APPEND common_options -D__EMSCRIPTEN__)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8 OR "${CMAKE_CXX_FLAGS} ${CMAKE_EXE_LINKER_FLAGS} ${SLANG_DEFINES} ${SLANG_OPTIONS}" MATCHES "MEMORY64=[12]|wasm64")
            list(APPEND common_options -D__wasm64__)
        else()
            list(APPEND common_options -D__wasm32__)
        endif()
    endif()
    if(NOGRAPHICSAPI_USE_WEBGPU)
        string(TOUPPER "${stage}" stage_upper)
        string(TOLOWER "${stage}" stage_lower)
        add_custom_command(
            OUTPUT ${output}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
            COMMAND ${NOGRAPHICSAPI_SLANGC} ${source} -target wgsl -DNOGRAPHICSAPI_WEBGPU
                -fwgsl-bind-bda-buffer 0 0
                -fvk-invert-y
                -D__${stage_upper}__ -D__${stage_lower}__
                -DNOGRAPHICSAPI_STAGE_${stage_upper} -DNOGRAPHICSAPI_STAGE_${stage_lower}
                ${common_options} -o ${output}
            DEPENDS ${dependencies}
            VERBATIM
            COMMENT "Compiling Slang ${stage} shader ${entry} to WGSL"
        )
    elseif(APPLE)
        add_custom_command(
            OUTPUT ${output}
            BYPRODUCTS ${output}.metal ${output}.air
            COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
            COMMAND ${NOGRAPHICSAPI_SLANGC} ${source} -target metal -DNOGRAPHICSAPI_METAL
                ${common_options} -o ${output}.metal
            COMMAND ${NOGRAPHICSAPI_XCRUN} -sdk ${NOGRAPHICSAPI_METAL_SDK} metal -std=metal4.0
                -target ${NOGRAPHICSAPI_METAL_TARGET} -c ${output}.metal -o ${output}.air
            COMMAND ${NOGRAPHICSAPI_XCRUN} -sdk ${NOGRAPHICSAPI_METAL_SDK} metallib ${output}.air -o ${output}
            DEPENDS ${dependencies}
            VERBATIM
            COMMENT "Compiling Slang ${stage} shader ${entry} to metallib"
        )
    else()
        set(options -capability spvDescriptorHeapEXT)
        if(stage STREQUAL "mesh" OR stage STREQUAL "amplification")
            list(APPEND options -capability spvMeshShadingEXT)
        endif()
        add_custom_command(
            OUTPUT ${output}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
            COMMAND ${NOGRAPHICSAPI_SLANGC} ${source} -target spirv -profile spirv_1_5
                -emit-spirv-directly -fvk-use-entrypoint-name ${common_options} ${options} -o ${output}
            COMMAND ${NOGRAPHICSAPI_SPIRV_VAL} --target-env vulkan1.4 --scalar-block-layout ${output}
            DEPENDS ${dependencies}
            VERBATIM
            COMMENT "Compiling and validating Slang ${stage} shader ${entry} to SPIR-V"
        )
    endif()
endfunction()

function(NoGraphicsAPI_add_example target)
    add_executable(${target} ${ARGN})
    set(embed_deps "")
    foreach(source IN LISTS ARGN)
        if(source MATCHES "\\.slang$")
            set_source_files_properties(${source} PROPERTIES HEADER_FILE_ONLY TRUE)
            source_group("Shaders" FILES ${source})
        elseif(source MATCHES "\\.(wgsl|rgba8)$")
            list(APPEND embed_deps "${source}")
        endif()
    endforeach()
    NoGraphicsAPI_disable_exceptions(${target})
    target_link_libraries(${target} PRIVATE NoGraphicsAPI_example_support)
    if(EMSCRIPTEN)
        string(REGEX REPLACE "^example_" "" target_short "${target}")
        set_target_properties(${target} PROPERTIES
            OUTPUT_NAME "${target_short}"
            SUFFIX ".html"
            LINK_DEPENDS "${embed_deps}"
        )
        target_link_options(${target} PRIVATE
            "--use-port=${EMDAWNWEBGPU_PORT}"
            "-sUSE_WEBGPU=0"
            "-sDISABLE_EXCEPTION_THROWING=0"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString"
            "--shell-file=${PROJECT_SOURCE_DIR}/examples/common/html_shell.html"
        )
    endif()
endfunction()
