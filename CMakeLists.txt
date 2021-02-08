cmake_minimum_required(VERSION 3.5)

project(netabs)

set( CMAKE_VERBOSE_MAKEFILE on )
set(CMAKE_C_STANDARD 99)

add_subdirectory(thirdparty/mbedtls)

add_subdirectory(thirdparty/paho-mqtt)

file(GLOB src_files
    src/*.c
)

add_library(netabs STATIC ${src_files})

target_include_directories(netabs PRIVATE
        src/include
        src/
        thirdparty/uthash/include
        thirdparty/mbedtls/include
        thirdparty/paho-mqtt/MQTTPacket
)

#-Wno-error=missing-braces -fvisibility=hidden
target_compile_options(netabs PRIVATE -Wall -Werror -g)



## compile test exe #################################3
set(test_SRC test/test1.c )
add_executable(test1 ${test_SRC})

target_include_directories(test1 PRIVATE
        src/include
        src/
        thirdparty/uthash/include
        thirdparty/mbedtls/include
)
target_link_libraries(test1 PUBLIC netabs  mbedtls mbedcrypto mbedx509)


##################################################
set(test_mqtt_SRC test/test_mqtt.c )
add_executable(test_mqtt ${test_mqtt_SRC})

target_include_directories(test_mqtt PRIVATE
        src/include
        src/
        thirdparty/uthash/include
        thirdparty/mbedtls/include
)
target_compile_options(test_mqtt PRIVATE -g)
target_link_libraries(test_mqtt PUBLIC netabs paho_mqtt mbedtls mbedcrypto mbedx509)
