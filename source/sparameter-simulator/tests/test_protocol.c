/* 不依赖真实网卡，验证一次完整协议帧交换。 */
#include "protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    int sockets[2];
    char payload[32] = {0};
    struct protocol_header header;
    /* socketpair 仍是字节流，可直接测试协议边界。 */
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(protocol_send(sockets[0], CMD_STATUS, "test", 4) == 0);
    assert(protocol_receive(sockets[1], &header, payload, sizeof(payload)) == 1);
    assert(header.command == CMD_STATUS);
    assert(header.payload_length == 4);
    assert(memcmp(payload, "test", 4) == 0);
    close(sockets[0]); close(sockets[1]);

    /* v2 复用同一固定头，版本由发送方显式指定。 */
    memset(payload, 0, sizeof(payload));
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(protocol_send_version(sockets[0], SPARAM_VERSION_V2,
                                 CMD_START_SWEEP, "", 0) == 0);
    assert(protocol_receive(sockets[1], &header, payload, sizeof(payload)) == 1);
    assert(header.version == SPARAM_VERSION_V2);
    assert(header.command == CMD_START_SWEEP);
    close(sockets[0]); close(sockets[1]);
    puts("protocol tests: PASS");
    return 0;
}
