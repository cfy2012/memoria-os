idf_component_register(
    SRCS
        window.cpp
        gb2312_font.cpp
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        memoria_drivers
        memoria_kernel
        esp_common
        log
        heap
)