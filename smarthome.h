#ifndef smart_home_H
#define smart_home_H
#include <stdint.h>

uint8_t crc8_array[] = {
        0,   29,  58,  39,  116, 105, 78,  83,  232, 245, 210, 207, 156, 129, 166, 187, 205, 208, 247, 234, 185, 164,
        131, 158, 37,  56,  31,  2,   81,  76,  107, 118, 135, 154, 189, 160, 243, 238, 201, 212, 111, 114, 85,  72,
        27,  6,   33,  60,  74,  87,  112, 109, 62,  35,  4,   25,  162, 191, 152, 133, 214, 203, 236, 241, 19,  14,
        41,  52,  103, 122, 93,  64,  251, 230, 193, 220, 143, 146, 181, 168, 222, 195, 228, 249, 170, 183, 144, 141,
        54,  43,  12,  17,  66,  95,  120, 101, 148, 137, 174, 179, 224, 253, 218, 199, 124, 97,  70,  91,  8,   21,
        50,  47,  89,  68,  99,  126, 45,  48,  23,  10,  177, 172, 139, 150, 197, 216, 255, 226, 38,  59,  28,  1,
        82,  79,  104, 117, 206, 211, 244, 233, 186, 167, 128, 157, 235, 246, 209, 204, 159, 130, 165, 184, 3,   30,
        57,  36,  119, 106, 77,  80,  161, 188, 155, 134, 213, 200, 239, 242, 73,  84,  115, 110, 61,  32,  7,   26,
        108, 113, 86,  75,  24,  5,   34,  63,  132, 153, 190, 163, 240, 237, 202, 215, 53,  40,  15,  18,  65,  92,
        123, 102, 221, 192, 231, 250, 169, 180, 147, 142, 248, 229, 194, 223, 140, 145, 182, 171, 16,  13,  42,  55,
        100, 121, 94,  67,  178, 175, 136, 149, 198, 219, 252, 225, 90,  71,  96,  125, 46,  51,  20,  9,   127, 98,
        69,  88,  11,  22,  49,  44,  151, 138, 173, 176, 227, 254, 217, 196};

typedef struct
{
    uint32_t src;
    uint32_t dst;
    uint32_t serial;
    uint8_t dev_type;
    uint8_t cmd;
} payload_type;

typedef struct
{
    payload_type payload;
    uint8_t length;
    uint8_t crc8;
} packet_type;

typedef struct
{
    uint8_t *str;
    size_t size;
} Response;

typedef struct
{
    uint8_t op;
    uint32_t value;
    uint8_t *name;
} triggers_type;

typedef struct type_net_sensor
{
    uint32_t address;
    uint8_t dev_type;
    uint8_t *dev_name;
    uint8_t sensors;
    uint8_t length_array;
    triggers_type *triggers;
    struct type_net_sensor *next;
} type_net_sensor;

typedef struct type_net_switch
{
    uint32_t address;
    uint8_t dev_type;
    uint8_t *dev_name;
    uint32_t length_array;
    uint8_t **names;
    struct type_net_switch *next;
} type_net_switch;

typedef struct type_net_other
{
    uint32_t address;
    uint8_t dev_type;
    uint8_t *dev_name;
    struct type_net_other *next;
} type_net_other;

typedef struct type_queue_request{
    uint32_t address;
    uint8_t dev_type;
    uint64_t timestamp;
    struct type_queue_request *next;

} type_queue_request;

#endif