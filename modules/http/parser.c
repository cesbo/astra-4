/*
 * Astra Module: HTTP
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2015, Andrey Dyldin <and@cesbo.com>
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

#include "parser.h"

bool parse_skip_word(const char *str, size_t size, size_t *skip)
{
    size_t _skip = *skip;

    while(_skip < size)
    {
        switch(str[_skip])
        {
            case ' ':
            case '\t':
                *skip = _skip;
                return true;
            default:
                ++_skip;
                break;
        }
    }

    return false;
}

bool parse_skip_space(const char *str, size_t size, size_t *skip)
{
    size_t _skip = *skip;

    while(_skip < size)
    {
        switch(str[_skip])
        {
            case ' ':
            case '\t':
                ++_skip;
                break;
            default:
                *skip = _skip;
                return true;
        }
    }

    return false;
}

bool parse_skip_line(const char *str, size_t size, size_t *skip)
{
    size_t _skip = *skip;

    while(_skip < size)
    {
        switch(str[_skip])
        {
            case '\n':
                *skip = _skip + 1;
                return true;
            case '\r':
                if(_skip + 1 >= size || str[_skip + 1] != '\n')
                    return false;
                *skip = _skip + 2;
                return true;
            default:
                ++_skip;
                break;
        }
    }

    return false;
}

/*
 * oooooooooo  ooooooooooo  oooooooo8 oooooooooo
 *  888    888  888    88  888         888    888
 *  888oooo88   888ooo8     888oooooo  888oooo88
 *  888  88o    888    oo          888 888         ooo
 * o888o  88o8 o888ooo8888 o88oooo888 o888o        888
 *
 */

bool http_parse_response(const char *str, size_t size, parse_match_t *match)
{
    size_t skip = 0;
    match[0].so = 0;

    // parse version
    match[1].so = skip;
    if(!parse_skip_word(str, size, &skip))
        return false;
    match[1].eo = skip;

    // skip space
    if(!parse_skip_space(str, size, &skip))
        return false;

    // parse code
    match[2].so = skip;
    if(!parse_skip_word(str, size, &skip))
        return false;
    match[2].eo = skip;

    // skip space
    if(!parse_skip_space(str, size, &skip))
        return false;

    // parse message
    match[3].so = skip;
    if(!parse_skip_line(str, size, &skip))
        return false;
    match[3].eo = parse_get_line_size(str, skip);

    match[0].eo = skip;
    return true;
}

/*
 * oooooooooo  ooooooooooo  ooooooo
 *  888    888  888    88 o888   888o
 *  888oooo88   888ooo8   888     888
 *  888  88o    888    oo 888o  8o888  ooo
 * o888o  88o8 o888ooo8888  88ooo88    888
 *                               88o8
 */

bool http_parse_request(const char *str, size_t size, parse_match_t *match)
{
    size_t skip = 0;
    match[0].so = 0;

    // parse method
    match[1].so = 0;
    if(!parse_skip_word(str, size, &skip))
        return false;
    match[1].eo = skip;

    // skip space
    if(!parse_skip_space(str, size, &skip))
        return false;

    // parse path
    match[2].so = skip;
    if(!parse_skip_word(str, size, &skip))
        return false;
    match[2].eo = skip;

    // skip space
    if(!parse_skip_space(str, size, &skip))
        return false;

    // parse version
    match[3].so = skip;
    if(!parse_skip_line(str, size, &skip))
        return false;
    match[3].eo = parse_get_line_size(str, skip);

    match[0].eo = skip;
    return true;
}

/*
 * ooooo ooooo ooooooooooo      o      ooooooooo  ooooooooooo oooooooooo
 *  888   888   888    88      888      888    88o 888    88   888    888
 *  888ooo888   888ooo8       8  88     888    888 888ooo8     888oooo88
 *  888   888   888    oo    8oooo88    888    888 888    oo   888  88o
 * o888o o888o o888ooo8888 o88o  o888o o888ooo88  o888ooo8888 o888o  88o8
 *
 */

bool http_parse_header(const char *str, size_t size, parse_match_t *match)
{
    size_t skip = 0;
    match[0].so = 0;
    match[1].so = 0;
    match[1].eo = 0;

    if(size == 0)
        return false;

    while(1)
    {
        if(skip >= size)
            return false;

        const char c = str[skip];
        if(c == ':')
            break;
        else if(c == '\n')
        {
            if(skip > 0)
                return false;

            // eol
            match[1].eo = 0;
            match[0].eo = skip + 1;
            return true;
        }
        else if(c == '\r')
        {
            if(skip > 0)
                return false;
            if(skip + 1 >= size || str[skip + 1] != '\n')
                return false;

            // eol
            match[1].eo = 0;
            match[0].eo = skip + 2;
            return true;
        }

        ++skip;
    }
    match[1].eo = skip;

    // parse value
    match[2].so = match[1].eo + 1; // skip ':'
    if(!parse_skip_space(str, size, &match[2].so))
        return false;

    skip = match[2].so;
    if(!parse_skip_line(str, size, &skip))
        return false;
    match[2].eo = parse_get_line_size(str, skip);

    match[0].eo = skip;
    return true;
}

/*
 *      o    ooooo  oooo ooooooooooo ooooo ooooo
 *     888    888    88  88  888  88  888   888
 *    8  88   888    88      888      888ooo888
 *   8oooo88  888    88      888      888   888
 * o88o  o888o 888oo88      o888o    o888o o888o
 *
 */

static bool http_parse_auth_challenge(const char *str, size_t size, parse_match_t *match)
{
    char c;
    size_t skip = 0;
    match[0].so = 0;

    // parse key
    match[1].so = 0;
    while(1)
    {
        if(skip >= size)
            return false;

        if(str[skip] == '=')
            break;
        else
            ++skip;
    }
    match[1].eo = skip;

    ++skip; // skip '='

    if(skip >= size)
        return false;

    bool quoted = false;
    if(str[skip] == '"')
    {
        quoted = true;
        ++skip;
    }

    match[2].so = skip;
    while(1)
    {
        if(skip >= size)
        {
            match[2].eo = skip;
            match[0].eo = skip;
            return true;
        }

        c = str[skip];
        if(c == '"' && quoted)
        {
            match[2].eo = skip;
            ++skip; // skip '"'
            break;
        }
        else if(c == '\\' && quoted)
        {
            ++skip;
            if(skip < size && str[skip] == '"')
                ++skip;
        }
        else if((c == ',' || c == ' ') && !quoted)
        {
            match[2].eo = skip;
            break;
        }
        else
            ++skip;
    }

    while(skip < size)
    {
        c = str[skip];
        if(c == ',' || c == ' ')
            ++skip;
        else
            break;
    }
    match[0].eo = skip;

    return true;
}

char * http_authorization(const char *auth_header, size_t size,
    const char *method, const char *path,
    const char *login, const char *password)
{
    if(!login)
        return NULL;
    if(!password)
        password = "";

    if(!strncasecmp(auth_header, "basic", 5))
    {
        size_t sl = strlen(login) + 1 + strlen(password);
        char *s = (char *)malloc(sl + 1);
        sprintf(s, "%s:%s", login, password);
        size_t tl = 0;
        char *t = base64_encode(s, sl, &tl);
        char *r = (char *)malloc(6 + tl + 1);
        sprintf(r, "Basic %s", t);
        free(s);
        free(t);
        return r;
    }
    else if(!strncasecmp(auth_header, "digest", 6))
    {
        parse_match_t m[4];
        md5_ctx_t ctx;
        uint8_t digest[MD5_DIGEST_SIZE];
        char ha1[MD5_DIGEST_SIZE * 2 + 1];
        char ha2[MD5_DIGEST_SIZE * 2 + 1];
        char ha3[MD5_DIGEST_SIZE * 2 + 1];

        const size_t login_len = strlen(login);
        const size_t password_len = strlen(password);

        char *realm = "";
        size_t realm_len = 0;
        char *nonce = "";
        size_t nonce_len = 0;

        size_t skip = 7;
        while(skip < size &&
            http_parse_auth_challenge(&auth_header[skip], size - skip, m))
        {
            if(!strncmp(&auth_header[skip], "realm", m[1].eo))
            {
                realm_len = m[2].eo - m[2].so;
                realm = (char *)malloc(realm_len + 1);
                memcpy(realm, &auth_header[skip + m[2].so], realm_len);
                realm[realm_len] = 0;
            }
            else if(!strncmp(&auth_header[skip], "nonce", m[1].eo))
            {
                nonce_len = m[2].eo - m[2].so;
                nonce = (char *)malloc(nonce_len + 1);
                memcpy(nonce, &auth_header[skip + m[2].so], nonce_len);
                nonce[nonce_len] = 0;
            }
            skip += m[0].eo;
        }

        memset(&ctx, 0, sizeof(md5_ctx_t));
        md5_init(&ctx);
        md5_update(&ctx, (uint8_t *)login, login_len);
        md5_update(&ctx, (uint8_t *)":", 1);
        md5_update(&ctx, (uint8_t *)realm, realm_len);
        md5_update(&ctx, (uint8_t *)":", 1);
        md5_update(&ctx, (uint8_t *)password, password_len);
        md5_final(&ctx, digest);
        hex_to_str(ha1, digest, MD5_DIGEST_SIZE);

        const size_t method_len = strlen(method);
        const size_t path_len = strlen(path);

        memset(&ctx, 0, sizeof(md5_ctx_t));
        md5_init(&ctx);
        md5_update(&ctx, (uint8_t *)method, method_len);
        md5_update(&ctx, (uint8_t *)":", 1);
        md5_update(&ctx, (uint8_t *)path, path_len);
        md5_final(&ctx, digest);
        hex_to_str(ha2, digest, MD5_DIGEST_SIZE);

        for(int i = 0; i < MD5_DIGEST_SIZE * 2; ++i)
        {
            const char c1 = ha1[i];
            if(c1 >= 'A' && c1 <= 'F')
                ha1[i] = c1 - 'A' + 'a';

            const char c2 = ha2[i];
            if(c2 >= 'A' && c2 <= 'F')
                ha2[i] = c2 - 'A' + 'a';
        }

        memset(&ctx, 0, sizeof(md5_ctx_t));
        md5_init(&ctx);
        md5_update(&ctx, (uint8_t *)ha1, MD5_DIGEST_SIZE * 2);
        md5_update(&ctx, (uint8_t *)":", 1);
        md5_update(&ctx, (uint8_t *)nonce, nonce_len);
        md5_update(&ctx, (uint8_t *)":", 1);
        md5_update(&ctx, (uint8_t *)ha2, MD5_DIGEST_SIZE * 2);
        md5_final(&ctx, digest);
        hex_to_str(ha3, digest, MD5_DIGEST_SIZE);

        for(int i = 0; i < MD5_DIGEST_SIZE * 2; ++i)
        {
            const char c3 = ha3[i];
            if(c3 >= 'A' && c3 <= 'F')
                ha3[i] = c3 - 'A' + 'a';
        }

        const char auth_template[] = "Digest "
                                     "username=\"%s\", "
                                     "realm=\"%s\", "
                                     "nonce=\"%s\", "
                                     "uri=\"%s\", "
                                     "response=\"%s\"";
        const size_t auth_template_len = sizeof(auth_template) - (2 * 5) - 1;

        char *r = (char *)malloc(auth_template_len +
            login_len + realm_len + nonce_len + path_len + MD5_DIGEST_SIZE * 2 + 1);

        sprintf(r, auth_template, login, realm, nonce, path, ha3);
        return r;
    }

    return NULL;
}

/*
 *   oooooooo8 ooooo ooooo ooooo  oooo oooo   oooo oooo   oooo
 * o888     88  888   888   888    88   8888o  88   888  o88
 * 888          888ooo888   888    88   88 888o88   888888
 * 888o     oo  888   888   888    88   88   8888   888  88o
 *  888oooo88  o888o o888o   888oo88   o88o    88  o888o o888o
 *
 */

bool http_parse_chunk(const char *str, size_t size, parse_match_t *match)
{
    char c;
    size_t skip = 0;
    match[0].so = 0;

    // parse chunk
    match[1].so = 0;
    while(1)
    {
        if(skip >= size)
            return false;

        c = str[skip];

        if((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
            ++skip;
        else if(c == '\n')
        {
            match[1].eo = skip;
            match[0].eo = skip + 1;
            return true;
        }
        else if(c == '\r')
        {
            if(skip + 1 >= size || str[skip + 1] != '\n')
                return false;
            match[1].eo = skip;
            match[0].eo = skip + 2;
            return true;
        }
        else if(c == ';')
            break;
        else
            return false;
    }

    // chunk extension
    ++skip; // skip ';'
    while(skip < size)
    {
        switch(str[skip])
        {
            case '\n':
                match[0].eo = skip + 1;
                return true;
            case '\r':
                if(skip + 1 >= size || str[skip + 1] != '\n')
                    return false;
                match[0].eo = skip + 2;
                return true;
            default:
                ++skip;
                break;
        }
    }

    return false;
}

/*
 *   ooooooo  ooooo  oooo ooooooooooo oooooooooo ooooo  oooo
 * o888   888o 888    88   888    88   888    888  888  88
 * 888     888 888    88   888ooo8     888oooo88     888
 * 888o  8o888 888    88   888    oo   888  88o      888
 *   88ooo88    888oo88   o888ooo8888 o888o  88o8   o888o
 *        88o8
 */

bool http_parse_query(const char *str, size_t size, parse_match_t *match)
{
    size_t skip = 0;
    match[0].so = 0;

    // parse key
    match[1].so = 0;
    while(skip < size)
    {
        const char c = str[skip];
        if(c == '=')
        {
            match[1].eo = skip;
            break;
        }
        else if(c == '&')
        {
            match[1].eo = skip;
            match[2].so = skip;
            match[2].eo = skip;
            match[0].eo = skip + 1;
            return true;
        }
        else
            ++skip;
    }

    if(skip == 0 || skip >= size)
    {
        match[1].eo = skip;
        match[2].so = skip;
        match[2].eo = skip;
        match[0].eo = skip;
        return true;
    }

    skip += 1; // skip '='

    // parse value
    match[2].so = skip;
    while(skip < size && str[skip] != '&')
        ++skip;
    match[2].eo = skip;
    match[0].eo = (skip < size && str[skip] == '&') ? skip + 1 : skip;

    return true;
}
