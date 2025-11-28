idf_component_register(SRCS "main.cpp" "nematode/NMEAParser.cpp" "nematode/NMEACommand.cpp" "nematode/GPSService.cpp" "nematode/GPSFix.cpp" "nematode/NumberConversion.cpp"
                    PRIV_REQUIRES esp_wifi nvs_flash spi_flash esp_eth esp-tls esp_netif esp_http_server
                    INCLUDE_DIRS "." "include")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=reorder)
