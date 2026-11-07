set(A10_ROOT_CA "${COMPONENT_DIR}/certs/AmazonRootCA1.pem")
set(A10_DEVICE_CERT "${COMPONENT_DIR}/certs/device.crt.pem")
set(A10_PRIVATE_KEY "${COMPONENT_DIR}/certs/private.key")

if(NOT EXISTS "${A10_ROOT_CA}")
    message(FATAL_ERROR "A10: missing certs/AmazonRootCA1.pem from the course package.")
endif()

set(A10_EMBED_TXTFILES "certs/AmazonRootCA1.pem")
set(A10_CREDENTIALS_PROVISIONED 0)

if(EXISTS "${A10_DEVICE_CERT}" AND EXISTS "${A10_PRIVATE_KEY}")
    list(APPEND A10_EMBED_TXTFILES
        "certs/device.crt.pem"
        "certs/private.key"
    )
    set(A10_CREDENTIALS_PROVISIONED 1)
    message(STATUS "A10: AWS IoT device certificate and private key found; cloud path enabled at runtime when endpoint is configured.")
elseif(EXISTS "${A10_DEVICE_CERT}" OR EXISTS "${A10_PRIVATE_KEY}")
    message(FATAL_ERROR
        "A10: incomplete AWS IoT credentials. Provide both certs/device.crt.pem and certs/private.key, or neither of them.")
else()
    message(STATUS
        "A10: AWS IoT device credentials not provisioned. Build is allowed for local/HTTP validation; MQTT/AWS will remain disabled at runtime.")
endif()

idf_component_register(
    SRCS "aws_iot_credentials.c"
    INCLUDE_DIRS "include"
    EMBED_TXTFILES ${A10_EMBED_TXTFILES}
)

target_compile_definitions(${COMPONENT_LIB}
    PRIVATE A10_CREDENTIALS_PROVISIONED=${A10_CREDENTIALS_PROVISIONED}
)
