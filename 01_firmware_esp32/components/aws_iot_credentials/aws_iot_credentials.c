#include "aws_iot_credentials.h"
#include <stddef.h>

#ifndef A10_CREDENTIALS_PROVISIONED
#define A10_CREDENTIALS_PROVISIONED 0
#endif

extern const unsigned char amazon_root_ca_1_pem_start[] asm("_binary_AmazonRootCA1_pem_start");

#if A10_CREDENTIALS_PROVISIONED
extern const unsigned char device_certificate_pem_start[] asm("_binary_device_crt_pem_start");
extern const unsigned char private_key_pem_start[] asm("_binary_private_key_start");
#endif

bool aws_iot_credentials_are_provisioned(void)
{
    return A10_CREDENTIALS_PROVISIONED != 0;
}

const char *aws_iot_root_ca_pem(void)
{
    return (const char *)amazon_root_ca_1_pem_start;
}

const char *aws_iot_device_certificate_pem(void)
{
#if A10_CREDENTIALS_PROVISIONED
    return (const char *)device_certificate_pem_start;
#else
    return NULL;
#endif
}

const char *aws_iot_private_key_pem(void)
{
#if A10_CREDENTIALS_PROVISIONED
    return (const char *)private_key_pem_start;
#else
    return NULL;
#endif
}
