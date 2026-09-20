# Included by ../CMakeLists.txt. Only sources under common/.
# Paths are relative to the main component directory.
# Do not call idf_component_register here.

set(COMMON_SRCS
    "common/filemgr/app_filemgr.c"
    "common/filemgr/cmd_filemgr.c"
    "common/cmd_system/cmdmgr.c"
    "common/cmd_system/cmd_system.c"
    "common/cmd_system/cmd_system_common.c"
    "common/cmd_nvs/cmd_nvs.c"
)

if(CONFIG_SOC_DEEP_SLEEP_SUPPORTED OR CONFIG_SOC_LIGHT_SLEEP_SUPPORTED)
    list(APPEND COMMON_SRCS "common/cmd_system/cmd_system_sleep.c")
endif()

if(CONFIG_FTP_SERVER_SUPPORT)
    list(APPEND COMMON_SRCS "common/ftp_server/app_ftpsrv.c")
    list(APPEND COMMON_SRCS "common/ftp_server/cmd_ftpsrv.c")
endif()

set(COMMON_INCLUDE_DIRS
    "common"
    "common/filemgr"
    "common/cmd_system"
    "common/cmd_nvs"
    "common/ftp_server"
)
