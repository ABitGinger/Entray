#!/usr/bin/env python3
"""检查构建出来的 Entray.exe 是否满足“单文件免安装”的要求。

用法：

    python tools/check_exe.py build/Release/Entray.exe

会在下面几项上把关，任何一项不通过就以非零码退出（因此可以直接放进 CI）：

1. 没有依赖 MSVC / MinGW 运行库，也就是不会在别人电脑上提示“缺少 xxx.dll”；
2. 内嵌了应用程序清单，而且带 dpiAware / asInvoker；
3. 内嵌了图标（多尺寸）；
4. 内嵌了版本信息。
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

# 输出里带中文，统一按 UTF-8 写，避免在 GBK 控制台里变成乱码。
# 调用方（scripts/build.ps1、CI）会同步把控制台输出编码也设成 UTF-8。
try:
    sys.stdout.reconfigure(encoding="utf-8")  # type: ignore[union-attr]
except Exception:  # pragma: no cover - 老版本 Python 或输出被重定向时忽略
    pass

# 一旦 import 表里出现这些名字，就说明运行时没能静态链接进去：
# 目标机器只要没装 VC++ 运行库 / Debug 运行库，程序就会直接启动失败。
FORBIDDEN_IMPORTS = (
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "msvcp140.dll",
    "concrt140.dll",
    "ucrtbased.dll",
    "vcruntimed.dll",
    "msvcp140d.dll",
    "libstdc++-6.dll",
    "libgcc_s_seh-1.dll",
    "libgcc_s_dw2-1.dll",
    "libwinpthread-1.dll",
)

RT_ICON = 3
RT_GROUP_ICON = 14
RT_VERSION = 16
RT_MANIFEST = 24


class PEFile:
    def __init__(self, data: bytes) -> None:
        self.data = data
        if data[:2] != b"MZ":
            raise ValueError("不是有效的 PE 文件（缺少 MZ 头）")
        self.pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
        if data[self.pe_offset : self.pe_offset + 4] != b"PE\0\0":
            raise ValueError("不是有效的 PE 文件（缺少 PE 签名）")

        coff = self.pe_offset + 4
        (
            self.machine,
            self.section_count,
            _timestamp,
            _symtab,
            _symcount,
            self.optional_size,
            self.characteristics,
        ) = struct.unpack_from("<HHIIIHH", data, coff)

        optional = coff + 20
        self.magic = struct.unpack_from("<H", data, optional)[0]
        if self.magic == 0x20B:
            self.is_pe32_plus = True
            self.data_directory_offset = optional + 112
        elif self.magic == 0x10B:
            self.is_pe32_plus = False
            self.data_directory_offset = optional + 96
        else:
            raise ValueError(f"无法识别的可选头 magic：0x{self.magic:x}")

        self.sections = []
        section_table = optional + self.optional_size
        for index in range(self.section_count):
            offset = section_table + index * 40
            name = data[offset : offset + 8].rstrip(b"\0").decode("ascii", "replace")
            (_vsize, vaddr, raw_size, raw_ptr, *_rest) = struct.unpack_from(
                "<IIIIIIHHI", data, offset + 8
            )
            self.sections.append((name, vaddr, raw_size, raw_ptr))

    def section_for_rva(self, rva: int):
        for name, vaddr, raw_size, raw_ptr in self.sections:
            span = max(raw_size, 0x1000)
            if vaddr <= rva < vaddr + span:
                return name, rva - vaddr + raw_ptr
        return None, None

    def read_rva(self, rva: int, size: int) -> bytes:
        _name, offset = self.section_for_rva(rva)
        if offset is None:
            return b""
        return self.data[offset : offset + size]

    def read_c_string_at_rva(self, rva: int) -> str:
        _name, offset = self.section_for_rva(rva)
        if offset is None:
            return ""
        end = self.data.find(b"\0", offset)
        return self.data[offset:end].decode("ascii", "replace")

    def data_directory(self, index: int) -> tuple[int, int]:
        return struct.unpack_from("<II", self.data, self.data_directory_offset + index * 8)

    # ---- 导入表 ----
    def imported_dlls(self) -> list[str]:
        rva, size = self.data_directory(1)
        if rva == 0:
            return []
        names = []
        cursor = 0
        while cursor < size:
            descriptor = self.read_rva(rva + cursor, 20)
            if len(descriptor) < 20:
                break
            original, _ts, _fwd, name_rva, _first = struct.unpack("<IIIII", descriptor)
            if original == 0 and name_rva == 0:
                break
            if name_rva:
                names.append(self.read_c_string_at_rva(name_rva))
            cursor += 20
        return names

    # ---- 资源目录 ----
    def _resource_entries(self, base: int, dir_offset: int):
        header = self.read_rva(base + dir_offset, 16)
        if len(header) < 16:
            return []
        named, ids = struct.unpack_from("<HH", header, 12)
        entries = []
        for index in range(named + ids):
            raw = self.read_rva(base + dir_offset + 16 + index * 8, 8)
            if len(raw) < 8:
                break
            name_or_id, offset_to_data = struct.unpack("<II", raw)
            entries.append((name_or_id, offset_to_data))
        return entries

    def resources(self) -> dict[int, list[bytes]]:
        """返回 {资源类型: [数据, ...]}，只保留有实际数据的叶子节点。"""
        rva, _size = self.data_directory(2)
        if rva == 0:
            return {}

        result: dict[int, list[bytes]] = {}
        for type_id, type_offset in self._resource_entries(rva, 0):
            if type_offset & 0x80000000:
                type_dir = type_offset & 0x7FFFFFFF
            else:
                continue  # 类型本身不该是叶子节点
            for _name_id, name_offset in self._resource_entries(rva, type_dir):
                if not (name_offset & 0x80000000):
                    continue
                name_dir = name_offset & 0x7FFFFFFF
                for _lang_id, lang_offset in self._resource_entries(rva, name_dir):
                    if lang_offset & 0x80000000:
                        continue
                    entry = self.read_rva(rva + lang_offset, 16)
                    if len(entry) < 16:
                        continue
                    data_rva, data_size, _cp, _res = struct.unpack("<IIII", entry)
                    blob = self.read_rva(data_rva, data_size)
                    result.setdefault(type_id, []).append(blob)
        return result


def check(path: Path) -> int:
    print(f"检查 {path}")
    data = path.read_bytes()
    pe = PEFile(data)
    print(f"  架构          : {pe.machine:#06x}（{'x64' if pe.machine == 0x8664 else 'x86' if pe.machine == 0x14c else '?'}）")
    print(f"  大小          : {len(data) / 1024:.1f} KiB")

    failures: list[str] = []
    warnings: list[str] = []

    # ---- 1. 依赖 ----
    dlls = pe.imported_dlls()
    print(f"  依赖 DLL ({len(dlls)}): {', '.join(sorted(set(dlls)))}")
    forbidden = sorted({d.lower() for d in dlls} & set(FORBIDDEN_IMPORTS))
    if forbidden:
        failures.append(
            "运行时库没有静态链接，目标机器会缺少 " + ", ".join(forbidden)
        )

    # ---- 2. 资源 ----
    resources = pe.resources()
    manifests = resources.get(RT_MANIFEST, [])
    if not manifests:
        failures.append("没有内嵌应用程序清单，Windows 可能按安装程序启发式规则弹 UAC")
    else:
        text = manifests[0].decode("utf-8", "replace")
        for needle in ("dpiAware", "asInvoker", "Common-Controls"):
            if needle not in text:
                failures.append(f"清单里缺少 {needle}")

    icons = resources.get(RT_ICON, [])
    groups = resources.get(RT_GROUP_ICON, [])
    if not icons or not groups:
        failures.append("没有内嵌图标资源")
    else:
        # RT_GROUP_ICON 的头部：reserved(2) type(2) count(2)
        count = struct.unpack_from("<H", groups[0], 4)[0] if len(groups[0]) >= 6 else 0
        if count < 4:
            warnings.append(f"图标只有 {count} 个尺寸，在小任务栏/高 DPI 下可能会糊")
        print(f"  图标          : {count} 个尺寸，{len(icons)} 张位图")

    if not resources.get(RT_VERSION):
        failures.append("没有内嵌版本信息")

    # ---- 结论 ----
    print()
    for message in warnings:
        print(f"  [警告] {message}")
    if failures:
        for message in failures:
            print(f"  [失败] {message}")
        return 1
    print("  全部检查通过：单文件、免安装、图标与清单均已内嵌。")
    return 0


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    path = Path(sys.argv[1])
    if not path.is_file():
        print(f"找不到文件：{path}")
        return 2
    return check(path)


if __name__ == "__main__":
    sys.exit(main())
