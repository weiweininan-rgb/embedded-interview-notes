#!/usr/bin/env python3
"""TCP 黑盒测试：分片报文、任务控制、结果读取和错误处理。"""
import os
import socket
import struct
import subprocess
import time

MAGIC = 0x53504152
VERSION = 1
VERSION_V2 = 2
PORT = 19000
BUILD = os.environ.get("SPARAM_BUILD", "build")
SERVICE = os.path.join(BUILD, "sparam_service")

missing_config = subprocess.run(
    [SERVICE, "--config", "config/not-found.conf", "--once"],
    stdout=subprocess.DEVNULL,
    stderr=subprocess.PIPE,
    universal_newlines=True,
)
assert missing_config.returncode == 0
assert "using defaults" in missing_config.stderr


def make_packet(command, payload=b"", version=VERSION):
    """构造与 C 实现共用的 12 字节网络序协议头。"""
    return struct.pack("!IHHI", MAGIC, version, command, len(payload)) + payload


def receive_full(connection, length):
    """TCP 响应可任意分片，因此必须收齐指定字节数。"""
    data = b""
    while len(data) < length:
        chunk = connection.recv(length - len(data))
        assert chunk
        data += chunk
    return data


def receive_response(connection, expected_version=VERSION):
    header = receive_full(connection, 12)
    magic, version, response_command, length = struct.unpack("!IHHI", header)
    assert magic == MAGIC and version == expected_version and response_command == 0x8000
    return receive_full(connection, length).decode()


def request(command, payload=b"", fragmented=False, version=VERSION):
    """发送一条命令；可选择逐字节发送以测试服务端。"""
    packet = make_packet(command, payload, version)
    # 首个状态请求故意分片，证明服务不依赖 TCP 包边界。
    with socket.create_connection(("127.0.0.1", PORT), timeout=2) as connection:
        if fragmented:
            for byte in packet:
                connection.sendall(bytes([byte]))
        else:
            connection.sendall(packet)
        return receive_response(connection, version)


def wait_done(version=VERSION_V2):
    deadline = time.time() + 8
    while True:
        status = request(3, version=version)
        if "state=DONE" in status:
            return status
        assert "state=ERROR" not in status
        assert time.time() < deadline
        time.sleep(0.01)


server = subprocess.Popen(
    [SERVICE, "--config", "config/test.conf"],
    stdout=subprocess.DEVNULL,
    stderr=subprocess.PIPE,
    universal_newlines=True,
)
try:
    deadline = time.time() + 3
    while True:
        try:
            assert "IDLE" in request(3, fragmented=True)
            break
        except (ConnectionRefusedError, OSError):
            if time.time() > deadline:
                raise
            time.sleep(0.05)
    assert request(4, b"fft") == "ok"
    # 一次写入两个帧，验证服务可在同一连接中持续解析。
    with socket.create_connection(("127.0.0.1", PORT), timeout=2) as connection:
        connection.sendall(make_packet(3) + make_packet(99))
        assert "IDLE" in receive_response(connection)
        assert receive_response(connection) == "error=unknown_command"
    assert request(1) == "ok"
    deadline = time.time() + 3
    while "DONE" not in request(3):
        assert time.time() < deadline
        time.sleep(0.02)
    result = request(5)
    assert "s11_db=-6.020600" in result and "s21_db=-12.041200" in result
    assert request(99) == "error=unknown_command"
    # v2：设置扫频后依次完成四个标准件，服务端负责校准顺序。
    sweep = (b"start_hz=1450000000&stop_hz=1550000000&step_hz=1000000"
             b"&scenario=rice_demo&samples=256")
    assert request(10, sweep, version=VERSION_V2) == "ok"
    assert request(10, sweep, version=VERSION) == "error=version_required"
    assert request(17, version=VERSION_V2) == "error=not_calibrated"
    assert request(11, version=VERSION_V2) == "ok&next=load"
    assert request(12, b"open", version=VERSION_V2) == "error=invalid_order_or_busy"
    for standard in (b"load", b"open", b"short", b"thru"):
        assert request(12, standard, version=VERSION_V2) == "ok"
        wait_done()
    calibration = request(13, version=VERSION_V2)
    assert "mask=15" in calibration and "ready=1" in calibration
    assert request(14, b"rice", version=VERSION_V2) == "ok"
    assert request(15, version=VERSION_V2) == "ok"
    wait_done()
    raw_page = request(
        16, b"representation=raw&channel=s21&offset=0&count=10",
        version=VERSION_V2,
    )
    calibrated_page = request(
        16, b"representation=calibrated&channel=s21&offset=0&count=10",
        version=VERSION_V2,
    )
    assert raw_page.startswith("total=101&offset=0&count=10")
    assert calibrated_page.startswith("total=101&offset=0&count=10")
    application = request(17, version=VERSION_V2)
    application_fields = dict(item.split("=", 1) for item in application.split("&"))
    assert abs(float(application_fields["f0_hz"]) - 1.5e9) <= 5.0e5
    assert 47.0 < float(application_fields["q"]) < 53.0
    assert application_fields["grain"] == "rice"
    assert "moisture=" in application
    # 错误 magic 必须关闭连接，不能被当成普通命令。
    with socket.create_connection(("127.0.0.1", PORT), timeout=2) as connection:
        connection.sendall(struct.pack("!IHHI", 0xDEADBEEF, VERSION, 3, 0))
        assert connection.recv(1) == b""
    print("end-to-end network tests: PASS")
finally:
    server.terminate()
    assert server.wait(timeout=3) == 0
