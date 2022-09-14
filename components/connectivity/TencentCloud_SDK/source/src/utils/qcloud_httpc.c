/*
 * Tencent is pleased to support the open source community by making IoT Hub available.
 * Copyright (C) 2016 THL A29 Limited, a Tencent company. All rights reserved.

 * Licensed under the MIT License (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://opensource.org/licenses/MIT

 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "qcloud.h"

#define HTTP_CLIENT_MIN(x,y) (((x)<(y))?(x):(y))
#define HTTP_CLIENT_MAX(x,y) (((x)>(y))?(x):(y))

#define HTTP_CLIENT_AUTHB_SIZE     128

#define HTTP_CLIENT_CHUNK_SIZE     1024
#define HTTP_CLIENT_SEND_BUF_SIZE  1024

#define HTTP_CLIENT_MAX_HOST_LEN   64
#define HTTP_CLIENT_MAX_URL_LEN    1024

#define HTTP_RETRIEVE_MORE_DATA   (1)

#if defined(MBEDTLS_DEBUG_C)
    #define DEBUG_LEVEL 2
#endif


static void _http_client_base64enc(char *out, const char *in)
{
    const char code[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
    int i = 0, x = 0, l = 0;

    for (; *in; in++) {
        x = x << 8 | *in;
        for (l += 8; l >= 6; l -= 6) {
            out[i++] = code[(x >> (l - 6)) & 0x3f];
        }
    }
    if (l > 0) {
        x <<= 6 - l;
        out[i++] = code[x & 0x3f];
    }
    for (; i % 4;) {
        out[i++] = '=';
    }
    out[i] = '\0';
}

static int _http_client_parse_url(const char *url, char *scheme, uint32_t max_scheme_len, char *host, uint32_t maxhost_len,
    int *port, char *path, uint32_t max_path_len)
{
    char *scheme_ptr = (char *) url;
    char *host_ptr = (char *) strstr(url, "://");
    uint32_t host_len = 0;
    uint32_t path_len;

    char *path_ptr;
    char *fragment_ptr;

    if (host_ptr == NULL) {
        QCLOUD_LOG_E("Could not find host");
        return QCLOUD_ERR_HTTP_PARSE;
    }

    if (max_scheme_len < host_ptr - scheme_ptr + 1) {
        QCLOUD_LOG_E("Scheme str is too small (%u >= %u)", max_scheme_len, (uint32_t)(host_ptr - scheme_ptr + 1));
        return QCLOUD_ERR_HTTP_PARSE;
    }
    memcpy(scheme, scheme_ptr, host_ptr - scheme_ptr);
    scheme[host_ptr - scheme_ptr] = '\0';

    host_ptr += 3;

    *port = 0;

    path_ptr = strchr(host_ptr, '/');
    if (NULL == path_ptr) {
        path_ptr = scheme_ptr + (int)strlen(url);
        host_len = path_ptr - host_ptr;
        memcpy(host, host_ptr, host_len);
        host[host_len] = '\0';

        memcpy(path, "/", 1);
        path[1] = '\0';

        return QCLOUD_ERR_SUCCESS;
    }

    if (host_len == 0) {
        host_len = path_ptr - host_ptr;
    }

    if (maxhost_len < host_len + 1) {
        QCLOUD_LOG_E("Host str is too long (host_len(%d) >= max_len(%d))", host_len + 1, maxhost_len);
        return QCLOUD_ERR_HTTP_PARSE;
    }
    memcpy(host, host_ptr, host_len);
    host[host_len] = '\0';

    fragment_ptr = strchr(host_ptr, '#');
    if (fragment_ptr != NULL) {
        path_len = fragment_ptr - path_ptr;
    } else {
        path_len = strlen(path_ptr);
    }

    if (max_path_len < path_len + 1) {
        QCLOUD_LOG_E("Path str is too small (%d >= %d)", max_path_len, path_len + 1);
        return QCLOUD_ERR_HTTP_PARSE;
    }

    memcpy(path, path_ptr, path_len);

    path[path_len] = '\0';

    return QCLOUD_ERR_SUCCESS;
}

static int _http_client_parse_host(const char *url, char *host, uint32_t host_max_len)
{
    const char *host_ptr = (const char *) strstr(url, "://");
    uint32_t host_len = 0;
    char *path_ptr;

    if (host_ptr == NULL) {
        QCLOUD_LOG_E("Could not find host");
        return QCLOUD_ERR_HTTP_PARSE;
    }
    host_ptr += 3;

    uint32_t pro_len = 0;
    pro_len = host_ptr - url;

    path_ptr = strchr(host_ptr, '/');
    if (path_ptr != NULL)
        host_len = path_ptr - host_ptr;
    else
        host_len = strlen(url) - pro_len;

    if (host_max_len < host_len + 1) {
        QCLOUD_LOG_E("Host str is too small (%d >= %d)", host_max_len, host_len + 1);
        return QCLOUD_ERR_HTTP_PARSE;
    }
    memcpy(host, host_ptr, host_len);
    host[host_len] = '\0';

    return QCLOUD_ERR_SUCCESS;
}

/**
 * @brief 拼接发送的数据
 *
 * @param client    http client
 * @param send_buf  发送数据buffer
 * @param send_idx  标志send_buf数据结束的位置
 * @param buf       需要被发送的数据，拼接到send_buf中
 * @param len       buf的长度
 * @return 返回QCLOUD_ERR_SUCCESS, 表示设置成功
 */
static int _http_client_get_info(HTTPClient *client, unsigned char *send_buf, int *send_idx, char *buf, uint32_t len)
{
    int rc = QCLOUD_ERR_SUCCESS;
    int cp_len;
    int idx = *send_idx;

    if (len == 0) {
        len = strlen(buf);
    }

    do {
        if ((HTTP_CLIENT_SEND_BUF_SIZE - idx) >= len) {
            cp_len = len;
        } else {
            cp_len = HTTP_CLIENT_SEND_BUF_SIZE - idx;
        }

        memcpy(send_buf + idx, buf, cp_len);
        idx += cp_len;
        len -= cp_len;

        if (idx == HTTP_CLIENT_SEND_BUF_SIZE) {
            size_t byte_written_len = 0;
            rc = client->network.write(&(client->network), send_buf, HTTP_CLIENT_SEND_BUF_SIZE, 5000, &byte_written_len);
            if (byte_written_len) {
                return (byte_written_len);
            }
        }
    } while (len);

    *send_idx = idx;
    return rc;
}

static int _http_client_send_auth(HTTPClient *client, unsigned char *send_buf, int *send_idx)
{
    char b_auth[(int)((HTTP_CLIENT_AUTHB_SIZE + 3) * 4 / 3 + 1)];
    char base64buff[HTTP_CLIENT_AUTHB_SIZE + 3];

    _http_client_get_info(client, send_buf, send_idx, "Authorization: Basic ", 0);
    osal_snprintf(base64buff, sizeof(base64buff), "%s:%s", client->auth_user, client->auth_password);

    _http_client_base64enc(b_auth, base64buff);
    b_auth[strlen(b_auth) + 1] = '\0';
    b_auth[strlen(b_auth)] = '\n';

    _http_client_get_info(client, send_buf, send_idx, b_auth, 0);

    return QCLOUD_ERR_SUCCESS;
}

/**
 * @brief 根据请求url和method，拼接请求头
 *
 * @param client        http client
 * @param url           请求url
 * @param method        请求方法
 * @param client_data   http数据负载
 * @return              返回QCLOUD_ERR_SUCCESS, 表示设置成功
 */
static int _http_client_send_header(HTTPClient *client, const char *url, HttpMethod method, HTTPClientData *client_data)
{
    char scheme[8] = { 0 };
    char host[HTTP_CLIENT_MAX_HOST_LEN] = { 0 };
    char path[HTTP_CLIENT_MAX_URL_LEN] = { 0 };
    int len;
    unsigned char send_buf[HTTP_CLIENT_SEND_BUF_SIZE] = { 0 };
    char buf[HTTP_CLIENT_SEND_BUF_SIZE] = { 0 };
    char *meth = (method == HTTP_GET) ? "GET" : (method == HTTP_POST) ? "POST" :
                 (method == HTTP_PUT) ? "PUT" : (method == HTTP_DELETE) ? "DELETE" :
                 (method == HTTP_HEAD) ? "HEAD" : "";
    int rc;
    int port;

    int res = _http_client_parse_url(url, scheme, sizeof(scheme), host, sizeof(host), &port, path, sizeof(path));
    if (res != QCLOUD_ERR_SUCCESS) {
        QCLOUD_LOG_E("httpclient_parse_url returned %d", res);
        return res;
    }

    if (strcmp(scheme, "http") == 0) {

    } else if (strcmp(scheme, "https") == 0) {

    }

    memset(send_buf, 0, HTTP_CLIENT_SEND_BUF_SIZE);
    len = 0;

    osal_snprintf(buf, sizeof(buf), "%s %s HTTP/1.1\r\nHost: %s\r\n", meth, path, host);
    rc = _http_client_get_info(client, send_buf, &len, buf, strlen(buf));
    if (rc) {
        QCLOUD_LOG_E("Could not write request");
        return QCLOUD_ERR_HTTP_CONN;
    }

    if (client->auth_user) {
        _http_client_send_auth(client, send_buf, &len);
    }

    if (client->header) {
        _http_client_get_info(client, send_buf, &len, (char *) client->header, strlen(client->header));
    }

    if (client_data->post_buf != NULL) {
        osal_snprintf(buf, sizeof(buf), "Content-Length: %d\r\n", client_data->post_buf_len);
        _http_client_get_info(client, send_buf, &len, buf, strlen(buf));

        if (client_data->post_content_type != NULL) {
            osal_snprintf(buf, sizeof(buf), "Content-Type: %s\r\n", client_data->post_content_type);
            _http_client_get_info(client, send_buf, &len, buf, strlen(buf));
        }
    }

    _http_client_get_info(client, send_buf, &len, "\r\n", 0);

    //QCLOUD_LOG_D("REQUEST:\n%s", send_buf);

    size_t written_len = 0;
    rc = client->network.write(&client->network, send_buf, len, 5000, &written_len);
    if (written_len > 0) {
        //QCLOUD_LOG_D("Written %lu bytes", written_len);
    } else if (written_len == 0) {
        QCLOUD_LOG_E("written_len == 0,Connection was closed by server");
        return QCLOUD_ERR_HTTP_CLOSED; /* Connection was closed by server */
    } else {
        QCLOUD_LOG_E("Connection error (send returned %d)", rc);
        return QCLOUD_ERR_HTTP_CONN;
    }

    return QCLOUD_ERR_SUCCESS;
}

/**
 * @brief 发送post的请求体数据
 *
 * @param client        http client
 * @param client_data   http数据负载
 * @return              返回QCLOUD_ERR_SUCCESS, 表示设置成功
 */
static int _http_client_send_userdata(HTTPClient *client, HTTPClientData *client_data)
{
    if (client_data->post_buf && client_data->post_buf_len) {
        //QCLOUD_LOG_D("client_data->post_buf: %s", client_data->post_buf);
        {
            size_t written_len = 0;
            int rc = client->network.write(&client->network, (unsigned char *)client_data->post_buf, client_data->post_buf_len, 5000,  &written_len);
            if (written_len > 0) {
                //QCLOUD_LOG_D("Written %d bytes", written_len);
            } else if (written_len == 0) {
                QCLOUD_LOG_E("written_len == 0,Connection was closed by server");
                return QCLOUD_ERR_HTTP_CLOSED;
            } else {
                QCLOUD_LOG_E("Connection error (send returned %d)", rc);
                return QCLOUD_ERR_HTTP_CONN;
            }
        }
    }

    return QCLOUD_ERR_SUCCESS;
}

/**
 * @brief 读取http数据
 *
 * @param client        http client
 * @param buf           数据buffer
 * @param min_len       读取数据的最小长度
 * @param max_len       读取数据的最大长度
 * @param p_read_len    成功读取到的数据的长度
 * @param timeout_ms    超时时间
 * @param client_data   http数据负载
 * @return              返回QCLOUD_ERR_SUCCESS, 表示设置成功
 */
static int _http_client_recv(HTTPClient *client, char *buf, int min_len, int max_len, int *p_read_len, uint32_t timeout_ms, HTTPClientData *client_data)
{
    QCLOUD_FUNC_ENTRY;

    int rc = 0;
    osal_timer_t timer;

    osal_timer_init(&timer);
    osal_timer_countdown_ms(&timer, (unsigned int)timeout_ms);

    *p_read_len = 0;

    rc = client->network.read(&client->network, (unsigned char *)buf, max_len, (uint32_t)osal_timer_remain(&timer), (size_t *)p_read_len);

    if (rc == QCLOUD_ERR_SSL_NOTHING_TO_READ || rc == QCLOUD_ERR_TCP_NOTHING_TO_READ) {
        QCLOUD_LOG_D("HTTP read nothing and timeout");
        rc = QCLOUD_ERR_SUCCESS;
    }
    else if (rc == QCLOUD_ERR_SSL_READ_TIMEOUT || rc == QCLOUD_ERR_TCP_READ_TIMEOUT) {
        if (*p_read_len == client_data->retrieve_len || client_data->retrieve_len == 0)
            rc = QCLOUD_ERR_SUCCESS;
    }
    else if (rc == QCLOUD_ERR_TCP_PEER_SHUTDOWN && *p_read_len > 0) {
        /* HTTP server give response and close this connection */
        client->network.disconnect(&client->network);
        rc = QCLOUD_ERR_SUCCESS;
    }
    else if (rc != QCLOUD_ERR_SUCCESS) { // 其他错误
        QCLOUD_LOG_E("Connection error rc = %d (recv returned %d)", rc, *p_read_len);
        QCLOUD_FUNC_EXIT_RC(rc);
    }

    QCLOUD_FUNC_EXIT_RC(QCLOUD_ERR_SUCCESS);
}

static int _http_client_retrieve_content(HTTPClient *client, char *data, int len, uint32_t timeout_ms,
                                         HTTPClientData *client_data)
{
    QCLOUD_FUNC_ENTRY;

    int count = 0;
    int templen = 0;
    int crlf_pos;
    osal_timer_t timer;

    osal_timer_init(&timer);
    osal_timer_countdown_ms(&timer, (unsigned int)timeout_ms);

    client_data->is_more = QCLOUD_TRUE;

    if (client_data->response_content_len == -1 && client_data->is_chunked == QCLOUD_FALSE) {
        while (1) {
            int rc, max_len;
            if (count + len < client_data->response_buf_len - 1) {
                memcpy(client_data->response_buf + count, data, len);
                count += len;
                client_data->response_buf[count] = '\0';
            } else {
                memcpy(client_data->response_buf + count, data, client_data->response_buf_len - 1 - count);
                client_data->response_buf[client_data->response_buf_len - 1] = '\0';
                return HTTP_RETRIEVE_MORE_DATA;
            }

            max_len = HTTP_CLIENT_MIN(HTTP_CLIENT_CHUNK_SIZE - 1, client_data->response_buf_len - 1 - count);
            rc = _http_client_recv(client, data, 1, max_len, &len, (uint32_t)osal_timer_remain(&timer), client_data);

            /* Receive data */
            //QCLOUD_LOG_D("data len: %d %d", len, count);

            if (rc != QCLOUD_ERR_SUCCESS) {
                QCLOUD_FUNC_EXIT_RC(rc);
            }

            if (len == 0) {
                /* read no more data */
                QCLOUD_LOG_D("no more data, len == 0");
                client_data->is_more = QCLOUD_FALSE;
                QCLOUD_FUNC_EXIT_RC(QCLOUD_ERR_SUCCESS);
            }
        }
    }

    while (1) {
        uint32_t readLen = 0;
        if (client_data->is_chunked && client_data->retrieve_len <= 0) {
            /* Read chunk header */
            bool foundCrlf;
            int n;
            do {
                foundCrlf = QCLOUD_FALSE;
                crlf_pos = 0;
                data[len] = 0;
                if (len >= 2) {
                    for (; crlf_pos < len - 2; crlf_pos++) {
                        if (data[crlf_pos] == '\r' && data[crlf_pos + 1] == '\n') {
                            foundCrlf = QCLOUD_TRUE;
                            break;
                        }
                    }
                }
                if (!foundCrlf) {
                    /* Try to read more */
                    if (len < HTTP_CLIENT_CHUNK_SIZE) {
                        int new_trf_len, rc;
                        rc = _http_client_recv(client,
                                                data + len,
                                                0,
                                                HTTP_CLIENT_CHUNK_SIZE - len - 1,
                                                &new_trf_len,
                                                osal_timer_remain(&timer),
                                                client_data);
                        len += new_trf_len;
                        if (rc != QCLOUD_ERR_SUCCESS) {
                            QCLOUD_FUNC_EXIT_RC(rc);
                        } else {
                            continue;
                        }
                    } else {
                        QCLOUD_FUNC_EXIT_RC(QCLOUD_ERR_HTTP);
                    }
                }
            } while (!foundCrlf);
            data[crlf_pos] = '\0';

            // n = sscanf(data, "%x", &readLen);/* chunk length */
            readLen = strtoul(data, NULL, 16);
            n = (0 == readLen) ? 0 : 1;
            client_data->retrieve_len = readLen;
            client_data->response_content_len += client_data->retrieve_len;
            if (readLen == 0) {
                client_data->is_more = QCLOUD_FALSE;
                QCLOUD_LOG_D("no more (last chunk)");
            }

            if (n != 1) {
                QCLOUD_LOG_E("Could not read chunk length");
                return QCLOUD_ERR_HTTP_UNRESOLVED_DNS;
            }

            memmove(data, &data[crlf_pos + 2], len - (crlf_pos + 2));
            len -= (crlf_pos + 2);

        } else {
            readLen = client_data->retrieve_len;
        }

        do {
            templen = HTTP_CLIENT_MIN(len, readLen);
            if (count + templen < client_data->response_buf_len - 1) {
                memcpy(client_data->response_buf + count, data, templen);
                count += templen;
                client_data->response_buf[count] = '\0';
                client_data->retrieve_len -= templen;
            } else {
                memcpy(client_data->response_buf + count, data, client_data->response_buf_len - 1 - count);
                client_data->response_buf[client_data->response_buf_len - 1] = '\0';
                client_data->retrieve_len -= (client_data->response_buf_len - 1 - count);
                QCLOUD_FUNC_EXIT_RC(HTTP_RETRIEVE_MORE_DATA);
            }

            if (len > readLen) {
                QCLOUD_LOG_D("memmove %d %d %d\n", readLen, len, client_data->retrieve_len);
                memmove(data, &data[readLen], len - readLen); /* chunk case, read between two chunks */
                len -= readLen;
                readLen = 0;
                client_data->retrieve_len = 0;
            } else {
                readLen -= len;
            }

            if (readLen) {
                int rc;
                int max_len = HTTP_CLIENT_MIN(HTTP_CLIENT_CHUNK_SIZE - 1, client_data->response_buf_len - 1 - count);
                max_len = HTTP_CLIENT_MIN(max_len, readLen);
                rc = _http_client_recv(client, data, 1, max_len, &len, osal_timer_remain(&timer), client_data);
                if (rc != QCLOUD_ERR_SUCCESS) {
                    QCLOUD_FUNC_EXIT_RC(rc);
                }
            }
        } while (readLen);

        if (client_data->is_chunked) {
            if (len < 2) {
                int new_trf_len, rc;
                /* Read missing chars to find end of chunk */
                rc = _http_client_recv(client, data + len, 2 - len, HTTP_CLIENT_CHUNK_SIZE - len - 1, &new_trf_len,
                                        osal_timer_remain(&timer), client_data);
                if (rc != QCLOUD_ERR_SUCCESS) {
                    QCLOUD_FUNC_EXIT_RC(rc);
                }
                len += new_trf_len;
            }

            if ((data[0] != '\r') || (data[1] != '\n')) {
                QCLOUD_LOG_E("Format error, %s", data); /* after memmove, the beginning of next chunk */
                QCLOUD_FUNC_EXIT_RC(QCLOUD_ERR_HTTP_UNRESOLVED_DNS);
            }
            memmove(data, &data[2], len - 2); /* remove the \r\n */
            len -= 2;
        } else {
            //QCLOUD_LOG_D("no more (content-length)");
            client_data->is_more = QCLOUD_FALSE;
            break;
        }

    }

    QCLOUD_FUNC_EXIT_RC(QCLOUD_ERR_SUCCESS);
}

/**
 * @brief 解析response body数据
 *
 * @param client        HTTPClient数据
 * @param data          读取到的数据
 * @param len           读取到的数据的长度
 * @param timeout_ms    读取数据的超时时间
 * @param client_data   http数据负载
 * @return              返回QCLOUD_ERR_SUCCESS, 表示成功
 */
static int _http_client_response_parse(HTTPClient *client, char *data, int len, uint32_t timeout_ms,
                                       HTTPClientData *client_data)
{
    QCLOUD_FUNC_ENTRY;

    int crlf_pos;
    osal_timer_t timer;
    char *tmp_ptr, *ptr_body_end;

    osal_timer_init(&timer);
    osal_timer_countdown_ms(&timer, timeout_ms);

    client_data->response_content_len = -1;

    char *crlf_ptr = strstr(data, "\r\n");
    if (crlf_ptr == NULL) {
        QCLOUD_LOG_E("\\r\\n not found");
        QCLOUD_FUNC_EXIT_RC(QCLOUD_ERR_HTTP_UNRESOLVED_DNS);
    }

    crlf_pos = crlf_ptr - data;
    data[crlf_pos] = '\0';

#if 0
    if (sscanf(data, "HTTP/%*d.%*d %d %*[^\r\n]", &(client->response_code)) != 1) {
        QCLOUD_LOG_E("Not a correct HTTP answer : %s\n", data);
        return QCLOUD_ERR_HTTP_UNRESOLVED_DNS;
    }
#endif

    client->response_code = atoi(data + 9);

    if ((client->response_code < 200) || (client->response_code >= 400)) {
        QCLOUD_LOG_W("Response code %d", client->response_code);

        if (client->response_code == 403)
            QCLOUD_FUNC_EXIT_RC(QCLOUD_ERR_HTTP_AUTH);

        if (client->response_code == 404)
            QCLOUD_FUNC_EXIT_RC(QCLOUD_ERR_HTTP_NOT_FOUND);
    }

    //QCLOUD_LOG_D("Reading headers : %s", data);

    // 移除null终止字符
    memmove(data, &data[crlf_pos + 2], len - (crlf_pos + 2) + 1);
    len -= (crlf_pos + 2);

    client_data->is_chunked = QCLOUD_FALSE;

    if (NULL == (ptr_body_end = strstr(data, "\r\n\r\n"))) {
        int new_trf_len, rc;
        rc = _http_client_recv(client, data + len, 1, HTTP_CLIENT_CHUNK_SIZE - len - 1, &new_trf_len, osal_timer_remain(&timer), client_data);
        if (rc != QCLOUD_ERR_SUCCESS) {
            QCLOUD_FUNC_EXIT_RC(rc);
        }
        len += new_trf_len;
        data[len] = '\0';
        if (NULL == (ptr_body_end = strstr(data, "\r\n\r\n"))) {
            QCLOUD_LOG_E("parse error: no end of the request body");
            QCLOUD_FUNC_EXIT_RC(QCLOUD_ERR_FAILURE);
        }
    }

    if (NULL != (tmp_ptr = strstr(data, "Content-Length"))) {
        client_data->response_content_len = atoi(tmp_ptr + strlen("Content-Length: "));
        client_data->retrieve_len = client_data->response_content_len;
    } else if (NULL != (tmp_ptr = strstr(data, "Transfer-Encoding"))) {
        int len_chunk = strlen("Chunked");
        char *chunk_value = data + strlen("Transfer-Encoding: ");

        if ((! memcmp(chunk_value, "Chunked", len_chunk))
            || (! memcmp(chunk_value, "chunked", len_chunk))) {
            client_data->is_chunked = QCLOUD_TRUE;
            client_data->response_content_len = 0;
            client_data->retrieve_len = 0;
        }
    } else {
        QCLOUD_LOG_E("Could not parse header");
        QCLOUD_FUNC_EXIT_RC(QCLOUD_ERR_HTTP);
    }

    len = len - (ptr_body_end + 4 - data);
    memmove(data, ptr_body_end + 4, len + 1);
    int rc = _http_client_retrieve_content(client, data, len, osal_timer_remain(&timer), client_data);
    QCLOUD_FUNC_EXIT_RC(rc);
}

static int _http_client_connect(HTTPClient *client)
{
    if (QCLOUD_ERR_SUCCESS != client->network.connect(&client->network)) {
        return QCLOUD_ERR_HTTP_CONN;
    }

    return QCLOUD_ERR_SUCCESS;
}

static int _http_client_send_request(HTTPClient *client, const char *url, HttpMethod method, HTTPClientData *client_data)
{
    int rc;

    rc = _http_client_send_header(client, url, method, client_data);
    if (rc != 0) {
        QCLOUD_LOG_E("httpclient_send_header is error, rc = %d", rc);
        return rc;
    }

    if (method == HTTP_POST || method == HTTP_PUT) {
        rc = _http_client_send_userdata(client, client_data);
    }

    return rc;
}

/**
 * @brief 接收http返回的数据
 *
 * @param client        http client
 * @param timeout_ms    读取数据的超时时间
 * @param client_data   http数据负载
 * @return              返回QCLOUD_ERR_SUCCESS, 表示设置成功
 */
static int _http_client_recv_response(HTTPClient *client, uint32_t timeout_ms, HTTPClientData *client_data)
{
    QCLOUD_FUNC_ENTRY;

    int reclen = 0, rc = QCLOUD_ERR_HTTP_CONN;
    char buf[HTTP_CLIENT_CHUNK_SIZE] = { 0 };
    osal_timer_t timer;

    osal_timer_init(&timer);
    osal_timer_countdown_ms(&timer, timeout_ms);

    if (!client->network.is_connected(&client->network)) {
        QCLOUD_LOG_E("Connection has not been established");
        QCLOUD_FUNC_EXIT_RC(rc);
    }

    if (client_data->is_more) {
        client_data->response_buf[0] = '\0';
        rc = _http_client_retrieve_content(client, buf, reclen, osal_timer_remain(&timer), client_data);
    } else {
        client_data->is_more = QCLOUD_TRUE;
        rc = _http_client_recv(client, buf, 1, HTTP_CLIENT_CHUNK_SIZE - 1, &reclen, osal_timer_remain(&timer), client_data);

        if (rc != QCLOUD_ERR_SUCCESS) {
            QCLOUD_FUNC_EXIT_RC(rc);
        }

        buf[reclen] = '\0';

        if (reclen) {
            //osal_printf("RESPONSE:\n%s", buf);
            rc = _http_client_response_parse(client, buf, reclen, osal_timer_remain(&timer), client_data);
        }
    }

    QCLOUD_FUNC_EXIT_RC(rc);
}

static int _http_network_init(qcloud_network_t *network, const char *host, int port, const char *ca_crt_dir)
{
    int rc = QCLOUD_ERR_SUCCESS;
    if (network == NULL) {
        return QCLOUD_ERR_INVAL;
    }
#if (QCLOUD_CFG_TLS_EN > 0u)
    if (ca_crt_dir != NULL) {
        network->tls_opt.ca_cert = ca_crt_dir;
        network->tls_opt.ca_cert_len = strlen(network->tls_opt.ca_cert);
        network->tls_opt.timeout = 10000;
    }
#endif

    memset(network->host, 0, sizeof(network->host));
    strncpy(network->host, host, sizeof(network->host) - 1);

    network->port = port;

    rc = qcloud_network_tcp_init(network);

    return rc;
}

int qcloud_http_client_connect(HTTPClient *client, const char *url, int port, const char *ca_crt)
{
    if (!client->network.is_connected(&client->network)) {
        QCLOUD_LOG_E("http client has connected to host!");
        return QCLOUD_ERR_HTTP_CONN;
    }

    int rc;
    char host[HTTP_CLIENT_MAX_HOST_LEN] = {0};
    rc = _http_client_parse_host(url, host, sizeof(host));
    if (rc != QCLOUD_ERR_SUCCESS) return rc;

    rc = _http_network_init(&client->network, host, port, ca_crt);
    if (rc != QCLOUD_ERR_SUCCESS)
        return rc;

    rc = _http_client_connect(client);
    if (rc != QCLOUD_ERR_SUCCESS) {
        QCLOUD_LOG_E("http_client_connect is error,rc = %d", rc);
        qcloud_http_client_close(client);
    } else {
        /* reduce log print due to frequent log server connect/disconnect */
		QCLOUD_LOG_D("http client connect success");
    }
    return rc;
}

void qcloud_http_client_close(HTTPClient *client)
{
    if (client->network.is_connected(&client->network)) {
        client->network.disconnect(&client->network);
    }
}

int qcloud_http_client_common(HTTPClient *client, const char *url, int port, const char *ca_crt, HttpMethod method, HTTPClientData *client_data)
{
    int rc;

    if (!client->network.is_connected(&client->network)) {
        rc = qcloud_http_client_connect(client, url, port, ca_crt);
        if (rc != QCLOUD_ERR_SUCCESS) return rc;
    }

    rc = _http_client_send_request(client, url, method, client_data);
    if (rc != QCLOUD_ERR_SUCCESS) {
        QCLOUD_LOG_E("http_client_send_request is error,rc = %d", rc);
        qcloud_http_client_close(client);
        return rc;
    }

    return QCLOUD_ERR_SUCCESS;
}

int qcloud_http_recv_data(HTTPClient *client, uint32_t timeout_ms, HTTPClientData *client_data)
{
    QCLOUD_FUNC_ENTRY;

    int rc = QCLOUD_ERR_SUCCESS;
    osal_timer_t timer;

    osal_timer_init(&timer);
    osal_timer_countdown_ms(&timer, (unsigned int) timeout_ms);

    if ((NULL != client_data->response_buf)
        && (0 != client_data->response_buf_len)) {
        rc = _http_client_recv_response(client, osal_timer_remain(&timer), client_data);
        if (rc < 0) {
            QCLOUD_LOG_E("http_client_recv_response is error,rc = %d", rc);
            qcloud_http_client_close(client);
            QCLOUD_FUNC_EXIT_RC(rc);
        }
    }
    QCLOUD_FUNC_EXIT_RC(QCLOUD_ERR_SUCCESS);
}



#ifdef __cplusplus
}
#endif
