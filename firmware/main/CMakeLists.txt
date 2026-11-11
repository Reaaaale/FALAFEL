idf_component_register(SRCS "main.c" "relay_network.c" "relay_tcp.c" "lab_messaging.c"
    INCLUDE_DIRS "." REQUIRES falafel_storage esp_wifi esp_netif esp_event nvs_flash esp_timer lwip)

