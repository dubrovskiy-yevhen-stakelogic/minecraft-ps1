cmake_minimum_required(VERSION 3.21)

project(
    ps1-minecraft
    LANGUAGES C ASM
    VERSION 0.1.0
    DESCRIPTION "Minecraft-like experiment for PlayStation 1"
)

psn00bsdk_add_executable(mcpsx GPREL main.c)

psn00bsdk_add_cd_image(
    iso
    minecraft
    iso.xml
    DEPENDS mcpsx system.cnf
)