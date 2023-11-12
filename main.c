#include <stdio.h>
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include "smarthome.h"

enum
{
    WHOISHERE = 0x1,
    IAMHERE = 0x2,
    STATUS = 0x4,
    TICK = 0x6
};

type_net_other *net_other_ptr;
type_net_sensor *net_sensor_ptr;
type_net_switch *net_switch_ptr;
type_queue_request *queue_request;
unsigned serial = 1;
uint64_t time_global;
uint64_t time_last_request;
uint8_t *str_send;
size_t count_send;
uint32_t hub_address;

uint8_t
compute_crc8(const uint8_t *data, size_t size)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < size; i++) {
        crc = crc8_array[data[i] ^ crc];
    }
    return crc;
}

uint64_t
read_uleb128(const uint8_t *in, size_t *count)
{
    uint64_t tmp, result = in[(*count)++];
    int i = 14;

    if (result > 0x7f) {
        tmp = in[(*count)++];
        result = (result & 0x7f) | ((tmp & 0x7f) << 7);
        while (tmp > 0x7f) {
            tmp = in[(*count)++];
            if (i == 63 && tmp != 1) {
                fprintf(stderr, "CAN NOT DECODE ULEB128: NOT A UINT64_T");
                exit(1);
            }
            result |= (tmp & 0x7f) << i;
            i += 7;
        };
    }
    return result;
}

void
write_uleb128(uint8_t *out, size_t *count, uint64_t value)
{
    uint8_t byte;
    do {
        byte = value & 0x7f;
        value >>= 7;
        if (value) {
            byte |= 0x80;
        }
        out[(*count)++] = byte;
    } while (value);
}

void realloc_packet_str(uint32_t tmp) {
    if ((count_send / 1000 + tmp) > 900) {
        str_send = realloc(str_send, ((count_send + tmp) / 1000 + 2) * 1000 * sizeof(uint8_t));
    }
}

void who(void) {
    size_t start;
    type_net_sensor *tmp_sensor = net_sensor_ptr;
    type_net_other *tmp_other = net_other_ptr;
    type_net_switch *tmp_switch = net_switch_ptr;
    uint32_t tmp;

    while (tmp_sensor) {
        start = ++count_send;
        write_uleb128(str_send, &count_send, tmp_sensor->address);
        write_uleb128(str_send, &count_send, 0x3fff);
        write_uleb128(str_send, &count_send, serial++);
        str_send[count_send++] = tmp_sensor->dev_type;
        str_send[count_send++] = 0x2;

        tmp = tmp_sensor->dev_name[0];
        realloc_packet_str(tmp);
        memcpy(&str_send[count_send], tmp_sensor->dev_name, tmp);
        count_send += tmp + 1;

        str_send[count_send++] = tmp_sensor->sensors;
        str_send[count_send++] = tmp_sensor->length_array;
        for (int i = 0; i < tmp_sensor->length_array; i++) {
            str_send[count_send++] = tmp_sensor->triggers[i].op;
            write_uleb128(str_send, &count_send, tmp_sensor->triggers[i].value);
            tmp = tmp_sensor->triggers[i].name[0];
            realloc_packet_str(tmp);
            memcpy(&str_send[count_send], tmp_sensor->triggers[i].name, tmp + 1);
            count_send += tmp + 1;
        }
        str_send[start - 1] = count_send - start;
        str_send[count_send++] = compute_crc8(&str_send[start], str_send[start - 1]);
        tmp_sensor = tmp_sensor->next;
    }

    while (tmp_switch) {
        start = ++count_send;
        write_uleb128(str_send, &count_send, tmp_switch->address);
        write_uleb128(str_send, &count_send, 0x3fff);
        write_uleb128(str_send, &count_send, serial++);
        str_send[count_send++] = tmp_switch->dev_type;
        str_send[count_send++] = 0x2;

        tmp = tmp_switch->dev_name[0];
        realloc_packet_str(tmp);
        memcpy(&str_send[count_send], tmp_switch->dev_name, tmp + 1);
        count_send += tmp + 1;

        str_send[count_send++] = tmp_switch->length_array;
        for (int i = 0; i < tmp_switch->length_array; i++) {
            tmp = tmp_switch->names[i][0];
            realloc_packet_str(tmp);
            memcpy(&str_send[count_send], tmp_switch->names[i], tmp + 1);
            count_send += tmp + 1;
        }
        str_send[start - 1] = count_send - start;
        str_send[count_send++] = compute_crc8(&str_send[start], str_send[start - 1]);
        tmp_switch = tmp_switch->next;
    }

    while (tmp_other) {
        start = ++count_send;
        write_uleb128(str_send, &count_send, tmp_other->address);
        write_uleb128(str_send, &count_send, 0x3fff);
        write_uleb128(str_send, &count_send, serial++);
        str_send[count_send++] = tmp_other->dev_type;
        str_send[count_send++] = 0x2;

        tmp = tmp_other->dev_name[0];
        realloc_packet_str(tmp);
        memcpy(&str_send[count_send], tmp_other->dev_name, tmp + 1);
        count_send += tmp + 1;

        str_send[start - 1] = count_send - start;
        str_send[count_send++] = compute_crc8(&str_send[start], str_send[start - 1]);
        tmp_other = tmp_other->next;
    }
}

void sensor_free(type_net_sensor *tmp_sensor) {
    free(tmp_sensor->dev_name);
    for (int i = 0; i < tmp_sensor->length_array; i++) {
        free(tmp_sensor->triggers[i].name);
    }
}

void switch_free(type_net_switch *tmp_switch) {
    free(tmp_switch->dev_name);
    for (int i = 0; i < tmp_switch->length_array; i++) {
        free(tmp_switch->names[i]);
    }
    free(tmp_switch->names);
}

void other_free(type_net_other *tmp_other) {
    free(tmp_other->dev_name);
}

void i_am_here(payload_type payload, const uint8_t *in, size_t *count)
{
    type_net_sensor *tmp_sensor;
    type_net_other *tmp_other;
    type_net_switch *tmp_switch;
    int tmp_length, tmp;
    switch(payload.dev_type) {
        case 0x2:
            tmp_sensor = net_sensor_ptr;
            if (tmp_sensor) {
                while (tmp_sensor->address != payload.src && tmp_sensor->next) {
                    tmp_sensor = tmp_sensor->next;
                }
                if (tmp_sensor->address != payload.src) {
                    tmp_sensor->next = calloc(1, sizeof(type_net_sensor));
                    tmp_sensor = tmp_sensor->next;
                } else {
                    sensor_free(tmp_sensor);
                }
            } else {
                tmp_sensor = calloc(1, sizeof(type_net_sensor));
                net_sensor_ptr = tmp_sensor;
            }
            tmp_sensor->dev_type = payload.dev_type;
            tmp_sensor->address = payload.src;
            tmp_length = in[(*count)++];
            tmp_sensor->dev_name = calloc(tmp_length + 2, sizeof(uint8_t));
            tmp_sensor->dev_name[0] = tmp_length;
            memcpy(&(tmp_sensor->dev_name[1]), &in[*count], tmp_length);
            (*count) += tmp_length;
            tmp_sensor->dev_name[tmp_length + 1] = '\0';
            tmp_sensor->sensors = in[(*count)++];
            tmp_sensor->length_array = in[(*count)++];
            tmp_sensor->triggers = calloc(tmp_sensor->length_array, sizeof(triggers_type));
            for (int i = 0; i < tmp_sensor->length_array; i++) {
                tmp_sensor->triggers[i].op = in[(*count)++];
                tmp_sensor->triggers[i].value = read_uleb128(in, count);
                tmp = in[(*count)++];
                tmp_sensor->triggers[i].name = calloc(tmp + 2, sizeof(uint8_t));
                tmp_sensor->triggers[i].name[0] = tmp;
                memcpy(&(tmp_sensor->triggers[i].name[1]), &in[*count], tmp);
                (*count) += tmp;
                tmp_sensor->triggers[i].name[tmp + 1] = '\0';
            }
            break;
        case 0x3:
            tmp_switch = net_switch_ptr;
            if (tmp_switch) {
                while (tmp_switch->address != payload.src && tmp_switch->next) {
                    tmp_switch = tmp_switch->next;
                }
                if (tmp_switch->address != payload.src) {
                    tmp_switch->next = calloc(1, sizeof(type_net_switch));
                    tmp_switch = tmp_switch->next;
                } else {
                    switch_free(tmp_switch);
                }
            } else {
                tmp_switch = calloc(1, sizeof(type_net_switch));
                net_switch_ptr = tmp_switch;
            }
            tmp_switch->dev_type = payload.dev_type;
            tmp_switch->address = payload.src;
            tmp_length = in[(*count)++];
            tmp_switch->dev_name = calloc(tmp_length + 2, sizeof(uint8_t));
            tmp_switch->dev_name[0] = tmp_length;
            memcpy(&(tmp_switch->dev_name[1]), &in[*count], tmp_length);
            (*count) += tmp_length;
            tmp_switch->dev_name[tmp_length + 1] = '\0';
            tmp_switch->length_array = in[(*count)++];
            tmp_switch->names = calloc(tmp_switch->length_array, sizeof(uint8_t *));
            for (int i = 0; i < tmp_switch->length_array; i++) {
                tmp = in[(*count)++];
                tmp_switch->names[i] = calloc(tmp + 2, sizeof(uint8_t));
                tmp_switch->names[i][0] = tmp;
                memcpy(&(tmp_switch->names[i][1]), &in[*count], tmp);
                (*count) += tmp;
                tmp_switch->names[i][tmp + 1] = '\0';
            }
            break;
        case 0x4:
        case 0x5:
        case 0x6:
            tmp_other = net_other_ptr;
            if (tmp_other) {
                while (tmp_other->address != payload.src && tmp_other->next) {
                    tmp_other = tmp_other->next;
                }
                if (tmp_other->address != payload.src) {
                    tmp_other->next = calloc(1, sizeof(type_net_other));
                    tmp_other = tmp_other->next;
                } else {
                    other_free(tmp_other);
                }
            } else {
                tmp_other = calloc(1, sizeof(type_net_other));
                net_other_ptr = tmp_other;
            }
            tmp_other->dev_type = payload.dev_type;
            tmp_other->address = payload.src;
            tmp_length = in[(*count)++];
            tmp_other->dev_name = calloc(tmp_length + 2, sizeof(uint8_t));
            tmp_other->dev_name[0] = tmp_length;
            memcpy(&(tmp_other->dev_name[1]), &in[*count], tmp_length);
            (*count) += tmp_length;
            tmp_other->dev_name[tmp_length + 1] = '\0';
    }

    size_t start = count_send++;
    write_uleb128(str_send, &count_send, hub_address);
    write_uleb128(str_send, &count_send, payload.src);
    write_uleb128(str_send, &count_send, serial++);
    str_send[count_send++] = payload.dev_type;
    str_send[count_send++] = 0x3;
    str_send[start] = count_send - start - 1;
    str_send[count_send++] = compute_crc8(&str_send[start + 1], str_send[start]);

    if (payload.dev_type != 0x6) {
        type_queue_request *tmp_queue = queue_request;
        if (tmp_queue) {
            while (tmp_queue->next) {
                tmp_queue = tmp_queue->next;
            }
            tmp_queue->next = calloc(1, sizeof(type_queue_request));
            tmp_queue = tmp_queue->next;
        } else {
            tmp_queue = calloc(1, sizeof(type_queue_request));
            queue_request = tmp_queue;
        }

        tmp_queue->address = payload.src;
        tmp_queue->dev_type = payload.dev_type;
        tmp_queue->timestamp = time_global;
    }
}

type_net_switch *find_switch(uint32_t address) {
    type_net_switch *tmp = net_switch_ptr;
    while (tmp && tmp->address != address) {
        tmp = tmp->next;
    }
    return tmp;
}

type_net_sensor *find_sensor(uint32_t address) {
    type_net_sensor *tmp = net_sensor_ptr;
    while (tmp && tmp->address != address) {
        tmp = tmp->next;
    }
    return tmp;
}

type_net_other *find_other_device(const uint8_t *name) {
    type_net_other *tmp = net_other_ptr;
    while (tmp && (strcmp((char *) tmp->dev_name, (char *) name) != 0)) {
        tmp = tmp->next;
    }
    return tmp;
}

void send_device_status(type_net_other *tmp_other, uint8_t value) {
    if (!tmp_other) {
        return;
    }
    realloc_packet_str(0);
    size_t start = count_send++;
    write_uleb128(str_send, &count_send, hub_address);
    write_uleb128(str_send, &count_send, tmp_other->address);
    write_uleb128(str_send, &count_send, serial++);
    str_send[count_send++] = tmp_other->dev_type;
    str_send[count_send++] = 0x5;
    str_send[count_send++] = value;
    str_send[start] = count_send - start - 1;
    str_send[count_send++] = compute_crc8(&str_send[start + 1], str_send[start]);

    type_queue_request *tmp_queue = queue_request;
    if (tmp_queue) {
        while (tmp_queue->next) {
            tmp_queue = tmp_queue->next;
        }
        tmp_queue->next = calloc(1, sizeof(type_queue_request));
        tmp_queue = tmp_queue->next;
    } else {
        tmp_queue = calloc(1, sizeof(type_queue_request));
        queue_request = tmp_queue;
    }

    tmp_queue->address = tmp_other->address;
    tmp_queue->dev_type = tmp_other->dev_type;
    tmp_queue->timestamp = time_global;
}

bool check_in_net(uint32_t address, uint8_t dev_type) {
    type_net_sensor *tmp_sensor = net_sensor_ptr;
    type_net_switch *tmp_switch = net_switch_ptr;
    type_net_other *tmp_other = net_other_ptr;
    switch (dev_type) {
        case 0x2:
            while (tmp_sensor && tmp_sensor->address != address) {
                tmp_sensor = tmp_sensor->next;
            }
            if (!tmp_sensor) {
                return 0;
            }
            return 1;
        case 0x3:
            while (tmp_switch && tmp_switch->address != address) {
                tmp_switch = tmp_switch->next;
            }
            if (!tmp_switch) {
                return 0;
            }
            return 1;
        default:
            while (tmp_other && tmp_other->address != address) {
                tmp_other = tmp_other->next;
            }
            if (!tmp_other) {
                return 0;
            }
            return 1;
    }
}

void status(payload_type payload, const uint8_t *in, size_t *count) {
    unsigned tmp, shift = 1;
    unsigned buf[4];
    type_net_sensor *tmp_sensor;
    type_net_switch *tmp_switch;
    type_queue_request *tmp_queue = queue_request, *prev_tmp = NULL;

    if (!check_in_net(payload.src, payload.dev_type)) {
        return;
    }

    switch (payload.dev_type) {
        case 0x2:
            (*count)++;
            tmp_sensor = find_sensor(payload.src);
            for (int i = 0; i < 4; i++) {
                if (tmp_sensor->sensors & shift) {
                    buf[i] = read_uleb128(in, count);
                }
                shift <<= 1;
            }

            for (int i = 0; i < tmp_sensor->length_array; i++) {
                if ((tmp_sensor->sensors & (1 << i))) {
                    if (tmp_sensor->triggers[i].op & 0x2) {
                        if (buf[tmp_sensor->triggers[i].op >> 2] > tmp_sensor->triggers[i].value) {
                            send_device_status(find_other_device(tmp_sensor->triggers[i].name),
                                               tmp_sensor->triggers[i].op & 1);
                        }
                    } else if (buf[tmp_sensor->triggers[i].op >> 2] < tmp_sensor->triggers[i].value) {
                            send_device_status(find_other_device(tmp_sensor->triggers[i].name),
                                               tmp_sensor->triggers[i].op & 1);
                        }
                }
            }
            break;
        case 0x3:
            tmp = in[(*count)++];
            tmp_switch = find_switch(payload.src);
            for (int i = 0; i < tmp_switch->length_array; i++) {
                send_device_status(find_other_device(tmp_switch->names[i]), tmp);
            }
            break;
    }
    while (tmp_queue && tmp_queue->address != payload.src) {
        prev_tmp = tmp_queue;
        tmp_queue = tmp_queue->next;
    }
    if (tmp_queue) {
        if (prev_tmp) {
            prev_tmp->next = tmp_queue->next;
        } else {
            queue_request = tmp_queue->next;
        }
        free(tmp_queue);
    }
}

void del_dev(uint32_t address, uint8_t dev_type) {
    type_net_sensor *tmp_sensor = net_sensor_ptr, *prev_tmp_sensor = NULL;
    type_net_switch *tmp_switch = net_switch_ptr, *prev_tmp_switch = NULL;
    type_net_other *tmp_other = net_other_ptr, *prev_tmp_other = NULL;
    switch (dev_type) {
        case 0x2:
            while (tmp_sensor->address != address) {
                prev_tmp_sensor = tmp_sensor;
                tmp_sensor = tmp_sensor->next;
            }
            if (prev_tmp_sensor) {
                prev_tmp_sensor->next = tmp_sensor->next;
            } else {
                net_sensor_ptr = tmp_sensor->next;
            }
            sensor_free(tmp_sensor);
            free(tmp_sensor);
            return;
        case 0x3:
            while (tmp_switch->address != address) {
                prev_tmp_switch = tmp_switch;
                tmp_switch = tmp_switch->next;
            }
            if (prev_tmp_switch) {
                prev_tmp_switch->next = tmp_switch->next;
            } else {
                net_switch_ptr = tmp_switch->next;
            }
            switch_free(tmp_switch);
            free(tmp_switch);
            return;

        default:
            while (tmp_other->address != address) {
                prev_tmp_other = tmp_other;
                tmp_other = tmp_other->next;
            }
            if (prev_tmp_other) {
                prev_tmp_other->next = tmp_other->next;
            } else {
                net_other_ptr = tmp_other->next;
            }
            other_free(tmp_other);
            free(tmp_other);
            return;
    }
}

void tick(const uint8_t *in, size_t *count) {
    time_global = read_uleb128(in, count);
    if (!time_last_request) {
        time_last_request = time_global;
    }

    type_queue_request *tmp_queue = queue_request, *prev_tmp = NULL;
    while (tmp_queue) {
        if (time_global - tmp_queue->timestamp > 300) {
            del_dev(tmp_queue->address, tmp_queue->dev_type);
            if (prev_tmp) {
                prev_tmp->next = tmp_queue->next;
            } else {
                queue_request = tmp_queue->next;
            }
            free(tmp_queue);
        }
        prev_tmp = tmp_queue;
        tmp_queue = tmp_queue->next;
    }
}

void parse_packet(const uint8_t *in) {
    size_t count = 0, start;
    packet_type packet;
    packet.length = in[0];

    while (in[count] != '\0') {
        start = count;
        packet.length = in[count++];
        packet.payload.src = read_uleb128(in, &count);
        packet.payload.dst = read_uleb128(in, &count);
        packet.payload.serial = read_uleb128(in, &count);
        packet.payload.dev_type = in[count++];
        packet.payload.cmd = in[count++];
        packet.crc8 = in[start + packet.length + 1];
        if (packet.crc8 == compute_crc8(&in[start + 1], packet.length)) {
            switch (packet.payload.cmd) {
                case WHOISHERE:
                    who();
                case IAMHERE:
                    if (time_global - time_last_request <= 300) {
                        i_am_here(packet.payload, in, &count);
                    }
                    break;
                case STATUS:
                    status(packet.payload, in, &count);
                    break;
                case TICK:
                    tick(in, &count);
                    break;
            }
        }
        count = start + packet.length + 2;
    }
    str_send[count_send] = '\0';
}

void free_net() {
    type_net_sensor *tmp_sensor, *sensor_ptr = net_sensor_ptr;
    type_net_other *tmp_other, *other_ptr = net_other_ptr;
    type_net_switch *tmp_switch, *switch_ptr = net_switch_ptr;
    type_queue_request *tmp_request, *request_ptr = queue_request;

    while (sensor_ptr) {
        tmp_sensor = sensor_ptr->next;
        sensor_free(sensor_ptr);
        free(sensor_ptr);
        sensor_ptr = tmp_sensor;
    }

    while (switch_ptr) {
        tmp_switch = switch_ptr->next;
        switch_free(switch_ptr);
        free(switch_ptr);
        switch_ptr = tmp_switch;
    }

    while (other_ptr) {
        tmp_other = other_ptr->next;
        other_free(other_ptr);
        free(other_ptr);
        other_ptr = tmp_other;
    }

    while (request_ptr) {
        tmp_request = request_ptr->next;
        free(request_ptr);
        request_ptr = tmp_request;
    }
}

int check_base64(uint8_t c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
        return 1;
    } else if (c == ' ' || c == '\n' || c == '\t') {
        return 0;
    }
    return -1;
}

bool b64u_decode(const uint8_t *str, uint8_t **p_data, size_t *p_size)
{
    int value;
    int count_str = 0, tmp;
    size_t count = 0, flag;
    uint8_t calc;
    uint8_t buf[3];

    uint8_t *ptr = calloc(100, sizeof(uint8_t));

    while (str[count] != '\0') {
        value = 0;
        flag = 0;
        for (int i = 0; i < 4 && str[count] != '\0'; i++) {
            while (str[count] != '\0' && (tmp = check_base64(str[count++])) != 1) {
                if (tmp == -1) {
                    free(ptr);
                    return false;
                }
            }
            if (str[count - 1] != '\0') {
                flag++;
                if (str[count - 1] >= 'A' && str[count - 1] <= 'Z') {
                    calc = str[count - 1] - 'A';
                } else if (str[count - 1] >= 'a' && str[count - 1] <= 'z') {
                    calc = str[count - 1] - 'a' + 26;
                } else if (str[count - 1] >= '0' && str[count - 1] <= '9') {
                    calc = str[count - 1] - '0' + 52;
                } else if (str[count - 1] == '-') {
                    calc = 62;
                } else {
                    calc = 63;
                }
                value |= calc << ((3 - i) * 6);
            }
        }
        memcpy(&buf, &value, sizeof(buf));
        ptr[count_str] = buf[2];
        if (flag == 3) {
            ptr[count_str + 1] = buf[1];
            count_str++;
        } else if (flag == 4) {
            ptr[count_str + 1] = buf[1];
            ptr[count_str + 2] = buf[0];
            count_str += 2;
        } else if (flag == 1) {
            free(ptr);
            return false;
        }
        count_str++;
        if (100 - (count_str % 100) < 5) {
            ptr = realloc(ptr, (count_str / 100 + 2) * 100 * sizeof(uint8_t));
        }
    }
    ptr[count_str] = '\0';
    *p_size = count_str;
    *p_data = ptr;
    return true;
}

uint8_t *b64u_encode(const uint8_t *data, size_t size)
{
    uint8_t alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    int count = 0, count_str = 0, tmp, flag = 0, value;

    uint8_t *ptr;
    if (!(ptr = calloc((size / 3 + 1) * 4, sizeof(uint8_t)))) {
        return NULL;
    }

    while (count < size) {
        value = 0;
        value |= (data[count] & 0xff) << 16;
        if (size - count == 1) {
            flag = 12;
        } else {
            value |= (data[count + 1] & 0xff) << 8;
            if (size - count == 2) {
                flag = 6;
            } else {
                value |= (data[count + 2] & 0xff);
            }
        }

        tmp = 18;
        for (int j = 0xfc0000; j > 0 && tmp >= flag; j >>= 6) {
            ptr[count_str] = alphabet[(value & j) >> tmp];
            tmp -= 6;
            count_str++;
        }
        count += 3;
    }
    ptr[count_str] = '\0';
    return ptr;
}

size_t write_response(void *data, size_t size, size_t nmemb, void *userdata) {
    size_t total_size = nmemb * size;
    Response *tmp = (Response *) userdata;
    uint8_t *ptr = realloc(tmp->str, tmp->size + total_size + 1);
    tmp->str = ptr;

    memcpy(&(tmp->str[tmp->size]), data, total_size);
    tmp->size += total_size;
    tmp->str[tmp->size] = '\0';

    return total_size;
}

uint8_t *send_packet(CURL *curl, uint8_t *data, char *argv[]) {
    Response response;
    response.str = calloc(1, 1);
    response.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, argv[1]);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) &response);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);

    curl_easy_perform(curl);

    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    if (response_code == 204) {
        free(response.str);
        free_net();
        curl_easy_cleanup(curl);
        exit(0);
    } else if (response_code != 200) {
        exit(99);
    }

    return response.str;
}

int
main(int argc, char *argv[])
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "ERROR CURL INITIALIZING\n");
        return 99;
    }

    sscanf(argv[2], "%x", &hub_address);
    size_t str_size;

    net_other_ptr = calloc(1, sizeof(type_net_other));
    net_other_ptr->dev_type = 0x1;
    sscanf(argv[2], "%x", &net_other_ptr->address);
    net_other_ptr->dev_name = calloc(7, sizeof(uint8_t));
    net_other_ptr->dev_name[0] = 5;
    memcpy(&(net_other_ptr->dev_name[1]), "HUB01", 6);

    str_send = calloc(100, sizeof(uint8_t));
    count_send = 1;

    write_uleb128(str_send, &count_send, hub_address);
    write_uleb128(str_send, &count_send, 0x3fff);
    write_uleb128(str_send, &count_send, serial++);
    str_send[count_send++] = 0x1;
    str_send[count_send++] = 0x1;
    memcpy(&(str_send[count_send]), net_other_ptr->dev_name, 6);
    count_send += 6;
    str_send[0] = count_send - 1;
    str_send[count_send++] = compute_crc8(&str_send[1], str_send[0]);
    str_send[count_send] = '\0';

    uint8_t *str_response, *str_encoded, *str_parse;

    while (true) {
        str_encoded = b64u_encode(str_send, count_send);
        free(str_send);

        str_response = send_packet(curl, str_encoded, argv);
        free(str_encoded);

        b64u_decode(str_response, &str_parse, &str_size);
        free(str_response);

        str_send = calloc(100, sizeof(uint8_t));
        count_send = 0;

        parse_packet(str_parse);
    }
    return 0;
}
