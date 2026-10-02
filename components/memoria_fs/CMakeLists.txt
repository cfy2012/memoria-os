idf_component_register(
    SRCS
        private_fs.cpp
        fat_scanner.cpp
    INCLUDE_DIRS "include"
    REQUIRES
        esp_partition
        fatfs
        freertos
        esp_driver_sdmmc
        heap
        memoria_drivers
)
