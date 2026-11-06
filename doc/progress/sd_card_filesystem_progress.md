# MicroSD Storage & Filesystem Subsystem Progress

- **Status**: **PLANNED (10%)**
- **Architecture Reference**: [`doc/architecture/sd_card_filesystem_subsystem.md`](../architecture/sd_card_filesystem_subsystem.md)

---

## 1. Implemented Components

| Component | File Path | Status | Description |
| :--- | :--- | :---: | :--- |
| **Storage FS Interface** | `app/include/interfaces/i_storage_fs.h` | 100% | Pure VFS abstract port (`mount`, `unmount`, `open`, `read`, `write`, `sync`, `stat`) |
| **Common Types** | `app/include/common/storage_fs_types.h` | 100% | File modes, error codes, file descriptor handle types |

---

## 2. Pending Work
- [ ] Implement `adapter_storage_sd_fatfs.c` wrapping Zephyr SDMMC1 disk driver & ELM FatFS.
- [ ] Implement hot-plug card detect GPIO (PG5 / `cd-gpios`) listener.
- [ ] Implement Active Object `ao_storage_sd` for asynchronous disk I/O.
- [ ] Wire filesystem subsystem in `composition_root.c`.

---

## 3. Changelog & Refactoring History
- **2026-11-05**: Initial port contract `i_storage_fs.h` defined.
