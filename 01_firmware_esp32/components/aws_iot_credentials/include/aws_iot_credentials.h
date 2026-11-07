#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool aws_iot_credentials_are_provisioned(void);
const char *aws_iot_root_ca_pem(void);
const char *aws_iot_device_certificate_pem(void);
const char *aws_iot_private_key_pem(void);

#ifdef __cplusplus
}
#endif
