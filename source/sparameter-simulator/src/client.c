#define _POSIX_C_SOURCE 200809L
/* 用于服务冒烟验证的命令行客户端；后续 Qt 使用同一协议合同。 */
#include "protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int command_id(const char *text)
{
    /* 将用户命令词与线上数字命令码分开。 */
    if (!strcmp(text, "start")) return CMD_START;
    if (!strcmp(text, "stop")) return CMD_STOP;
    if (!strcmp(text, "status")) return CMD_STATUS;
    if (!strcmp(text, "algorithm")) return CMD_SET_ALGORITHM;
    if (!strcmp(text, "result")) return CMD_GET_RESULT;
    if (!strcmp(text, "set-sweep")) return CMD_SET_SWEEP;
    if (!strcmp(text, "cal-begin")) return CMD_CALIBRATE_BEGIN;
    if (!strcmp(text, "cal-step")) return CMD_CALIBRATE_STEP;
    if (!strcmp(text, "cal-status")) return CMD_CALIBRATE_STATUS;
    if (!strcmp(text, "grain")) return CMD_SET_GRAIN;
    if (!strcmp(text, "sweep")) return CMD_START_SWEEP;
    if (!strcmp(text, "trace")) return CMD_GET_TRACE;
    if (!strcmp(text, "application")) return CMD_GET_APPLICATION_RESULT;
    return -1;
}

int main(int argc, char **argv)
{
    struct addrinfo hints = {0}, *addresses;
    struct protocol_header header;
    char response[SPARAM_MAX_PAYLOAD + 1];
    const char *payload = "";
    int socket_fd, command;
    uint16_t version;

    if (argc < 4 || argc > 5 || (command = command_id(argv[3])) < 0) {
        fprintf(stderr,
                "usage: %s HOST PORT COMMAND [PAYLOAD]\n"
                "v1: start stop status result algorithm\n"
                "v2: set-sweep cal-begin cal-step cal-status grain sweep trace application\n",
                argv[0]);
        return 2;
    }
    if (argc == 5)
        payload = argv[4];
    if ((command == CMD_SET_ALGORITHM || command == CMD_SET_SWEEP ||
         command == CMD_CALIBRATE_STEP || command == CMD_SET_GRAIN ||
         command == CMD_GET_TRACE) && argc != 5)
        return 2;
    version = command >= CMD_SET_SWEEP ? SPARAM_VERSION_V2 : SPARAM_VERSION_V1;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(argv[1], argv[2], &hints, &addresses)) return 1;
    socket_fd = socket(addresses->ai_family, addresses->ai_socktype, addresses->ai_protocol);
    if (socket_fd < 0 || connect(socket_fd, addresses->ai_addr, addresses->ai_addrlen)) {
        perror("connect"); freeaddrinfo(addresses); return 1;
    }
    freeaddrinfo(addresses);
    /* protocol_receive 处理 TCP 分片，不能假设一次读取收到完整响应。 */
    if (protocol_send_version(socket_fd, version, (uint16_t)command,
                              payload, (uint32_t)strlen(payload)) ||
        protocol_receive(socket_fd, &header, response, SPARAM_MAX_PAYLOAD) <= 0) {
        perror("protocol"); close(socket_fd); return 1;
    }
    response[header.payload_length] = '\0';
    puts(response);
    close(socket_fd);
    return 0;
}
