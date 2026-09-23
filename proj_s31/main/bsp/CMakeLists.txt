# Included by ../CMakeLists.txt. Only sources under bsp/.
# Paths are relative to the main component directory.
# Do not call idf_component_register here.

set(BSP_SRCS
    "bsp/bspwifi/bsp_wifimgr.c"
    "bsp/bspwifi/bsp_wifiap.c"
    "bsp/bspwifi/bsp_wifista.c"
    "bsp/bspwifi/bsp_wifiapsta.c"
    "bsp/bspwifi/bsp_wificmd.c"
    "bsp/gptimer/bsp_timer.c"
    "bsp/twdt/bsp_twdt.c"
)

if(CONFIG_BSP_AUDIO_ENABLE)
    list(APPEND BSP_SRCS "bsp/bspaudio/bsp_audiomgr.c")
    list(APPEND BSP_SRCS "bsp/bspaudio/bsp_audio_cmd.c")
endif()

set(BSP_INCLUDE_DIRS
    "bsp"
    "bsp/bspwifi"
    "bsp/gptimer"
    "bsp/twdt"
)
if(CONFIG_BSP_AUDIO_ENABLE)
    list(APPEND BSP_INCLUDE_DIRS "bsp/bspaudio")
endif()

