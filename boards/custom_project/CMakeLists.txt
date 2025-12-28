px4_add_board(
    PLATFORM nuttx
    VENDOR custom_project
    MODEL orange_lean
    LABEL default
    TOOLCHAIN arm-none-eabi
    ARCHITECTURE cortex-m7
    ROMFSROOT px4fmu_common
    IO px4_io-v2
    CONSTRAIN_FLASH 2031616
    TESTING
)