#ifndef PROTOCOL_H
#define PROTOCOL_H

/*
 * 带版本的 TCP 分帧协议。
 * TCP 只有连续字节，没有“消息”边界；所以每条命令先传固定 12 字节头，
 * 接收端先读满头，再按 payload_length 精确读取负载。
 */
#include <stddef.h>
#include <stdint.h>

#define SPARAM_MAGIC 0x53504152U /* ASCII "SPAR"，用于拒绝无关连接数据。 */
#define SPARAM_VERSION_V1 1U
#define SPARAM_VERSION_V2 2U
#define SPARAM_VERSION SPARAM_VERSION_V1
#define SPARAM_MAX_PAYLOAD 4096U

enum protocol_command {
    CMD_START = 1,
    CMD_STOP = 2,
    CMD_STATUS = 3,
    CMD_SET_ALGORITHM = 4,
    CMD_GET_RESULT = 5,
    /* v2 新命令保留 v1 编号和语义，曲线通过分页请求避免超大网络包。 */
    CMD_SET_SWEEP = 10,
    CMD_CALIBRATE_BEGIN = 11,
    CMD_CALIBRATE_STEP = 12,
    CMD_CALIBRATE_STATUS = 13,
    CMD_SET_GRAIN = 14,
    CMD_START_SWEEP = 15,
    CMD_GET_TRACE = 16,
    CMD_GET_APPLICATION_RESULT = 17,
    CMD_RESPONSE = 0x8000,
};

struct protocol_header {
    /*
     * protocol_receive 后为主机字节序；在线上始终使用网络字节序。
     * magic 识别本协议，version 保证兼容性，length 防止无界内存读取。
     */
    uint32_t magic;
    uint16_t version;
    uint16_t command;
    uint32_t payload_length;
};

/*
 * 处理 TCP 的部分读写，直到收发完指定字节数。
 * read_full 返回 1 表示收齐，0 表示对端 EOF，-1 表示错误；
 * write_full 返回 0 表示写完，-1 表示错误。
 */
int read_full(int fd, void *buffer, size_t length);
int write_full(int fd, const void *buffer, size_t length);
int protocol_send_version(int fd, uint16_t version, uint16_t command,
                          const void *payload, uint32_t length);
int protocol_send(int fd, uint16_t command, const void *payload, uint32_t length);
int protocol_receive(int fd, struct protocol_header *header, void *payload,
                     size_t capacity);

#endif
