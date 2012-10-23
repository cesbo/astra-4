/*
 * Astra SoftCAM module
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
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
 *
 * For more information, visit http://cesbo.com
 */

#include "../softcam.h"
#include <modules/utils/utils.h>

#include <openssl/des.h>
#include <openssl/md5.h>

#define LOG_MSG(_msg) "[newcamd %s] " _msg, mod->config.name

#define NEWCAMD_TIMEOUT (5 * 1000)

#define NEWCAMD_HEADER_SIZE 12
#define NEWCAMD_MSG_SIZE 400
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

struct module_data_s
{
    CAM_MODULE_BASE();

    struct
    {
        char *name;
        char *host;
        int port;
        char *user;
        char *pass;
        char *key;
        char *cas_data;
    } config;

    char _name[32];
    char password_md5[36];

    int sock;
    newcamd_status_t status;
    void *timeout_timer;

    uint8_t *prov_buffer;

    struct
    {
        uint8_t key[16];
        DES_key_schedule ks1;
        DES_key_schedule ks2;
    } triple_des;

    uint16_t msg_id; // last message id

    uint64_t last_key[2]; // NDS

    size_t buffer_size;
    uint8_t buffer[NEWCAMD_MSG_SIZE];
};

// to get data from cas_data_t
struct cas_data_s
{
    CAS_MODULE_BASE();
};

typedef enum {
    NEWCAMD_MSG_ERROR = 0,
    NEWCAMD_MSG_FIRST = 0xDF,
    NEWCAMD_MSG_CLIENT_2_SERVER_LOGIN,
    NEWCAMD_MSG_CLIENT_2_SERVER_LOGIN_ACK,
    NEWCAMD_MSG_CLIENT_2_SERVER_LOGIN_NAK,
    NEWCAMD_MSG_CARD_DATA_REQ,
    NEWCAMD_MSG_CARD_DATA,
} newcamd_cmd_t;

/* module code */

static void triple_des_set_key(module_data_t *mod
                               , uint8_t *key
                               , size_t key_size)
{
    uint8_t tmp_key[14];
    // parse des key
    str_to_hex(mod->config.key, tmp_key, sizeof(tmp_key));

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

static void md5_to64(char *s, uint64_t v, int n)
{
    static const char md5_itoa64[] = /* 0 ... 63 => ascii - 64 */
        "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    while(--n >= 0)
    {
        *s++ = md5_itoa64[v&0x3f];
        v >>= 6;
    }
}

// From FreeBSD
static void md5_crypt(const char *pw, const char *salt, char *passwd)
{
    const char *ep;
    const char *sp = salt;

    // If it starts with the magic string, then skip that
    static const char *md5_magic = "$1$";
    const size_t md5_magic_len = 3;
    if(!strncmp(sp, md5_magic, md5_magic_len))
        sp += md5_magic_len;

    for(ep = sp; *ep && *ep != '$' && ep < (sp + 8); ep++)
        continue;

    // get the length of the true salt
    const int sl = (int)(ep - sp);

    MD5_CTX ctx;
    MD5_Init(&ctx);

    // The password first, since that is what is most unknown
    const size_t pw_len = strlen(pw);
    MD5_Update(&ctx, pw, pw_len);
    // Then our magic string
    MD5_Update(&ctx, md5_magic, md5_magic_len);
    // Then the raw salt
    MD5_Update(&ctx, sp, sl);
    // Then just as many characters of the MD5(pw,salt,pw)
    MD5_CTX ctx1;
    MD5_Init(&ctx1);
    MD5_Update(&ctx1, pw, pw_len);
    MD5_Update(&ctx1, sp, sl);
    MD5_Update(&ctx1, pw, pw_len);
    uint8_t final[17];
    MD5_Final(final, &ctx1);
    for(int i = (int)pw_len; i > 0; i -= 16)
        MD5_Update(&ctx, (const uint8_t *)final, (i > 16) ? 16 : i);

    memset(final, 0, sizeof(final));
    for(int i = (int)pw_len; i; i >>= 1)
    {
        const uint8_t *d = (i & 1) ? final : (uint8_t *)pw;
        MD5_Update(&ctx, d, 1);
    }

    // Now make the output string
    strcpy(passwd, md5_magic);
    strncat(passwd, sp, sl);
    strcat(passwd, "$");

    MD5_Final(final, &ctx);

    for(int i = 0; i < 1000; i++)
    {
        MD5_Init(&ctx1);

        if(i & 1)
            MD5_Update(&ctx1, pw, pw_len);
        else
            MD5_Update(&ctx1, final, 16);

        if(i % 3)
            MD5_Update(&ctx1, sp, sl);

        if(i % 7)
            MD5_Update(&ctx1, pw, pw_len);

        if(i & 1)
            MD5_Update(&ctx1, final, 16);
        else
            MD5_Update(&ctx1, pw, pw_len);

        MD5_Final(final, &ctx1);
    }

    uint64_t l;
    char *p = passwd + strlen(passwd);
    final[16] = final[5];
    for(int i = 0; i < 5; i++)
    {
        l = (final[i] << 16) | (final[i + 6] << 8) | final[i + 12];
        md5_to64(p, l, 4); p += 4;
    }
    l = final[11];
    md5_to64(p, l, 2); p += 2;
    *p = '\0';

    memset(final, 0, sizeof(final));
} /* md5_crypt */

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

/* timeout */

static void newcamd_drop_packet(module_data_t *mod)
{
    cam_packet_t *packet = NULL;
    list_t *i = mod->__cam_module.queue.head;
    while(i)
    {
        packet = list_get_data(i);
        if(packet->id == mod->msg_id)
            break;
        i = list_get_next(i);
    }
    if(!i)
    {
        log_error(LOG_MSG("please report to and@cesbo.com bug:235620102012"));
        return;
    }

    packet->type = MPEGTS_PACKET_UNKNOWN; // drop packet in cam_callback
    packet->keys[0] = packet->payload[0];
    packet->keys[1] = 0x00;
    packet->keys[2] = 0x00;

    cam_callback(mod, i);
}

static void newcamd_connect(void *);
static void newcamd_disconnect(module_data_t *);

static void timeout_timer_callback(void *arg)
{
    module_data_t *mod = arg;

    timer_detach(mod->timeout_timer);
    mod->timeout_timer = NULL;

    switch(mod->status)
    {
        case NEWCAMD_UNKNOWN:
        case NEWCAMD_STARTED:
            log_error(LOG_MSG("connection timeout. try again"));
            break;
        case NEWCAMD_READY:
            log_warning(LOG_MSG("receiving timeout. drop packet"));
            newcamd_drop_packet(mod);
            return;
        default:
            log_error(LOG_MSG("receiving timeout. reconnect"));
            break;
    }

    newcamd_disconnect(mod);
    newcamd_connect(mod);
}

static void newcamd_timeout_set(module_data_t *mod)
{
    if(mod->timeout_timer)
        return;
    mod->timeout_timer
        = timer_attach(NEWCAMD_TIMEOUT, timeout_timer_callback, mod);
}

static void newcamd_timeout_unset(module_data_t *mod)
{
    if(mod->timeout_timer)
    {
        timer_detach(mod->timeout_timer);
        mod->timeout_timer = NULL;
    }
}

/* send/recv */

static int newcamd_send_msg(module_data_t *mod, list_t *q_item)
{
    // packet header
    const size_t buffer_size = mod->buffer_size;
    uint8_t *buffer = mod->buffer;

    memset(&buffer[2], 0, NEWCAMD_HEADER_SIZE - 2);
    if(q_item)
    {
        cam_packet_t *packet = list_get_data(q_item);
        const uint16_t pnr = cas_pnr(packet->cas);
        buffer[4] = pnr >> 8;
        buffer[5] = pnr & 0xff;

        ++mod->msg_id;
        buffer[2] = mod->msg_id >> 8;
        buffer[3] = mod->msg_id & 0xff;

        packet->id = mod->msg_id;
    }

    // packet content
    buffer[13] = ((buffer[13] & 0xf0) | (((buffer_size - 3) >> 8) & 0x0f));
    buffer[14] = (buffer_size - 3) & 0xff;

    uint16_t packet_size = NEWCAMD_HEADER_SIZE + buffer_size;
    // pad_message
    const uint8_t no_pad_bytes= (8 - ((packet_size - 1) % 8)) % 8;
    if((packet_size + no_pad_bytes + 1) >= (NEWCAMD_MSG_SIZE - 8))
    {
        log_error(LOG_MSG("send: failed to pad message"));
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
        log_error(LOG_MSG("send: failed to encrypt"));
        return 0;
    }
    memcpy(&buffer[packet_size], ivec, sizeof(ivec));
    DES_ede2_cbc_encrypt(&buffer[2], &buffer[2], packet_size - 2
                         , &mod->triple_des.ks1, &mod->triple_des.ks2
                         , (DES_cblock *)ivec, DES_ENCRYPT);
    packet_size += sizeof(ivec);

    buffer[0] = (packet_size - 2) >> 8;
    buffer[1] = (packet_size - 2) & 0xff;

    if(socket_send(mod->sock, buffer, packet_size) != packet_size)
    {
        log_error(LOG_MSG("send: failed [%d]"), errno);
        if(q_item)
        {
            // TODO: set the packet error flag
            cam_callback(mod, q_item);
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

int newcamd_recv_msg(module_data_t *mod, list_t **q_item)
{
    if(mod->status != NEWCAMD_READY)
        newcamd_timeout_unset(mod);

    uint8_t *buffer = mod->buffer;
    buffer[NEWCAMD_HEADER_SIZE] = 0x00;

    uint16_t packet_size = 0;

    if(socket_recv(mod->sock, buffer, 2) != 2)
    {
        log_error(LOG_MSG("recv: failed to read the length of message [%s]")
                  , strerror(errno));
        return 0;
    }
    packet_size = (buffer[0] << 8) | buffer[1];
    if(packet_size > (NEWCAMD_MSG_SIZE - 2))
    {
        log_error(LOG_MSG("recv: message size %d is greater than %d")
                  , packet_size, NEWCAMD_MSG_SIZE);
        return 0;
    }
    const ssize_t read_size = socket_recv(mod->sock, &buffer[2], packet_size);
    if(read_size != packet_size)
    {
        log_error(LOG_MSG("recv: failed to read message [%s]")
                  , strerror(errno));
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
        log_error(LOG_MSG("recv: bad checksum"), NULL);
        return 0;
    }

    const uint8_t msg_type = buffer[NEWCAMD_HEADER_SIZE];
    if(q_item && msg_type >= 0x80 && msg_type <= 0x8F)
    {
        const uint16_t msg_id = (buffer[2] << 8) | buffer[3];

        cam_packet_t *p = NULL;
        list_t *i = mod->__cam_module.queue.head;
        while(i)
        {
            p = list_get_data(i);
            if(p->id == msg_id)
                break;
            i = list_get_next(i);
        }
        if(!i)
        {
            log_warning(LOG_MSG("packet with id %d is not found [type:0x%02X]")
                        , msg_id, msg_type);
        }
        else
            newcamd_timeout_unset(mod);
        *q_item = i;
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

/* login */

static int newcamd_login_1(module_data_t *mod)
{
    newcamd_timeout_unset(mod); // connection timeout

    mod->msg_id = 0;
    uint8_t rnd_data[14];
    const ssize_t len = socket_recv(mod->sock, rnd_data, sizeof(rnd_data));
    if(len != sizeof(rnd_data))
    {
        log_debug(LOG_MSG("%s(): len=%d [%s]"), __FUNCTION__, len
                  , strerror(errno));
        return 0;
    }
    triple_des_set_key(mod, rnd_data, sizeof(rnd_data));

    uint8_t *buffer = &mod->buffer[NEWCAMD_HEADER_SIZE];

    const size_t u_len = strlen(mod->config.user) + 1;
    memcpy(&buffer[3], mod->config.user, u_len);
    md5_crypt(mod->config.pass, "$1$abcdefgh$", mod->password_md5);
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
        log_error(LOG_MSG("login failed [0x%02X]"), buffer[0]);
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
        log_error(LOG_MSG("NEWCAMD_MSG_CARD_DATA"));
        return 0;
    }

    mod->__cam_module.caid = (buffer[4] << 8) | buffer[5];
    memcpy(mod->__cam_module.ua, &buffer[6], 8);

    char hex_str[32];
    log_info(LOG_MSG("CaID=0x%04X admin=%s UA=%s")
             , mod->__cam_module.caid
             , (buffer[3] == 1) ? "YES" : "NO"
             , hex_to_str(hex_str, mod->__cam_module.ua, 8));

    if(buffer[3] != 1) // disable emm if not admin
        mod->__cam_module.disable_emm = 1;

    const int prov_count = (buffer[14] <= MAX_PROV_COUNT)
                         ? buffer[14]
                         : MAX_PROV_COUNT;

    static const int info_size = 3 + 8; /* ident + sa */
    mod->prov_buffer = malloc(prov_count * info_size);

    for(int i = 0; i < prov_count; i++)
    {
        uint8_t *p = &mod->prov_buffer[i * info_size];
        memcpy(&p[0], &buffer[15 + (11 * i)], 3);
        memcpy(&p[3], &buffer[18 + (11 * i)], 8);
        mod->__cam_module.prov_list
            = list_append(mod->__cam_module.prov_list, p);
        log_info(LOG_MSG("Prov:%d ID:%s SA:%s"), i
                  , hex_to_str(hex_str, &p[0], 3)
                  , hex_to_str(&hex_str[8], &p[3], 8));
    }

    mod->status = NEWCAMD_READY;
    decrypt_module_cam_status(mod, 1);
    return 1;
}

static int newcamd_process(module_data_t *mod)
{
    list_t *q_item = NULL;
    const int len = newcamd_recv_msg(mod, &q_item);
    if(len > 0 && q_item)
    {
        uint8_t *buffer = &mod->buffer[NEWCAMD_HEADER_SIZE];
        cam_packet_t *packet = list_get_data(q_item);
        const uint16_t pnr = cas_pnr(packet->cas);
        if(len == 19 && packet->type == MPEGTS_PACKET_ECM)
        {
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

            memcpy(packet->keys, buffer, 19);
        }
        else if(len >= 3)
        {
            memcpy(packet->keys, buffer, 3);
        }
        else
        {
            packet->keys[0] = packet->payload[0];
            packet->keys[2] = 0x00;
            log_error(LOG_MSG("%s PNR:%d incorrect message length [%d]")
                      , (packet->type == MPEGTS_PACKET_ECM) ? "ECM" : "EMM"
                      , pnr, len);
        }

        cam_callback(mod, q_item);
    }

    return len;
}

/* read/write callbacks */

static void newcamd_read_cb(void *arg, int event)
{
    module_data_t *mod = arg;
    if(event == EVENT_ERROR)
    {
        log_error(LOG_MSG("failed to read from socket [%s]"), strerror(errno));
        newcamd_disconnect(mod);
        newcamd_timeout_set(mod);
        return;
    }

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
            ret = newcamd_process(mod);
            break;
        case NEWCAMD_STOPPED:
            break;
        default: // logout
            ret = -1;
            break;
    }

    if(ret <= 0)
        newcamd_read_cb(arg, EVENT_ERROR);
}

static void newcamd_write_cb(void *arg, int event)
{
    module_data_t *mod = arg;
    if(event == EVENT_ERROR)
    {
        log_error(LOG_MSG("socket not ready [%s]"), strerror(errno));
        newcamd_disconnect(mod);
        newcamd_timeout_set(mod);
        return;
    }

    event_detach(mod->sock);
    mod->status = NEWCAMD_STARTED;
    event_attach(mod->sock, newcamd_read_cb, mod, EVENT_READ);
}

/* connect/disconnect */

static void newcamd_connect(void *arg)
{
    module_data_t *mod = arg;

    mod->sock = socket_open(SOCKET_PROTO_TCP | SOCKET_CONNECT
                            , mod->config.host, mod->config.port);
    newcamd_timeout_set(mod);

    if(mod->sock <= 0)
        return;

    event_attach(mod->sock, newcamd_write_cb, mod, EVENT_WRITE);
}

static void newcamd_disconnect(module_data_t *mod)
{
    newcamd_timeout_unset(mod);

    if(mod->sock)
    {
        event_detach(mod->sock);
        socket_close(mod->sock);
        mod->sock = 0;
    }
    mod->status = NEWCAMD_UNKNOWN;

    cam_queue_flush(mod);
    decrypt_module_cam_status(mod, 0);
}

/* config_check */

static int config_check_key(module_data_t *mod)
{
    if(strlen(mod->config.key) == 28)
        return 1;
    log_error(LOG_MSG("key length must be equal 28"));
    return 0;
}

static int config_check_cas_data(module_data_t *mod)
{
    cam_set_cas_data(mod, mod->config.cas_data);
    return 1;
}

/* softcam callbacks */

static void interface_send_em(module_data_t *mod)
{
    list_t *current = mod->__cam_module.queue.current;
    cam_packet_t *packet = list_get_data(current);
    uint8_t *buffer = &mod->buffer[NEWCAMD_HEADER_SIZE];
    memcpy(buffer, packet->payload, packet->size);
    mod->buffer_size = packet->size;
    newcamd_send_msg(mod, current);
}

/* required */

static void module_init(module_data_t *mod)
{
    if(!mod->config.name)
    {
        snprintf(mod->_name, sizeof(mod->_name)
                 , "%s:%d", mod->config.host, mod->config.port);
        mod->config.name = mod->_name;
    }

    log_debug(LOG_MSG("init"));

    CAM_INTERFACE();

    newcamd_connect(mod);
}

static void module_destroy(module_data_t *mod)
{
    log_debug(LOG_MSG("destroy"));

    newcamd_timeout_unset(mod);

    if(mod->sock)
    {
        event_detach(mod->sock);
        socket_close(mod->sock);
    }

    cam_queue_flush(mod);
    decrypt_module_cam_status(mod, -1);

    list_t *i = mod->__cam_module.prov_list;
    while(i)
        i = list_delete(i, NULL);
    mod->__cam_module.prov_list = NULL;

    if(mod->prov_buffer)
        free(mod->prov_buffer);
}

MODULE_OPTIONS()
{
    OPTION_STRING("name", config.name, NULL)
    OPTION_STRING("host", config.host, NULL)
    OPTION_NUMBER("port", config.port, NULL)
    OPTION_STRING("user", config.user, NULL)
    OPTION_STRING("pass", config.pass, NULL)
    OPTION_STRING("key", config.key, config_check_key)
    OPTION_NUMBER("disable_emm", __cam_module.disable_emm, NULL)
    OPTION_STRING("cas_data", config.cas_data, config_check_cas_data)
};

MODULE_METHODS_EMPTY();

MODULE(newcamd)
