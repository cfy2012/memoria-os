idf_component_register(
    SRCS
        netfile.cpp
    INCLUDE_DIRS "include"
    REQUIRES
        esp_http_server
        esp_wifi
        esp_netif
        memoria_drivers
)
