/*
 * Astra Module: SoftCAM. Newcamd Client
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2014, Andrey Dyldin <and@cesbo.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <astra.h>
#include "../module_cam.h"

#include <openssl/des.h>

#define NEWCAMD_HEADER_SIZE 12
#define NEWCAMD_MSG_SIZE (NEWCAMD_HEADER_SIZE + EM_MAX_SIZE)
#define MAX_PROV_COUNT 16
#define KEY_SIZE 14

#define MSG(_msg) "[newcamd %s] " _msg, mod->config.name

typedef union {
    uint8_t a[8];
    uint64_t l;
} csa_key_t;

struct module_data_t
{
    MODULE_CAM_DATA();

    struct
    {
        const char *name;

        const char *host;
        int port;
        int timeout;

        const char *user;
        char pass[36];

        uint8_t key[KEY_SIZE];

        bool disable_emm;
    } config;

    int status;
    asc_socket_t *sock;
    asc_timer_t *timeout;

    uint8_t *prov_buffer;

    struct
    {
        uint8_t key[16];
        DES_key_schedule ks1;
        DES_key_schedule ks2;
    } triple_des;

    uint16_t msg_id;        // curren message id
    em_packet_t *packet;    // current packet
    csa_key_t last_key[2];  // NDS

    uint8_t buffer[NEWCAMD_MSG_SIZE];
    size_t payload_size;    // to send
    size_t buffer_skip;     // to recv
};

typedef enum {
    NEWCAMD_MSG_ERROR = 0,
    NEWCAMD_MSG_FIRST = 0xDF,
    NEWCAMD_MSG_CLIENT_2_SERVER_LOGIN,
    NEWCAMD_MSG_CLIENT_2_SERVER_LOGIN_ACK,
    NEWCAMD_MSG_CLIENT_2_SERVER_LOGIN_NAK,
    NEWCAMD_MSG_CARD_DATA_REQ,
    NEWCAMD_MSG_CARD_DATA,
    NEWCAMD_MSG_KEEPALIVE = 0xFD,
} newcamd_cmd_t;

static void newcamd_connect(module_data_t *mod);
static void newcamd_reconnect(module_data_t *mod, bool timeout);

/*
 * ooooooooooo ooooooooo  ooooooooooo      o
 * 88  888  88  888    88o 888    88      888
 *     888      888    888 888ooo8       8  88
 *     888      888    888 888    oo    8oooo88
 *    o888o    o888ooo88  o888ooo8888 o88o  o888o
 *
 */

static void triple_des_set_key(module_data_t *mod, const uint8_t *key, size_t key_size)
{
    uint8_t tmp_key[14];
    memcpy(tmp_key, mod->config.key, sizeof(tmp_key));

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
 *  oooooooo8    ooooooo     oooooooo8 oooo   oooo ooooooooooo ooooooooooo
 * 888         o888   888o o888     88  888  o88    888    88  88  888  88
 *  888oooooo  888     888 888          888888      888ooo8        888
 *         888 888o   o888 888o     oo  888  88o    888    oo      888
 * o88oooo888    88ooo88    888oooo88  o888o o888o o888ooo8888    o888o
 *
 */

static void on_timeout(void *arg)
{
    module_data_t *mod = arg;

    asc_timer_destroy(mod->timeout);
    mod->timeout = NULL;

    switch(mod->status)
    {
        case -1:
            newcamd_connect(mod);
            return;
        case 0:
            asc_log_error(MSG("connection timeout"));
            break;
        default:
            asc_log_error(MSG("response timeout"));
            break;
    }

    newcamd_reconnect(mod, false);
}

static void on_newcamd_close(void *arg)
{
    module_data_t *mod = arg;

    if(!mod->sock)
        return;

    asc_socket_close(mod->sock);
    mod->sock = NULL;

    if(mod->timeout)
    {
        asc_timer_destroy(mod->timeout);
        mod->timeout = NULL;
    }

    module_cam_reset(&mod->__cam);

    if(mod->prov_buffer)
    {
        free(mod->prov_buffer);
        mod->prov_buffer = NULL;
    }

    if(mod->packet)
    {
        free(mod->packet);
        mod->packet = NULL;
    }

    if(mod->status == 0)
        asc_log_error(MSG("connection failed"));
    else if(mod->status == 1)
        asc_log_error(MSG("failed to parse response"));

    if(mod->status != -1)
    {
        mod->status = -1;
        mod->timeout = asc_timer_init(mod->config.timeout, on_timeout, mod);
    }
    else
        mod->status = 0;
}

/*
 *  oooooooo8 ooooooooooo oooo   oooo ooooooooo
 * 888         888    88   8888o  88   888    88o
 *  888oooooo  888ooo8     88 888o88   888    888
 *         888 888    oo   88   8888   888    888
 * o88oooo888 o888ooo8888 o88o    88  o888ooo88
 *
 */

static void on_newcamd_ready(void *arg)
{
    module_data_t *mod = arg;

    memset(mod->buffer, 0, NEWCAMD_HEADER_SIZE);

    uint8_t *buffer = &mod->buffer[NEWCAMD_HEADER_SIZE];

    if(mod->packet)
    {
        memcpy(buffer, mod->packet->buffer, mod->packet->buffer_size);
        mod->payload_size = mod->packet->buffer_size - 3;

        mod->msg_id = (mod->msg_id + 1) & 0xFFFF;
        mod->buffer[2] = mod->msg_id >> 8;
        mod->buffer[3] = mod->msg_id & 0xff;

        const uint16_t pnr = mod->packet->decrypt->cas_pnr;
        mod->buffer[4] = pnr >> 8;
        mod->buffer[5] = pnr & 0xff;
    }

    buffer[1] = (mod->payload_size >> 8) & 0x0F;
    buffer[2] = (mod->payload_size     ) & 0xFF;

    size_t packet_size = NEWCAMD_HEADER_SIZE + 3 + mod->payload_size;
    const uint8_t no_pad_bytes = (8 - ((packet_size - 1) % 8)) % 8;

    if((packet_size + no_pad_bytes + 1) >= (NEWCAMD_MSG_SIZE - 8))
    {
        asc_log_error(MSG("failed to pad message"));
        newcamd_reconnect(mod, true);
        return;
    }

    DES_cblock pad_bytes;
    DES_random_key((DES_cblock *)pad_bytes);
    memcpy(&mod->buffer[packet_size], pad_bytes, no_pad_bytes);
    packet_size += no_pad_bytes;
    mod->buffer[packet_size] = xor_sum(&mod->buffer[2], packet_size - 2);
    ++packet_size;

    // encrypt
    DES_cblock ivec;
    DES_random_key((DES_cblock *)ivec);
    if(packet_size + sizeof(ivec) >= NEWCAMD_MSG_SIZE)
    {
        asc_log_error(MSG("failed to encrypt message"));
        newcamd_reconnect(mod, true);
        return;
    }
    memcpy(&mod->buffer[packet_size], ivec, sizeof(ivec));
    DES_ede2_cbc_encrypt(  &mod->buffer[2], &mod->buffer[2], packet_size - 2
                         , &mod->triple_des.ks1, &mod->triple_des.ks2
                         , (DES_cblock *)ivec, DES_ENCRYPT);
    packet_size += sizeof(ivec);

    mod->buffer[0] = ((packet_size - 2) >> 8) & 0xFF;
    mod->buffer[1] = ((packet_size - 2)     ) & 0xFF;

    if(asc_socket_send(mod->sock, mod->buffer, packet_size) != (ssize_t)packet_size)
    {
        asc_log_error(MSG("failed to send message"));
        newcamd_reconnect(mod, true);
        return;
    }

    asc_socket_set_on_ready(mod->sock, NULL);

    mod->buffer_skip = 0;
    mod->payload_size = 0;

    if(!mod->timeout)
        mod->timeout = asc_timer_init(mod->config.timeout, on_timeout, mod);
}

/*
 * oooooooooo  ooooooooooo  oooooooo8 ooooo  oooo
 *  888    888  888    88 o888     88  888    88
 *  888oooo88   888ooo8   888           888  88
 *  888  88o    888    oo 888o     oo    88888
 * o888o  88o8 o888ooo8888 888oooo88      888
 *
 */

static void on_newcamd_read_packet(void *arg)
{
    module_data_t *mod = arg;

    if(mod->buffer_skip < 2)
    {
        const ssize_t len = asc_socket_recv(  mod->sock
                                            , &mod->buffer[mod->buffer_skip]
                                            , 2 - mod->buffer_skip);
        if(len <= 0)
        {
            asc_log_error(MSG("failed to read header"));
            newcamd_reconnect(mod, true);
            return;
        }
        mod->buffer_skip += len;
        if(mod->buffer_skip != 2)
            return;

        mod->payload_size = 2 + ((mod->buffer[0] << 8) | mod->buffer[1]);
        if(mod->payload_size > NEWCAMD_MSG_SIZE)
        {
            asc_log_error(MSG("wrong message size"));
            newcamd_reconnect(mod, true);
            return;
        }

        return;
    }

    const ssize_t len = asc_socket_recv(  mod->sock
                                        , &mod->buffer[mod->buffer_skip]
                                        , mod->payload_size - mod->buffer_skip);
    if(len <= 0)
    {
        asc_log_error(MSG("failed to read message"));
        newcamd_reconnect(mod, true);
        return;
    }

    mod->buffer_skip += len;
    if(mod->buffer_skip != mod->payload_size)
        return;

    size_t packet_size = mod->payload_size - 2;
    mod->payload_size = 0;
    mod->buffer_skip = 0;

    // decrypt
    if(   (packet_size % 8 == 0)
       && (packet_size > NEWCAMD_HEADER_SIZE + 3))
    {
        DES_cblock ivec;
        packet_size -= sizeof(ivec);
        memcpy(ivec, &mod->buffer[packet_size + 2], sizeof(ivec));
        DES_ede2_cbc_encrypt(  &mod->buffer[2], &mod->buffer[2], packet_size
                             , &mod->triple_des.ks1, &mod->triple_des.ks2
                             , (DES_cblock *)ivec, DES_DECRYPT);
    }
    if(xor_sum(&mod->buffer[2], packet_size))
    {
        asc_log_error(MSG("bad message checksum"));
        newcamd_reconnect(mod, true);
        return;
    }

    const uint8_t msg_type = mod->buffer[NEWCAMD_HEADER_SIZE];

    uint8_t *buffer = &mod->buffer[NEWCAMD_HEADER_SIZE];
    mod->payload_size = ((buffer[1] & 0x0F) << 8) | buffer[2];

    if(mod->status == 3)
    {
        if(!mod->packet && msg_type == NEWCAMD_MSG_KEEPALIVE)
        {
            buffer[0] = NEWCAMD_MSG_KEEPALIVE;
            buffer[1] = 0;
            buffer[2] = 0;
            mod->payload_size = 0;

            asc_socket_set_on_ready(mod->sock, on_newcamd_ready);
            return;
        }

        if(!mod->packet || msg_type < 0x80 || msg_type > 0x8F)
        {
            asc_log_warning(MSG("unknown packet type [0x%02X]"), msg_type);
            return;
        }

        asc_timer_destroy(mod->timeout);
        mod->timeout = NULL;

        asc_list_for(mod->__cam.decrypt_list)
        {
            if(asc_list_data(mod->__cam.decrypt_list) == mod->packet->decrypt)
                break;
        }
        if(asc_list_eol(mod->__cam.decrypt_list))
        {
            /* the decrypt module was detached */
            free(mod->packet);
            mod->packet = module_cam_queue_pop(&mod->__cam);
            if(mod->packet)
                asc_socket_set_on_ready(mod->sock, on_newcamd_ready);
            return;
        }

        if(mod->payload_size == 16)
        {
            // NDS
            csa_key_t key_0, key_1;
            memcpy(key_0.a, &buffer[3], 8);
            memcpy(key_1.a, &buffer[11], 8);
            if(key_0.l == 0)
            {
                key_0.l = mod->last_key[0].l;
                mod->last_key[1].l = key_1.l;
            }
            else if(key_1.l == 0)
            {
                key_1.l = mod->last_key[1].l;
                mod->last_key[0].l = key_0.l;
            }

            memcpy(mod->packet->buffer, buffer, 16 + 3);
            mod->packet->buffer_size = 16 + 3;
        }
        else if(mod->payload_size == 0)
        {
            memcpy(mod->packet->buffer, buffer, 3);
            mod->packet->buffer_size = 3;
        }
        else
        {
            mod->packet->buffer[2] = 0x00;
            mod->packet->buffer[3] = 0x00;
            mod->packet->buffer_size = 3;
        }

        on_cam_response(mod->packet->decrypt->self, mod->packet->arg, mod->packet->buffer);
        free(mod->packet);

        mod->packet = module_cam_queue_pop(&mod->__cam);
        if(mod->packet)
            asc_socket_set_on_ready(mod->sock, on_newcamd_ready);
    }
    else if(mod->status == 1)
    {
        if(msg_type != NEWCAMD_MSG_CLIENT_2_SERVER_LOGIN_ACK)
        {
            asc_log_error(MSG("login failed [0x%02X]"), msg_type);
            newcamd_reconnect(mod, true);
            return;
        }

        mod->status = 2;

        const size_t p_len = 35; /* strlen(mod->config.pass) */
        triple_des_set_key(mod, (uint8_t *)mod->config.pass, p_len - 1);

        buffer[0] = NEWCAMD_MSG_CARD_DATA_REQ;
        buffer[1] = 0;
        buffer[2] = 0;
        mod->payload_size = 0;

        asc_socket_set_on_ready(mod->sock, on_newcamd_ready);
    }
    else if(mod->status == 2)
    {
        if(msg_type != NEWCAMD_MSG_CARD_DATA)
        {
            asc_log_error(MSG("NEWCAMD_MSG_CARD_DATA"));
            newcamd_reconnect(mod, true);
            return;
        }

        mod->status = 3;

        mod->__cam.caid = (buffer[4] << 8) | buffer[5];
        memcpy(mod->__cam.ua, &buffer[6], 8);

        char hex_str[32];
        asc_log_info(  MSG("CaID=0x%04X AU=%s UA=%s")
                     , mod->__cam.caid
                     , (buffer[3] == 1) ? "YES" : "NO"
                     , hex_to_str(hex_str, mod->__cam.ua, 8));

        mod->__cam.disable_emm = (mod->config.disable_emm) ? (true) : (buffer[3] != 1);

        const int prov_count = (buffer[14] <= MAX_PROV_COUNT) ? buffer[14] : MAX_PROV_COUNT;

        static const int info_size = 3 + 8; /* ident + sa */
        if(mod->prov_buffer)
            free(mod->prov_buffer);
        mod->prov_buffer = malloc(prov_count * info_size);

        for(int i = 0; i < prov_count; i++)
        {
            uint8_t *p = &mod->prov_buffer[i * info_size];
            memcpy(&p[0], &buffer[15 + (11 * i)], 3);
            memcpy(&p[3], &buffer[18 + (11 * i)], 8);
            asc_list_insert_tail(mod->__cam.prov_list, p);
            asc_log_info(  MSG("Prov:%d ID:%s SA:%s"), i
                         , hex_to_str(hex_str, &p[0], 3)
                         , hex_to_str(&hex_str[8], &p[3], 8));
        }

        asc_timer_destroy(mod->timeout);
        mod->timeout = NULL;

        module_cam_ready(&mod->__cam);
    }
}

static void on_newcamd_read_init(void *arg)
{
    module_data_t *mod = arg;

    const ssize_t len = asc_socket_recv(  mod->sock
                                        , &mod->buffer[mod->buffer_skip]
                                        , KEY_SIZE - mod->buffer_skip);
    if(len <= 0)
    {
        asc_log_error(MSG("failed to read initial response"));
        newcamd_reconnect(mod, true);
        return;
    }
    mod->buffer_skip += len;
    if(mod->buffer_skip != KEY_SIZE)
        return;

    triple_des_set_key(mod, mod->buffer, KEY_SIZE);

    uint8_t *buffer = &mod->buffer[NEWCAMD_HEADER_SIZE];

    buffer[0] = NEWCAMD_MSG_CLIENT_2_SERVER_LOGIN;
    const size_t u_len = strlen(mod->config.user) + 1;
    memcpy(&buffer[3], mod->config.user, u_len);
    const size_t p_len = 35; /* strlen(mod->config.pass) */
    memcpy(&buffer[3 + u_len], mod->config.pass, p_len);

    mod->payload_size = u_len + p_len;

    asc_socket_set_on_read(mod->sock, on_newcamd_read_packet);
    asc_socket_set_on_ready(mod->sock, on_newcamd_ready);
}

/*
 *   oooooooo8   ooooooo  oooo   oooo oooo   oooo ooooooooooo  oooooooo8 ooooooooooo
 * o888     88 o888   888o 8888o  88   8888o  88   888    88 o888     88 88  888  88
 * 888         888     888 88 888o88   88 888o88   888ooo8   888             888
 * 888o     oo 888o   o888 88   8888   88   8888   888    oo 888o     oo     888
 *  888oooo88    88ooo88  o88o    88  o88o    88  o888ooo8888 888oooo88     o888o
 *
 */

static void on_newcamd_connect(void *arg)
{
    module_data_t *mod = arg;

    mod->status = 1;

    asc_timer_destroy(mod->timeout);
    mod->timeout = asc_timer_init(mod->config.timeout, on_timeout, mod);

    mod->buffer_skip = 0;

    asc_socket_set_on_read(mod->sock, on_newcamd_read_init);
}

static void newcamd_connect(module_data_t *mod)
{
    if(mod->sock)
        return;

    mod->status = 0;
    mod->payload_size = 0;
    mod->buffer_skip = 0;

    mod->sock = asc_socket_open_tcp4(mod);
    asc_socket_connect(  mod->sock
                       , mod->config.host, mod->config.port
                       , on_newcamd_connect, on_newcamd_close);

    mod->timeout = asc_timer_init(mod->config.timeout, on_timeout, mod);
}

static void newcamd_disconnect(module_data_t *mod)
{
    mod->status = -1;
    on_newcamd_close(mod);
}

static void newcamd_reconnect(module_data_t *mod, bool timeout)
{
    mod->status = -1;
    on_newcamd_close(mod);

    if(timeout)
        mod->timeout = asc_timer_init(mod->config.timeout, on_timeout, mod);
    else
        newcamd_connect(mod);
}

void newcamd_send_em(  module_data_t *mod
                     , module_decrypt_t *decrypt, void *arg
                     , const uint8_t *buffer, uint16_t size)
{
    if(mod->status != 3)
        return;

    const size_t packet_size = NEWCAMD_HEADER_SIZE + size;
    const uint8_t no_pad_bytes = (8 - ((packet_size - 1) % 8)) % 8;
    if(packet_size + no_pad_bytes > NEWCAMD_MSG_SIZE)
    {
        asc_log_error(  MSG("wrong packet size (pnr:%d drop:0x%02X size:%d")
                      , decrypt->pnr, buffer[0], size);
        return;
    }

    em_packet_t *packet = malloc(sizeof(em_packet_t));
    memcpy(packet->buffer, buffer, size);
    packet->buffer_size = size;
    packet->decrypt = decrypt;
    packet->arg = arg;

    if(packet->buffer[0] == 0x80 || packet->buffer[0] == 0x81)
    {
        asc_list_for(mod->__cam.packet_queue)
        {
            em_packet_t *queue_item = asc_list_data(mod->__cam.packet_queue);
            if(   queue_item->decrypt == decrypt
               && queue_item->arg == arg
               && (queue_item->buffer[0] == 0x80 || queue_item->buffer[0] == 0x81))
            {
                asc_log_warning(  MSG("drop old packet (pnr:%d drop:0x%02X set:0x%02X)")
                                , decrypt->pnr, queue_item->buffer[0], packet->buffer[0]);
                asc_list_remove_current(mod->__cam.packet_queue);
                free(queue_item);
                break;
            }
        }
    }

    if(mod->packet)
    {
        // newcamd is busy
        asc_list_insert_tail(mod->__cam.packet_queue, packet);
        return;
    }

    mod->packet = packet;
    asc_socket_set_on_ready(mod->sock, on_newcamd_ready);
}

static void module_init(module_data_t *mod)
{
    module_option_string("name", &mod->config.name, NULL);
    asc_assert(mod->config.name != NULL, "[newcamd] option 'name' is required");

    module_option_string("host", &mod->config.host, NULL);
    asc_assert(mod->config.host != NULL, MSG("option 'host' is required"));
    module_option_number("port", &mod->config.port);
    asc_assert(mod->config.port != 0, MSG("option 'port' is required"));

    module_option_string("user", &mod->config.user, NULL);
    asc_assert(mod->config.user != NULL, MSG("option 'user' is required"));

    const char *pass = NULL;
    module_option_string("pass", &pass, NULL);
    asc_assert(pass != NULL, MSG("option 'pass' is required"));
    md5_crypt(pass, "$1$abcdefgh$", mod->config.pass);

    const char *key = "0102030405060708091011121314";
    size_t key_size = 28;
    module_option_string("key", &key, &key_size);
    asc_assert(key_size == 28, MSG("option 'key' must be 28 chars length"));
    str_to_hex(key, mod->config.key, sizeof(mod->config.key));

    module_option_boolean("disable_emm", &mod->config.disable_emm);

    module_option_number("timeout", &mod->config.timeout);
    if(!mod->config.timeout)
        mod->config.timeout = 8;
    mod->config.timeout *= 1000;

    module_cam_init(mod, newcamd_connect, newcamd_disconnect, newcamd_send_em);
}

static void module_destroy(module_data_t *mod)
{
    mod->status = -1;
    on_newcamd_close(mod);

    module_cam_destroy(mod);
}

MODULE_CAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_CAM_METHODS_REF()
};
MODULE_LUA_REGISTER(newcamd)
