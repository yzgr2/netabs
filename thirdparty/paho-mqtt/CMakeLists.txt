cmake_minimum_required(VERSION 3.5)

project(paho_mqtt)

file(GLOB src_files
    MQTTPacket/*.c
)

add_library(paho_mqtt STATIC ${src_files})

target_compile_options(paho_mqtt PRIVATE -Wall -Werror -g)
