/*
 * Astra Module: HTTP
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

#include "parser.h"

/* RFC: 2616 (HTTP/1.1), 3986 (URI) */

#define IS_UPPER_ALPHA(_c) (_c >= 'A' && _c <= 'Z')
#define IS_LOWER_ALPHA(_c) (_c >= 'a' && _c <= 'z')
#define IS_ALPHA(_c) (IS_UPPER_ALPHA(_c) || IS_LOWER_ALPHA(_c))

#define IS_DIGIT(_c) (_c >= '0' && _c <= '9')

#define IS_UNRESERVED(_c) (   IS_ALPHA(_c)                                  \
                           || IS_DIGIT(_c)                                  \
                           || (_c == '-')                                   \
                           || (_c == '.')                                   \
                           || (_c == '_')                                   \
                           || (_c == '~'))

#define IS_HEXDIGIT(_c) (   IS_DIGIT(_c)                                    \
                         || (_c >= 'A' && _c <= 'F')                        \
                         || (_c >= 'a' && _c <= 'f'))

/*
 * oooooooooo  ooooooooooo  oooooooo8 oooooooooo
 *  888    888  888    88  888         888    888
 *  888oooo88   888ooo8     888oooooo  888oooo88
 *  888  88o    888    oo          888 888         ooo
 * o888o  88o8 o888ooo8888 o88oooo888 o888o        888
 *
 */

bool http_parse_response(const char *str, parse_match_t *match)
{
    char c;
    size_t skip = 0;
    match[0].so = 0;

    // parse version
    match[1].so = skip;
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            break;
        else
            ++skip;
    }
    match[1].eo = skip;

    // skip space
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            ++skip;
        else
            break;
    }

    // parse code
    match[2].so = skip;
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            break;
        else
            ++skip;
    }
    match[2].eo = skip;

    // skip space
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            ++skip;
        else
            break;
    }

    // parse message
    match[3].so = skip;
    while(1)
    {
        if(skip + 1 >= match[0].eo)
            return false;

        if(str[skip] == '\r' && str[skip + 1] == '\n')
            break;
        else
            ++skip;
    }
    match[3].eo = skip;

    match[0].eo = skip + 2;
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

bool http_parse_request(const char *str, parse_match_t *match)
{
    char c;
    size_t skip = 0;
    match[0].so = 0;

    // parse method
    match[1].so = 0;
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            break;
        else
            ++skip;
    }
    match[1].eo = skip;

    // skip space
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            ++skip;
        else
            break;
    }

    // parse path
    match[2].so = skip;
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            break;
        else
            ++skip;
    }
    match[2].eo = skip;

    // skip space
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            ++skip;
        else
            break;
    }

    // parse version
    match[3].so = skip;
    while(1)
    {
        if(skip + 1 >= match[0].eo)
            return false;

        if(str[skip] == '\r' && str[skip + 1] == '\n')
            break;
        else
            ++skip;
    }
    match[3].eo = skip;

    match[0].eo = skip + 2;
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

bool http_parse_header(const char *str, parse_match_t *match)
{
    char c;
    size_t skip = 0;
    match[0].so = 0;

    if(match[0].eo < 2)
        return false;

    // empty line
    if(str[0] == '\r' && str[1] == '\n')
    {
        match[1].so = 0;
        match[1].eo = 0;
        match[0].eo = 2;
        return true;
    }

    // parse key
    match[1].so = 0;
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        if(str[skip] == ':')
            break;
        else
            ++skip;
    }
    match[1].eo = skip;

    // skip ':'
    ++skip;

    // skip space
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            ++skip;
        else
            break;
    }

    // check eol
    if(skip + 2 >= match[0].eo)
        return false;

    if(str[skip] == '\r' && str[skip + 1] == '\n')
    {
        match[2].so = skip;
        match[2].eo = skip;
        match[0].eo = skip + 2;
        return true;
    }

    // parse value
    match[2].so = skip;
    while(1)
    {
        if(skip + 1 >= match[0].eo)
            return false;

        if(str[skip] == '\r' && str[skip + 1] == '\n')
            break;
        else
            ++skip;
    }
    match[2].eo = skip;

    match[0].eo = skip + 2;
    return true;
}

/* WWW-Authenticate */

bool http_parse_auth_challenge(const char *str, parse_match_t *match)
{
    char c;
    size_t skip = 0;
    match[0].so = 0;

    // parse key
    match[1].so = 0;
    while(1)
    {
        c = str[skip];

        if(IS_UNRESERVED(c))
        {
            skip += 1;
        }
        else
        {
            match[1].eo = skip;

            if(skip == 0)
            {
                // end of line
                match[2].so = skip;
                match[2].eo = skip;
                match[0].eo = skip;
                return true;
            }

            break;
        }
    }

    if(str[skip] != '=')
        return false;

    ++skip; // skip '='

    char quote = str[skip];
    if(quote == '"')
        ++skip;
    else
        quote = 0;

    match[2].so = skip;
    while(1)
    {
        c = str[skip];

        if(!c)
        {
            return false;
        }
        else if(c == '\\' && quote && str[skip + 1] == quote)
        {
            skip += 2;
        }
        else if(c == quote)
        {
            match[2].eo = skip;
            skip += 1;
            break;
        }
        else if(c == '\r' && str[skip + 1] == '\n' && !quote)
        {
            skip += 2;
            break;
        }
        else if(c == ',' && !quote)
        {
            match[2].eo = skip;
            break;
        }
        else
        {
            skip += 1;
        }
    }

    if(str[skip] == ',' && str[skip + 1] == ' ')
        skip += 2;

    match[0].eo = skip;
    return true;
}

char * http_authorization(  const char *auth_header
                          , const char *method, const char *path
                          , const char *login, const char *password)
{
    if(!login)
        return NULL;
    if(!password)
        password = "";

    if(!strncasecmp(auth_header, "basic", 5))
    {
        size_t sl = strlen(login) + 1 + strlen(password);
        char *s = malloc(sl + 1);
        sprintf(s, "%s:%s", login, password);
        size_t tl = 0;
        char *t = base64_encode(s, sl, &tl);
        char *r = malloc(6 + tl + 1);
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

        auth_header += 7; // "Digest "
        while(http_parse_auth_challenge(auth_header, m) && m[1].eo != 0)
        {
            if(!strncmp(auth_header, "realm", m[1].eo))
            {
                realm_len = m[2].eo - m[2].so;
                realm = strndup(&auth_header[m[2].so], realm_len);
            }
            else if(!strncmp(auth_header, "nonce", m[1].eo))
            {
                nonce_len = m[2].eo - m[2].so;
                nonce = strndup(&auth_header[m[2].so], nonce_len);
            }
            auth_header += m[0].eo;
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

        const char template[] = "Digest "
                                "username=\"%s\", "
                                "realm=\"%s\", "
                                "nonce=\"%s\", "
                                "uri=\"%s\", "
                                "response=\"%s\"";
        const size_t template_len = sizeof(template) - (2 * 5) - 1;

        char *r = malloc(  template_len
                         + login_len
                         + realm_len
                         + nonce_len
                         + path_len
                         + MD5_DIGEST_SIZE * 2
                         + 1);

        sprintf(r, template, login, realm, nonce, path, ha3);
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

bool http_parse_chunk(const char *str, parse_match_t *match)
{
    char c;
    size_t skip = 0;
    match[0].so = 0;

    // parse chunk
    match[1].so = 0;
    while(1)
    {
        if(skip + 1 >= match[0].eo)
            return false;

        c = str[skip];

        if((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
            ++skip;
        else if(c == '\r' && str[skip + 1] == '\n')
        {
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
    while(1)
    {
        if(skip + 1 >= match[0].eo)
            return false;

        if(str[skip] == '\r' && str[skip + 1] == '\n')
            break;
        else
            ++skip;
    }
    match[0].eo = skip + 2;

    return true;
}

/*
 *   ooooooo  ooooo  oooo ooooooooooo oooooooooo ooooo  oooo
 * o888   888o 888    88   888    88   888    888  888  88
 * 888     888 888    88   888ooo8     888oooo88     888
 * 888o  8o888 888    88   888    oo   888  88o      888
 *   88ooo88    888oo88   o888ooo8888 o888o  88o8   o888o
 *        88o8
 */

bool http_parse_query(const char *str, parse_match_t *match)
{
    char c;
    size_t skip = 0;
    match[0].so = 0;

    // parse key
    match[1].so = 0;
    while(1)
    {
        c = str[skip];

        if(IS_UNRESERVED(c) || c == '+')
        {
            skip += 1;
        }
        else if(c == '%')
        {
            const char d1 = str[skip + 1];
            if(!IS_HEXDIGIT(d1))
                return false;

            const char d2 = str[skip + 2];
            if(!IS_HEXDIGIT(d2))
                return false;

            skip += 3;
        }
        else
        {
            match[1].eo = skip;

            if(skip == 0)
            {
                match[2].so = skip;
                match[2].eo = skip;
                match[0].eo = skip;
                return true;
            }

            break;
        }
    }

    if(str[skip] != '=')
    {
        match[2].so = skip;
        match[2].eo = skip;
        match[0].eo = skip;
        return true;
    }

    ++skip; // skip '='

    match[2].so = skip;
    while(1)
    {
        c = str[skip];

        if(IS_UNRESERVED(c) || c == '+' || c == '=')
        {
            skip += 1;
        }
        else if(c == '%')
        {
            const char d1 = str[skip + 1];
            if(!IS_HEXDIGIT(d1))
                return false;

            const char d2 = str[skip + 2];
            if(!IS_HEXDIGIT(d2))
                return false;

            skip += 3;
        }
        else
        {
            match[2].eo = skip;
            break;
        }
    }

    match[0].eo = skip;
    return true;
}
