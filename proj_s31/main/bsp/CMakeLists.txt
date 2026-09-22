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

set(BSP_INCLUDE_DIRS
    "bsp"
    "bsp/bspwifi"
    "bsp/gptimer"
    "bsp/twdt"
)
