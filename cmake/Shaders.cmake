# Build-time HLSL compilation with fxc. Every shader variant becomes a header
# (g_<name> byte array) that is embedded into the executable.

find_program(BGN_FXC fxc
    HINTS
        "$ENV{WindowsSdkVerBinPath}/x64"
        "$ENV{WindowsSdkBinPath}/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/$ENV{WindowsSDKVersion}/x64"
    DOC "fxc.exe from the Windows SDK")
if(NOT BGN_FXC)
    file(GLOB _fxc_candidates "C:/Program Files (x86)/Windows Kits/10/bin/10.*/x64/fxc.exe")
    list(SORT _fxc_candidates)
    list(REVERSE _fxc_candidates)
    list(GET _fxc_candidates 0 BGN_FXC)
endif()
if(NOT BGN_FXC)
    message(FATAL_ERROR "fxc.exe not found (install the Windows 10/11 SDK)")
endif()
message(STATUS "fxc: ${BGN_FXC}")

set(BGN_SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(BGN_SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/generated/shaders")
file(MAKE_DIRECTORY "${BGN_SHADER_OUT_DIR}")
file(GLOB BGN_SHADER_COMMON_DEPS "${BGN_SHADER_SRC_DIR}/*.hlsli" "${BGN_SHADER_SRC_DIR}/*.h" "${BGN_SHADER_SRC_DIR}/generated/*.hlsli")

set(BGN_SHADER_OUTPUTS "")
set(BGN_SHADER_TABLE "")
set(BGN_SHADER_INCLUDES "")

# bgn_shader(<name> <file> <profile> <entry> <kind CS|VS|PS> [defines...])
macro(bgn_shader NAME FILE PROFILE ENTRY KIND)
    set(_out "${BGN_SHADER_OUT_DIR}/${NAME}.h")
    set(_defs "")
    foreach(_d ${ARGN})
        list(APPEND _defs "/D" "${_d}")
    endforeach()
    add_custom_command(
        OUTPUT "${_out}"
        COMMAND "${BGN_FXC}" /nologo /O3 /T ${PROFILE} /E ${ENTRY} /Vn g_${NAME} /Fh "${_out}" ${_defs} /I "${BGN_SHADER_SRC_DIR}" "${BGN_SHADER_SRC_DIR}/${FILE}"
        DEPENDS "${BGN_SHADER_SRC_DIR}/${FILE}" ${BGN_SHADER_COMMON_DEPS}
        COMMENT "HLSL ${NAME} (${FILE})"
        VERBATIM)
    list(APPEND BGN_SHADER_OUTPUTS "${_out}")
    string(APPEND BGN_SHADER_TABLE "BGN_SHADER(${NAME}, ${KIND})\n")
    string(APPEND BGN_SHADER_INCLUDES "#include \"${NAME}.h\"\n")
endmacro()

# ---- Pipeline ----------------------------------------------------------------
bgn_shader(ingest_cs ingest.hlsl cs_5_0 main CS)
bgn_shader(luma_down_color_cs luma_down.hlsl cs_5_0 main CS LUMA_FROM_COLOR=1)
bgn_shader(luma_down_cs luma_down.hlsl cs_5_0 main CS)
bgn_shader(flow_cs flow.hlsl cs_5_0 main CS)
bgn_shader(flow_smooth_cs flow_smooth.hlsl cs_5_0 main CS)
bgn_shader(cleanup_cs cleanup.hlsl cs_5_0 main CS)
bgn_shader(quality_cs quality.hlsl cs_5_0 main CS)
bgn_shader(temporal_cs temporal.hlsl cs_5_0 main CS)
bgn_shader(deblur_cs deblur.hlsl cs_5_0 main CS)
bgn_shader(resample_up_cs resample.hlsl cs_5_0 main CS RESAMPLE_UP=1)
bgn_shader(resample_down_cs resample.hlsl cs_5_0 main CS RESAMPLE_DOWN=1)
bgn_shader(resample_bilinear_cs resample.hlsl cs_5_0 main CS RESAMPLE_BILINEAR=1)
bgn_shader(finish_cs finish.hlsl cs_5_0 main CS)
bgn_shader(interp_cs interp.hlsl cs_5_0 main CS)
bgn_shader(stats_cs stats.hlsl cs_5_0 main CS)
bgn_shader(content_res_cs content_res.hlsl cs_5_0 main CS)
bgn_shader(synthetic_cs synthetic.hlsl cs_5_0 main CS)
bgn_shader(present_vs present.hlsl vs_5_0 vsMain VS)
bgn_shader(present_ps present.hlsl ps_5_0 psMain PS)

# ---- Neural Super Resolution (S: 8 ch / 2 hidden, L: 16 ch / 3 hidden) ----------
foreach(_half "" "_h")
    if(_half STREQUAL "_h")
        set(_hd "NSR_HALF=1")
    else()
        set(_hd "NSR_FP32=1")
    endif()
    bgn_shader(nsr_s_first${_half}_cs nsr.hlsl cs_5_0 main CS NSR_STAGE_FIRST=1 NSR_LAYER=0 ${_hd})
    bgn_shader(nsr_s_l1${_half}_cs nsr.hlsl cs_5_0 main CS NSR_STAGE_HIDDEN=1 NSR_LAYER=1 ${_hd})
    bgn_shader(nsr_s_l2${_half}_cs nsr.hlsl cs_5_0 main CS NSR_STAGE_HIDDEN=1 NSR_LAYER=2 ${_hd})
    bgn_shader(nsr_s_last${_half}_cs nsr.hlsl cs_5_0 main CS NSR_STAGE_LAST=1 NSR_LAYER=3 ${_hd})
    bgn_shader(nsr_l_first${_half}_cs nsr.hlsl cs_5_0 main CS NSR_MODEL_L=1 NSR_STAGE_FIRST=1 NSR_LAYER=0 ${_hd})
    bgn_shader(nsr_l_l1${_half}_cs nsr.hlsl cs_5_0 main CS NSR_MODEL_L=1 NSR_STAGE_HIDDEN=1 NSR_LAYER=1 ${_hd})
    bgn_shader(nsr_l_l2${_half}_cs nsr.hlsl cs_5_0 main CS NSR_MODEL_L=1 NSR_STAGE_HIDDEN=1 NSR_LAYER=2 ${_hd})
    bgn_shader(nsr_l_l3${_half}_cs nsr.hlsl cs_5_0 main CS NSR_MODEL_L=1 NSR_STAGE_HIDDEN=1 NSR_LAYER=3 ${_hd})
    bgn_shader(nsr_l_last${_half}_cs nsr.hlsl cs_5_0 main CS NSR_MODEL_L=1 NSR_STAGE_LAST=1 NSR_LAYER=4 ${_hd})
endforeach()

file(WRITE "${BGN_SHADER_OUT_DIR}/bgn_shader_table.inc.tmp" "${BGN_SHADER_TABLE}")
file(WRITE "${BGN_SHADER_OUT_DIR}/bgn_shader_includes.inc.tmp" "${BGN_SHADER_INCLUDES}")
configure_file("${BGN_SHADER_OUT_DIR}/bgn_shader_table.inc.tmp" "${BGN_SHADER_OUT_DIR}/bgn_shader_table.inc" COPYONLY)
configure_file("${BGN_SHADER_OUT_DIR}/bgn_shader_includes.inc.tmp" "${BGN_SHADER_OUT_DIR}/bgn_shader_includes.inc" COPYONLY)
add_custom_target(bgn_shaders DEPENDS ${BGN_SHADER_OUTPUTS})
