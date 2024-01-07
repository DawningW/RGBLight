#!/usr/bin/env python3
# coding=utf-8

import sys
import os
from enum import Enum, IntEnum
import struct
import shutil
import binascii
import hashlib

bin_path = "./build/RGBLight.ino.bin"
data_path = "./data"
output_path = "./build"
upgrade_pack_name = "upgrade.bin"

class UpgradeOperation(IntEnum):
    ADD = 0
    DEL = 1
    MOD = 2

class UpgradePackHeader:
    def __init__(self, version, file_count):
        self.magic = b"UPKG"
        self.version = version
        self.file_count = file_count
        self.crc32 = 0

    def pack(self):
        temp = struct.pack("<4sII", self.magic, self.version, self.file_count)
        self.crc32 = binascii.crc32(temp)
        temp += struct.pack("<I", self.crc32)
        return temp

class UpgradeFileHeader:
    def __init__(self, operation, file_name, file_size):
        self.magic = b"\xAA"
        self.operation = operation
        self.file_name = file_name
        self.file_size = file_size

    def pack(self):
        file_name = self.file_name.encode("utf-8", errors="ignore") + b"\x00"
        temp = struct.pack("<cB", self.magic, self.operation)
        temp += file_name
        temp += struct.pack("<I", self.file_size)
        return temp

if __name__ == "__main__":
    if not os.path.exists(bin_path):
        print("未找到编译后的 bin 文件, 请先编译项目或将 bin_path 变量改为编译输出目录")
        exit(1)

    upgrade_pack_path = os.path.join(output_path, upgrade_pack_name)
    with open(upgrade_pack_path, "wb") as file:
        file_count = 0
        file.write(b'\x00' * 16) # 预留头部空间

        # 添加 bin 文件
        with open(bin_path, "rb") as bin_file:
            file_size = os.path.getsize(bin_path)
            file_name = "/RGBLight.bin"
            file_header = UpgradeFileHeader(UpgradeOperation.ADD, file_name, file_size)
            file.write(file_header.pack())
            shutil.copyfileobj(bin_file, file)
            print(f"Add {bin_path} -> {file_name}")
            file_count += 1

        # 添加 data 目录下的资源文件
        for root, dirs, files in os.walk(data_path):
            for file_name in files:
                file_path = os.path.join(root, file_name)
                with open(file_path, "rb") as data_file:
                    file_size = os.path.getsize(file_path)
                    file_name2 = file_path.replace(data_path, "").replace("\\", "/")
                    file_header = UpgradeFileHeader(UpgradeOperation.ADD, file_name2, file_size)
                    file.write(file_header.pack())
                    shutil.copyfileobj(data_file, file)
                    print(f"Add {file_path} -> {file_name2}")
                    file_count += 1

        file.seek(0)
        header = UpgradePackHeader(1, file_count)
        file.write(header.pack())
        print(f"升级包 {upgrade_pack_path} 打包完成, 文件数量: {file_count}")
