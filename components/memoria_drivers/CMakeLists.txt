# Helix AAC 解码器（earlephilhower/ESP8266Audio 移植，源自 RealNetworks 2005 fixpt，RPSL）
# 第三方固定点实现，不参与项目告警门槛
set(HELIX_SRCS
    third_party/helix_aac/src/libhelix-aac/aacdec.c
    third_party/helix_aac/src/libhelix-aac/aactabs.c
    third_party/helix_aac/src/libhelix-aac/bitstream.c
    third_party/helix_aac/src/libhelix-aac/buffers.c
    third_party/helix_aac/src/libhelix-aac/dct4.c
    third_party/helix_aac/src/libhelix-aac/decelmnt.c
    third_party/helix_aac/src/libhelix-aac/dequant.c
    third_party/helix_aac/src/libhelix-aac/fft.c
    third_party/helix_aac/src/libhelix-aac/filefmt.c
    third_party/helix_aac/src/libhelix-aac/huffmanaac.c
    third_party/helix_aac/src/libhelix-aac/hufftabs.c
    third_party/helix_aac/src/libhelix-aac/imdct.c
    third_party/helix_aac/src/libhelix-aac/noiseless.c
    third_party/helix_aac/src/libhelix-aac/pns.c
    third_party/helix_aac/src/libhelix-aac/sbr.c
    third_party/helix_aac/src/libhelix-aac/sbrfft.c
    third_party/helix_aac/src/libhelix-aac/sbrfreq.c
    third_party/helix_aac/src/libhelix-aac/sbrhfadj.c
    third_party/helix_aac/src/libhelix-aac/sbrhfgen.c
    third_party/helix_aac/src/libhelix-aac/sbrhuff.c
    third_party/helix_aac/src/libhelix-aac/sbrimdct.c
    third_party/helix_aac/src/libhelix-aac/sbrmath.c
    third_party/helix_aac/src/libhelix-aac/sbrqmf.c
    third_party/helix_aac/src/libhelix-aac/sbrside.c
    third_party/helix_aac/src/libhelix-aac/sbrtabs.c
    third_party/helix_aac/src/libhelix-aac/stproc.c
    third_party/helix_aac/src/libhelix-aac/tns.c
    third_party/helix_aac/src/libhelix-aac/trigtabs.c
)

idf_component_register(
    SRCS
        spi_bus.cpp
        ili9341.cpp
        sdcard.cpp
        power.cpp
        i2s_audio.cpp
        mp4_demux.cpp
        joystick.cpp
        battery_adc.cpp
        rtc_clock.cpp
        wifi_manager.cpp
        ble_manager.cpp
        ${HELIX_SRCS}
    INCLUDE_DIRS "include" "third_party/minimp3" "third_party/helix_aac/src/libhelix-aac"
    REQUIRES
        driver
        esp_adc
        esp_driver_spi
        esp_driver_gpio
        esp_driver_i2s
        esp_driver_ledc
        esp_driver_sdmmc
        esp_driver_sdspi
        esp_wifi
        esp_event
        esp_netif
        bt
        esp_partition
        nvs_flash
        fatfs
        vfs
        freertos
        heap
        esp_timer
)

# 第三方旧代码不参与 -Werror（历史实现，非本项目代码）
set_source_files_properties(${HELIX_SRCS} PROPERTIES COMPILE_OPTIONS "-Wno-error")