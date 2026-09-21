/*
 * TCP 分帧实现：socket 是字节流，一次 read/write 不等于一个完整报文。
 * 发送端固定先发 12 字节头再发负载；接收端严格按同一顺序收齐并校验。
 */
#include "protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

int read_full(int fd, void *buffer, size_t length)
{
    /*
     * 持续读取，直到收齐、遇到 EOF 或发生真实错误。
     * offset 记录已收字节数；EINTR 只表示被信号中断，应继续读取。
     */
    size_t offset = 0;
    while (offset < length) {
        ssize_t count = read(fd, (char *)buffer + offset, length - offset);
        if (count == 0) return 0;
        if (count < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        offset += (size_t)count;
    }
    return 1;
}

int write_full(int fd, const void *buffer, size_t length)
{
    /*
     * 发送端对应处理；TCP 背压下部分写入是正常现象。
     * 返回 0 之前，头和负载的每个字节都已经交给内核 socket 缓冲区。
     */
    size_t offset = 0;
    while (offset < length) {
        ssize_t count = write(fd, (const char *)buffer + offset, length - offset);
        if (count < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        offset += (size_t)count;
    }
    return 0;
}

int protocol_send_version(int fd, uint16_t version, uint16_t command,
                          const void *payload, uint32_t length)
{
    /* 在协议边界把所有头字段转为网络字节序。 */
    struct protocol_header header;
    if ((version != SPARAM_VERSION_V1 && version != SPARAM_VERSION_V2) ||
        length > SPARAM_MAX_PAYLOAD || (length && !payload)) {
        errno = EINVAL;
        return -1;
    }
    header.magic = htonl(SPARAM_MAGIC);
    header.version = htons(version);
    header.command = htons(command);
    header.payload_length = htonl(length);
    if (write_full(fd, &header, sizeof(header)) ||
        (length && write_full(fd, payload, length))) return -1;
    return 0;
}

int protocol_send(int fd, uint16_t command, const void *payload, uint32_t length)
{
    return protocol_send_version(fd, SPARAM_VERSION_V1, command, payload, length);
}

int protocol_receive(int fd, struct protocol_header *header, void *payload,
                     size_t capacity)
{
    /*
     * 先校验 magic、版本和长度上限，再信任对端数据。
     * 特别是 payload_length 来自网络，未限制就可能使接收端越界或无限等待。
     */
    struct protocol_header wire;
    int status = read_full(fd, &wire, sizeof(wire));
    if (status <= 0) return status;
    header->magic = ntohl(wire.magic);
    header->version = ntohs(wire.version);
    header->command = ntohs(wire.command);
    header->payload_length = ntohl(wire.payload_length);
    if (header->magic != SPARAM_MAGIC ||
        (header->version != SPARAM_VERSION_V1 &&
         header->version != SPARAM_VERSION_V2) ||
        header->payload_length > SPARAM_MAX_PAYLOAD ||
        header->payload_length > capacity) {
        errno = EPROTO;
        return -1;
    }
    if (header->payload_length)
        return read_full(fd, payload, header->payload_length);
    return 1;
}
