idf_component_register(
    SRCS
        vault.cpp
    INCLUDE_DIRS "include"
    REQUIRES
        nvs_flash
        fatfs
        esp_partition
        esp_timer
        esp_common
)
