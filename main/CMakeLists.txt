idf_component_register(
    SRCS main.cpp
    INCLUDE_DIRS "."
    REQUIRES
        memoria_kernel
        memoria_fs
        memoria_window
        memoria_drivers
        memoria_script
        memoria_shell
        memoria_package
        fatfs
        esp_driver_spi
        esp_driver_sdmmc
        esp_partition
        nvs_flash
        esp_wifi
        bt
)