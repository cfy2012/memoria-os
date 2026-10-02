idf_component_register(
    SRCS
        shell.cpp
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        esp_common
        log
        esp_system
        heap
        memoria_kernel
        memoria_fs
        memoria_drivers
        memoria_script
        memoria_package
)
