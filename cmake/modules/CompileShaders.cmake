# Locates fxc.exe (ships with the Windows SDK) and provides
# `compile_shaders_from_manifest` which:
#   - emits two add_custom_command rules per shader (VSMain + PSMain)
#   - produces .cso bytecode under ${CMAKE_BINARY_DIR}/shaders/
#   - generates ${CMAKE_BINARY_DIR}/generated/shaders.rc embedding every
#     .cso as RCDATA (resource names <NAME>_VS and <NAME>_PS).
#
# Manifest format: a flat list of triplets {name vs_profile ps_profile},
# e.g.:
#   set(SHADER_MANIFEST
#       wave       vs_4_0 ps_4_0
#       shroud_fog vs_5_0 ps_5_0
#       ...
#   )

# Locate fxc.exe — bundled with every Windows SDK install.
find_program(FXC_EXECUTABLE
    NAMES fxc.exe
    PATHS
        "$ENV{WindowsSdkVerBinPath}/x64"
        "$ENV{WindowsSdkDir}bin/$ENV{WindowsSDKVersion}x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22000.0/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/x64"
    DOC "DirectX shader compiler"
)

if(NOT FXC_EXECUTABLE)
    message(FATAL_ERROR
        "fxc.exe not found. Install the Windows 10/11 SDK or add fxc to PATH.")
endif()
message(STATUS "Found fxc: ${FXC_EXECUTABLE}")


# Build per-config fxc flags. Debug = debug info + no optimization;
# Release = full optimization.
set(FXC_FLAGS_COMMON /nologo /WX)
set(FXC_FLAGS_DEBUG  /Zi /Od)
set(FXC_FLAGS_RELEASE /O3)


# Emit add_custom_command rules for one shader stage.
#
#   _compile_shader_stage(<name> <entry> <profile> <stage_suffix> OUT_VAR)
#
# Appends the produced .cso path to OUT_VAR (in parent scope).
function(_compile_shader_stage SHADER_NAME ENTRY PROFILE STAGE_SUFFIX OUT_VAR)
    set(HLSL_SOURCE "${PROJECT_SOURCE_DIR}/new/gfx/shaders/${SHADER_NAME}.hlsl")
    set(CSO_OUTPUT  "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}_${STAGE_SUFFIX}.cso")

    add_custom_command(
        OUTPUT  "${CSO_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND "${FXC_EXECUTABLE}"
                ${FXC_FLAGS_COMMON}
                "$<$<CONFIG:Debug>:${FXC_FLAGS_DEBUG}>"
                "$<$<NOT:$<CONFIG:Debug>>:${FXC_FLAGS_RELEASE}>"
                /T ${PROFILE}
                /E ${ENTRY}
                /Fo "${CSO_OUTPUT}"
                "${HLSL_SOURCE}"
        DEPENDS "${HLSL_SOURCE}"
        COMMAND_EXPAND_LISTS
        VERBATIM
        COMMENT "fxc ${SHADER_NAME}.hlsl [${ENTRY} / ${PROFILE}]"
    )

    list(APPEND ${OUT_VAR} "${CSO_OUTPUT}")
    set(${OUT_VAR} "${${OUT_VAR}}" PARENT_SCOPE)
endfunction()


# Compile every shader in MANIFEST and generate the shaders.rc file.
#
#   compile_shaders_from_manifest(<manifest_var> <out_cso_list_var> <out_rc_var> <out_hlsl_list_var>)
#
# Inputs:
#   manifest_var       — name of the variable holding the flat triplet list
#                        (name vs_profile ps_profile name vs_profile ps_profile ...)
# Outputs:
#   out_cso_list_var   — list of every produced .cso (for OBJECT_DEPENDS on the
#                        generated .rc, and to add to target sources so CMake
#                        knows about them)
#   out_rc_var         — full path to the generated shaders.rc
#   out_hlsl_list_var  — list of .hlsl source files (added to the target as
#                        HEADER_FILE_ONLY so they show up in the IDE but
#                        MSBuild doesn't try to compile them with the C++
#                        compiler)
function(compile_shaders_from_manifest MANIFEST_VAR OUT_CSO_LIST_VAR OUT_RC_VAR OUT_HLSL_LIST_VAR)
    set(MANIFEST ${${MANIFEST_VAR}})

    list(LENGTH MANIFEST MANIFEST_LEN)
    math(EXPR REMAINDER "${MANIFEST_LEN} % 3")
    if(NOT REMAINDER EQUAL 0)
        message(FATAL_ERROR
            "Shader manifest must be a flat list of triplets (name vs_profile ps_profile). "
            "Got ${MANIFEST_LEN} entries — not divisible by 3.")
    endif()

    set(CSO_FILES "")
    set(HLSL_FILES "")
    set(RC_LINES "")

    math(EXPR LAST_INDEX "${MANIFEST_LEN} - 1")
    foreach(i RANGE 0 ${LAST_INDEX} 3)
        math(EXPR i_vs "${i} + 1")
        math(EXPR i_ps "${i} + 2")
        list(GET MANIFEST ${i}    SHADER_NAME)
        list(GET MANIFEST ${i_vs} VS_PROFILE)
        list(GET MANIFEST ${i_ps} PS_PROFILE)

        _compile_shader_stage("${SHADER_NAME}" VSMain "${VS_PROFILE}" vs CSO_FILES)
        _compile_shader_stage("${SHADER_NAME}" PSMain "${PS_PROFILE}" ps CSO_FILES)

        list(APPEND HLSL_FILES
            "${PROJECT_SOURCE_DIR}/new/gfx/shaders/${SHADER_NAME}.hlsl")

        # RC resource names: uppercase with _VS / _PS suffix. Resource paths
        # are relative to the .rc file's directory; with the .rc generated
        # into ${CMAKE_BINARY_DIR}/generated, the .cso lives one level up.
        string(TOUPPER "${SHADER_NAME}" SHADER_NAME_UPPER)
        list(APPEND RC_LINES
            "${SHADER_NAME_UPPER}_VS  RCDATA  \"../shaders/${SHADER_NAME}_vs.cso\"")
        list(APPEND RC_LINES
            "${SHADER_NAME_UPPER}_PS  RCDATA  \"../shaders/${SHADER_NAME}_ps.cso\"")
    endforeach()

    # Generate shaders.rc. configure_file with a list joined by \n produces
    # the .rc body. RC needs ASCII; we use forward slashes in the path
    # (RC tolerates them).
    set(SHADERS_RC "${CMAKE_BINARY_DIR}/generated/shaders.rc")
    list(JOIN RC_LINES "\n" RC_BODY)
    set(RC_HEADER
        "// Generated by CompileShaders.cmake — DO NOT EDIT BY HAND.\n"
        "// Pre-compiled shader bytecode embedded as RCDATA.\n"
        "//\n"
        "// Resource names follow `<NAME>_VS` / `<NAME>_PS`. Loaded at runtime\n"
        "// via FindResource / LoadResource and passed straight to\n"
        "// CreateVertexShader / CreatePixelShader by Effect::Initialize.\n"
        "\n")
    list(JOIN RC_HEADER "" RC_HEADER_STR)
    file(GENERATE OUTPUT "${SHADERS_RC}" CONTENT "${RC_HEADER_STR}${RC_BODY}\n")

    # Make the .rc depend on every .cso so the RC step re-runs when any
    # shader bytecode changes. CMake doesn't track resource-file content
    # dependencies automatically; we set OBJECT_DEPENDS here.
    set_source_files_properties("${SHADERS_RC}" PROPERTIES
        OBJECT_DEPENDS "${CSO_FILES}")

    # Mark .hlsl files as HEADER_FILE_ONLY so MSBuild lists them in the IDE
    # but doesn't pass them to the C++ compiler. They're built into .cso via
    # the add_custom_command rules above.
    set_source_files_properties(${HLSL_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

    set(${OUT_CSO_LIST_VAR}  "${CSO_FILES}"  PARENT_SCOPE)
    set(${OUT_RC_VAR}        "${SHADERS_RC}" PARENT_SCOPE)
    set(${OUT_HLSL_LIST_VAR} "${HLSL_FILES}" PARENT_SCOPE)
endfunction()
