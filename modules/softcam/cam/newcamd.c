/*
 * Astra SoftCAM Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 *
 * This module is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This module is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this module.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <astra.h>
#include "../module_cam.h"

#include <openssl/des.h>

#define NEWCAMD_HEADER_SIZE 12
#define NEWCAMD_MSG_SIZE EM_MAX_SIZE
#define MAX_PROV_COUNT 16

typedef enum
{
    NEWCAMD_UNKNOWN = 0,
    NEWCAMD_STARTED,
    NEWCAMD_CONNECTED,
    NEWCAMD_AUTHORIZED,
    NEWCAMD_READY,
    NEWCAMD_STOPPED,
} newcamd_status_t;

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_CAM_DATA();

    /* Config */
    const char *name;

    const char *host;
    int port;
    int timeout;

    const char *user;
    const char *pass;
    uint8_t key[14];

    /* Base */
    char password_md5[36];

    asc_socket_t *sock;
    newcamd_status_t status;
    asc_timer_t *timeout_timer;

    uint8_t *prov_buffer;

    struct
    {
        uint8_t key[16];
        DES_key_schedule ks1;
        DES_key_schedule ks2;
    } triple_des;

    uint16_t msg_id;        // curren message id
    em_packet_t *packet;    // current packet
    uint64_t last_key[2];   // NDS

    size_t buffer_size;
    uint8_t buffer[NEWCAMD_MSG_SIZE];
};

#define MSG(_msg) "[newcamd %s] " _msg, mod->name

typedef enum {
    NEWCAMD_MSG_ERROR = 0,
    NEWCAMD_MSG_FIRST = 0xDF,
    NEWCAMD_MSG_CLIENT_2_SERVER_LOGIN,
    NEWCAMD_MSG_CLIENT_2_SERVER_LOGIN_ACK,
    NEWCAMD_MSG_CLIENT_2_SERVER_LOGIN_NAK,
    NEWCAMD_MSG_CARD_DATA_REQ,
    NEWCAMD_MSG_CARD_DATA,
} newcamd_cmd_t;

/*
 * ooooooooooo ooooooooo  ooooooooooo      o
 * 88  888  88  888    88o 888    88      888
 *     888      888    888 888ooo8       8  88
 *     888      888    888 888    oo    8oooo88
 *    o888o    o888ooo88  o888ooo8888 o88o  o888o
 *
 */

static void triple_des_set_key(module_data_t *mod, uint8_t *key, size_t key_size)
{
    uint8_t tmp_key[14];
    memcpy(tmp_key, mod->key, sizeof(tmp_key));

    // set key
    for(size_t i = 0; i < key_size; ++i)
        tmp_key[i % sizeof(tmp_key)] ^= key[i];

    uint8_t *triple_des_key = mod->triple_des.key;
    triple_des_key[0] = tmp_key[0] & 0xfe;
    triple_des_key[1] = ((tmp_key[0] << 7) | (tmp_key[1] >> 1)) & 0xfe;
    triple_des_key[2] = ((tmp_key[1] << 6) | (tmp_key[2] >> 2)) & 0xfe;
    triple_des_key[3] = ((tmp_key[2] << 5) | (tmp_key[3] >> 3)) & 0xfe;
    triple_des_key[4] = ((tmp_key[3] << 4) | (tmp_key[4] >> 4)) & 0xfe;
    triple_des_key[5] = ((tmp_key[4] << 3) | (tmp_key[5] >> 5)) & 0xfe;
    triple_des_key[6] = ((tmp_key[5] << 2) | (tmp_key[6] >> 6)) & 0xfe;
    triple_des_key[7] = tmp_key[6] << 1;
    triple_des_key[8] = tmp_key[7] & 0xfe;
    triple_des_key[9] = ((tmp_key[7] << 7) | (tmp_key[8] >> 1)) & 0xfe;
    triple_des_key[10] = ((tmp_key[8] << 6) | (tmp_key[9] >> 2)) & 0xfe;
    triple_des_key[11] = ((tmp_key[9] << 5) | (tmp_key[10] >> 3)) & 0xfe;
    triple_des_key[12] = ((tmp_key[10] << 4) | (tmp_key[11] >> 4)) & 0xfe;
    triple_des_key[13] = ((tmp_key[11] << 3) | (tmp_key[12] >> 5)) & 0xfe;
    triple_des_key[14] = ((tmp_key[12] << 2) | (tmp_key[13] >> 6)) & 0xfe;
    triple_des_key[15] = tmp_key[13] << 1;

    DES_set_odd_parity((DES_cblock *)&triple_des_key[0]);
    DES_set_odd_parity((DES_cblock *)&triple_des_key[8]);
    DES_key_sched((DES_cblock *)&triple_des_key[0], &mod->triple_des.ks1);
    DES_key_sched((DES_cblock *)&triple_des_key[8], &mod->triple_des.ks2);
} /* triple_des_set_key */

static uint8_t xor_sum(const uint8_t *mem, int len)
{
    uint8_t cs = 0;
    while(len > 0)
    {
        cs ^= *mem++;
        len--;
    }
    return cs;
}

/*
 * ooooooooooo ooooo oooo     oooo ooooooooooo  ooooooo  ooooo  oooo ooooooooooo
 * 88  888  88  888   8888o   888   888    88 o888   888o 888    88  88  888  88
 *     888      888   88 888o8 88   888ooo8   888     888 888    88      888
 *     888      888   88  888  88   888    oo 888o   o888 888    88      888
 *    o888o    o888o o88o  8  o88o o888ooo8888  88ooo88    888oo88      o888o
 *
 */

static void newcamd_connect(void *);
static void newcamd_disconnect(module_data_t *);

static void timeout_timer_callback(void *arg)
{
    module_data_t *mod = arg;

    asc_timer_destroy(mod->timeout_timer);
    mod->timeout_timer = NULL;

    switch(mod->status)
    {
        case NEWCAMD_UNKNOWN:
        case NEWCAMD_STARTED:
            asc_log_error(MSG("connection timeout. try again"));
            break;
        case NEWCAMD_READY:
        {
            asc_log_warning(MSG("receiving timeout. drop packet"));
            // TODO: drop packet...
            return;
        }
        default:
            asc_log_error(MSG("receiving timeout. reconnect"));
            break;
    }

    newcamd_disconnect(mod);
    newcamd_connect(mod);
}

static void newcamd_timeout_set(module_data_t *mod)
{
    if(mod->timeout_timer)
        return;
    mod->timeout_timer = asc_timer_init(mod->timeout, timeout_timer_callback, mod);
}

static void newcamd_timeout_unset(module_data_t *mod)
{
    if(mod->timeout_timer)
    {
        asc_timer_destroy(mod->timeout_timer);
        mod->timeout_timer = NULL;
    }
}

/*
 *  oooooooo8 ooooooooooo oooo   oooo ooooooooo
 * 888         888    88   8888o  88   888    88o
 *  888oooooo  888ooo8     88 888o88   888    888
 *         888 888    oo   88   8888   888    888
 * o88oooo888 o888ooo8888 o88o    88  o888ooo88
 *
 */

static int newcamd_send_msg(module_data_t *mod, em_packet_t *packet)
{
    // packet header
    const size_t buffer_size = mod->buffer_size;
    uint8_t *buffer = mod->buffer;

    memset(&buffer[2], 0, NEWCAMD_HEADER_SIZE - 2);
    if(packet)
    {
        const uint16_t pnr = packet->pnr;
        buffer[4] = pnr >> 8;
        buffer[5] = pnr & 0xff;

        ++mod->msg_id;
        buffer[2] = mod->msg_id >> 8;
        buffer[3] = mod->msg_id & 0xff;
    }

    // packet content
    buffer[13] = ((buffer[13] & 0xf0) | (((buffer_size - 3) >> 8) & 0x0f));
    buffer[14] = (buffer_size - 3) & 0xff;

    uint16_t packet_size = NEWCAMD_HEADER_SIZE + buffer_size;
    // pad_message
    const uint8_t no_pad_bytes= (8 - ((packet_size - 1) % 8)) % 8;
    if((packet_size + no_pad_bytes + 1) >= (NEWCAMD_MSG_SIZE - 8))
    {
        asc_log_error(MSG("send: failed to pad message"));
        return 0;
    }

    DES_cblock pad_bytes;
    DES_random_key((DES_cblock *)pad_bytes);
    memcpy(&buffer[packet_size], pad_bytes, no_pad_bytes);
    packet_size += no_pad_bytes;
    buffer[packet_size] = xor_sum(&buffer[2], packet_size - 2);
    ++packet_size;

    // encrypt
    DES_cblock ivec;
    DES_random_key((DES_cblock *)ivec);
    if(packet_size + sizeof(ivec) >= NEWCAMD_MSG_SIZE)
    {
        asc_log_error(MSG("send: failed to encrypt"));
        return 0;
    }
    memcpy(&buffer[packet_size], ivec, sizeof(ivec));
    DES_ede2_cbc_encrypt(&buffer[2], &buffer[2], packet_size - 2
                         , &mod->triple_des.ks1, &mod->triple_des.ks2
                         , (DES_cblock *)ivec, DES_ENCRYPT);
    packet_size += sizeof(ivec);

    buffer[0] = (packet_size - 2) >> 8;
    buffer[1] = (packet_size - 2) & 0xff;

    if(asc_socket_send_direct(mod->sock, buffer, packet_size) != packet_size)
    {
        asc_log_error(MSG("send: failed [%d]"), errno);
        if(packet)
        {
            packet->buffer[2] = 0x00;
            // TODO: cam_callback(mod, packet);
            --mod->msg_id;
        }
        return 0;
    }

    newcamd_timeout_set(mod);
    return packet_size;
} /* newcamd_send_msg */

static int newcamd_send_cmd(module_data_t *mod, newcamd_cmd_t cmd)
{
    uint8_t *buffer = &mod->buffer[NEWCAMD_HEADER_SIZE];
    buffer[0] = cmd;
    buffer[1] = 0;
    buffer[2] = 0;
    mod->buffer_size = 3;
    return newcamd_send_msg(mod, NULL);
}

/*
 * oooooooooo  ooooooooooo  oooooooo8 ooooo  oooo
 *  888    888  888    88 o888     88  888    88
 *  888oooo88   888ooo8   888           888  88
 *  888  88o    888    oo 888o     oo    88888
 * o888o  88o8 o888ooo8888 888oooo88      888
 *
 */

int newcamd_recv_msg(module_data_t *mod, em_packet_t *packet)
{
    if(mod->status != NEWCAMD_READY)
        newcamd_timeout_unset(mod);

    uint8_t *buffer = mod->buffer;
    buffer[NEWCAMD_HEADER_SIZE] = 0x00;

    uint16_t packet_size = 0;

    if(asc_socket_recv(mod->sock, buffer, 2) != 2)
    {
        asc_log_error(MSG("recv: failed to read the length of message [%s]"), strerror(errno));
        return 0;
    }
    packet_size = (buffer[0] << 8) | buffer[1];
    if(packet_size > (NEWCAMD_MSG_SIZE - 2))
    {
        asc_log_error(MSG("recv: message size %d is greater than %d")
                      , packet_size, NEWCAMD_MSG_SIZE);
        return 0;
    }
    const ssize_t read_size = asc_socket_recv(mod->sock, &buffer[2], packet_size);
    if(read_size != packet_size)
    {
        asc_log_error(MSG("recv: failed to read message [%s]"), strerror(errno));
        return 0;
    }

    // decrypt
    if(!(packet_size % 8) && (packet_size >= 16))
    {
        DES_cblock ivec;
        packet_size -= sizeof(ivec);
        memcpy(ivec, &buffer[packet_size + 2], sizeof(ivec));
        DES_ede2_cbc_encrypt(&buffer[2], &buffer[2], packet_size
                             , &mod->triple_des.ks1, &mod->triple_des.ks2
                             , (DES_cblock *)ivec, DES_DECRYPT);
    }
    if(xor_sum(&buffer[2], packet_size))
    {
        asc_log_error(MSG("recv: bad checksum"), NULL);
        return 0;
    }

    const uint8_t msg_type = buffer[NEWCAMD_HEADER_SIZE];
    if(packet && msg_type >= 0x80 && msg_type <= 0x8F)
    {
        const uint16_t msg_id = (buffer[2] << 8) | buffer[3];
        if(msg_id != mod->msg_id)
        {
            asc_log_warning(MSG("wrong message id. required:%d receive:%d"), mod->msg_id, msg_id);
            return 0;
        }
    }

    mod->buffer_size = (((buffer[NEWCAMD_HEADER_SIZE + 1] << 8)
                        | buffer[NEWCAMD_HEADER_SIZE + 2]) & 0x0fff)
                        + 3;

    return mod->buffer_size;
} /* newcamd_recv_msg */

static newcamd_cmd_t newcamd_recv_cmd(module_data_t *mod)
{
    if(newcamd_recv_msg(mod, NULL))
        return mod->buffer[NEWCAMD_HEADER_SIZE];

    return NEWCAMD_MSG_ERROR;
}

/*
 * ooooo         ooooooo     ooooooo8 ooooo oooo   oooo
 *  888        o888   888o o888    88  888   8888o  88
 *  888        888     888 888         888   88 888o88
 *  888      o 888o   o888 888o   oooo 888   88   8888
 * o888ooooo88   88ooo88    888ooo888 o888o o88o    88
 *
 */

static int newcamd_login_1(module_data_t *mod)
{
    newcamd_timeout_unset(mod); // connection timeout

    mod->msg_id = 0;
    uint8_t rnd_data[14];
    const ssize_t len = asc_socket_recv(mod->sock, rnd_data, sizeof(rnd_data));
    if(len != sizeof(rnd_data))
    {
        asc_log_error(MSG("%s(): len=%d [%s]"), __FUNCTION__, len, strerror(errno));
        return 0;
    }
    triple_des_set_key(mod, rnd_data, sizeof(rnd_data));

    uint8_t *buffer = &mod->buffer[NEWCAMD_HEADER_SIZE];

    const size_t u_len = strlen(mod->user) + 1;
    memcpy(&buffer[3], mod->user, u_len);
    md5_crypt(mod->pass, "$1$abcdefgh$", mod->password_md5);
    const size_t p_len = 35; /* strlen(mod->password_md5) */
    memcpy(&buffer[3 + u_len], mod->password_md5, p_len);
    buffer[0] = NEWCAMD_MSG_CLIENT_2_SERVER_LOGIN;
    buffer[1] = 0;
    buffer[2] = u_len + p_len;
    mod->buffer_size = u_len + p_len + 3;

    mod->status = NEWCAMD_CONNECTED;
    return newcamd_send_msg(mod, NULL);
}

static int newcamd_login_2(module_data_t *mod)
{
    uint8_t *buffer = &mod->buffer[NEWCAMD_HEADER_SIZE];

    if(newcamd_recv_cmd(mod) != NEWCAMD_MSG_CLIENT_2_SERVER_LOGIN_ACK)
    {
        asc_log_error(MSG("login failed [0x%02X]"), buffer[0]);
        return 0;
    }

    const size_t p_len = 35;
    triple_des_set_key(mod, (uint8_t *)mod->password_md5, p_len - 1);

    mod->status = NEWCAMD_AUTHORIZED;
    return newcamd_send_cmd(mod, NEWCAMD_MSG_CARD_DATA_REQ);
}

static int newcamd_login_3(module_data_t *mod)
{
    uint8_t *buffer = &mod->buffer[NEWCAMD_HEADER_SIZE];

    if(!newcamd_recv_msg(mod, NULL))
        return 0;
    if(buffer[0] != NEWCAMD_MSG_CARD_DATA)
    {
        asc_log_error(MSG("NEWCAMD_MSG_CARD_DATA"));
        return 0;
    }

    mod->__cam.caid = (buffer[4] << 8) | buffer[5];
    memcpy(mod->__cam.ua, &buffer[6], 8);

    char hex_str[32];
    asc_log_info(MSG("CaID=0x%04X au=%s UA=%s")
                 , mod->__cam.caid
                 , (buffer[3] == 1) ? "YES" : "NO"
                 , hex_to_str(hex_str, mod->__cam.ua, 8));

    if(buffer[3] != 1) // disable emm if not admin
        mod->__cam.disable_emm = 1;

    const int prov_count = (buffer[14] <= MAX_PROV_COUNT) ? buffer[14] : MAX_PROV_COUNT;

    static const int info_size = 3 + 8; /* ident + sa */
    mod->prov_buffer = malloc(prov_count * info_size);

    for(int i = 0; i < prov_count; i++)
    {
        uint8_t *p = &mod->prov_buffer[i * info_size];
        memcpy(&p[0], &buffer[15 + (11 * i)], 3);
        memcpy(&p[3], &buffer[18 + (11 * i)], 8);
        module_cam_set_provider(mod, p);
        asc_log_info(MSG("Prov:%d ID:%s SA:%s"), i
                     , hex_to_str(hex_str, &p[0], 3)
                     , hex_to_str(&hex_str[8], &p[3], 8));
    }

    mod->status = NEWCAMD_READY;
    return 1;
}

/*
 * oooooooooo  ooooooooooo  oooooooo8 oooooooooo    ooooooo  oooo   oooo oooooooo8 ooooooooooo
 *  888    888  888    88  888         888    888 o888   888o 8888o  88 888         888    88
 *  888oooo88   888ooo8     888oooooo  888oooo88  888     888 88 888o88  888oooooo  888ooo8
 *  888  88o    888    oo          888 888        888o   o888 88   8888         888 888    oo
 * o888o  88o8 o888ooo8888 o88oooo888 o888o         88ooo88  o88o    88 o88oooo888 o888ooo8888
 *
 */

static int newcamd_response(module_data_t *mod)
{
    const int len = newcamd_recv_msg(mod, mod->packet);
    if(len)
    {
        uint8_t *buffer = &mod->buffer[NEWCAMD_HEADER_SIZE];

        if(len == 19)
        {
            // NDS
            uint64_t *key_0 = (uint64_t *)&buffer[3];
            uint64_t *key_1 = (uint64_t *)&buffer[11];
            if(!(*key_0))
            {
                *key_0 = mod->last_key[0];
                mod->last_key[1] = *key_1;
            }
            else if(!(*key_1))
            {
                *key_1 = mod->last_key[1];
                mod->last_key[0] = *key_0;
            }

            memcpy(mod->packet->buffer, buffer, 19);
            mod->packet->buffer_size = 19;
        }
        else if(len >= 3)
        {
            memcpy(mod->packet->buffer, buffer, 3);
            mod->packet->buffer_size = 3;
        }
        else
        {
            mod->packet->buffer[2] = 0x00;
            asc_log_error(MSG("pnr:%d incorrect message length %d"), mod->packet->pnr, len);
            mod->packet->buffer_size = 3;
        }

        // cam_callback(mod, packet);
    }

    return len;
}

/*
 * oooooooooo         o88 oooo     oooo                 oooo
 *  888    888       o88   88   88  88         ooooooo   888ooooo
 *  888oooo88      o88      88 888 88        888     888 888    888
 *  888  88o     o88         888 888         888         888    888
 * o888o  88o8 o88            8   8            88ooo888 o888ooo88
 *            o88
 */

static void on_close(void *arg)
{
    module_data_t *mod = arg;

    asc_log_error(MSG("failed to read from socket [%s]"), strerror(errno));
    newcamd_disconnect(mod);
    newcamd_timeout_set(mod);
}

static void on_read(void *arg)
{
    module_data_t *mod = arg;

    int ret = 0;
    switch(mod->status) {
        case NEWCAMD_STARTED:
            ret = newcamd_login_1(mod);
            break;
        case NEWCAMD_CONNECTED:
            ret = newcamd_login_2(mod);
            break;
        case NEWCAMD_AUTHORIZED:
            ret = newcamd_login_3(mod);
            break;
        case NEWCAMD_READY:
            ret = newcamd_response(mod);
            break;
        case NEWCAMD_STOPPED:
            break;
        default: // logout
            ret = -1;
            break;
    }

    if(ret <= 0)
        on_close(mod);
}

static void on_connect_error(void *arg)
{
    module_data_t *mod = arg;

    asc_log_error(MSG("socket not ready [%s]"), asc_socket_error());
    newcamd_disconnect(mod);
    newcamd_timeout_set(mod);
}

static void on_connect(void *arg)
{
    module_data_t *mod = arg;

    mod->status = NEWCAMD_STARTED;
    asc_socket_set_on_read(mod->sock, on_read);
}

/*
 *   oooooooo8   ooooooo  oooo   oooo oooo   oooo ooooooooooo  oooooooo8 ooooooooooo
 * o888     88 o888   888o 8888o  88   8888o  88   888    88 o888     88 88  888  88
 * 888         888     888 88 888o88   88 888o88   888ooo8   888             888
 * 888o     oo 888o   o888 88   8888   88   8888   888    oo 888o     oo     888
 *  888oooo88    88ooo88  o88o    88  o88o    88  o888ooo8888 888oooo88     o888o
 *
 */

static void newcamd_connect(void *arg)
{
    module_data_t *mod = arg;

    mod->sock = asc_socket_open_tcp4(mod);
    asc_socket_connect(mod->sock, mod->host, mod->port, on_connect, on_connect_error);
}

static void newcamd_disconnect(module_data_t *mod)
{
    newcamd_timeout_unset(mod);

    if(mod->sock)
    {
        asc_socket_close(mod->sock);
        mod->sock = NULL;
    }
    mod->status = NEWCAMD_UNKNOWN;

    module_cam_reset(mod);
}

static void module_init(module_data_t *mod)
{
    module_option_string("name", &mod->name);
    asc_assert(mod->name != NULL, "[newcamd] option 'name' is required");

    module_option_string("host", &mod->host);
    asc_assert(mod->host != NULL, MSG("option 'host' is required"));
    module_option_number("port", &mod->port);
    asc_assert(mod->port > 0 && mod->port < 65535, MSG("option 'port' is required"));

    module_option_string("user", &mod->user);
    asc_assert(mod->user != NULL, MSG("option 'user' is required"));
    module_option_string("pass", &mod->pass);
    asc_assert(mod->pass != NULL, MSG("option 'pass' is required"));

    const char *key = NULL;
    module_option_string("key", &key);
    asc_assert(key != NULL, MSG("option 'key' is required"));
    str_to_hex(key, mod->key, sizeof(mod->key));

    module_option_number("timeout", &mod->timeout);
    if(!mod->timeout)
        mod->timeout = 8;
    mod->timeout *= 1000;

    module_cam_init(mod);
}

static void module_destroy(module_data_t *mod)
{
    newcamd_disconnect(mod);

    if(mod->prov_buffer)
        free(mod->prov_buffer);

    module_cam_destroy(mod);
}

MODULE_CAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_CAM_METHODS_REF()
};
MODULE_LUA_REGISTER(newcamd)
