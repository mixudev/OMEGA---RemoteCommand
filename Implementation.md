

Saya telah melakukan studi mendalam terhadap sistem **OMEGA C2** Anda. Selain bug input terminal yang sudah kita perbaiki sebelumnya, saya menemukan beberapa akar masalah yang menyebabkan fitur-fitur lain terasa "rusak" atau tidak stabil:

### 🔍 Temuan Utama (Fitur yang Bermasalah):

1.  **Sesi "Zombie"**: Saat agen terputus secara tidak normal (misalnya agent dimatikan paksa), server tidak menyadarinya. Sesi tetap muncul di daftar `sessions`, namun saat di-interaksi akan memunculkan error `ERR_RECV` tanpa menghapus sesi tersebut dari daftar.
2.  **Ketergantungan PowerShell**: Fitur `tasks` dan `drives` sangat bergantung pada PowerShell. Jika target memiliki kebijakan eksekusi yang ketat atau PowerShell dinonaktifkan, fitur ini akan gagal total.
3.  **Bug Path Spasi pada Upload**: Perintah `:upload` pada sisi agen (executor) menggunakan pemisah spasi pertama untuk memisahkan *path* dan *data*. Jika Anda mengunggah ke path yang mengandung spasi (misal: `C:\Program Files\target.exe`), sistem akan gagal memprosesnya.
4.  **Verifikasi Tunnel Kurang Akurat**: Status tunnel saat ini hanya mengecek apakah proses `localtonet.exe` berjalan, bukan apakah koneksi tunnel tersebut benar-benar sukses dan aktif.

### 🛠️ Rencana Perbaikan:

Saya telah menyusun **Implementation Plan** mendalam untuk menstabilkan sistem ini:
*   **Pembersihan Sesi Otomatis**: Server akan langsung menghapus sesi jika terjadi kegagalan komunikasi (`socket error`).
*   **Native C++ Fallback**: Menambahkan implementasi native Win32 API untuk `tasks` dan `drives` sebagai cadangan jika PowerShell tidak tersedia.
*   **Robust Argument Parsing**: Memperbaiki logika pemisahan argumen pada agen agar mendukung path dengan spasi (menggunakan tanda kutip).
*   **Sinkronisasi SSL**: Memastikan *context* SSL selalu diperbarui saat agen melakukan rekoneksi untuk mencegah *handshake failure*




# Implementation Plan - C2 Framework Stabilization

I have studied the current system and identified several areas that are causing features to "not work" or behave inconsistently. This plan addresses session management, tunnel verification, and command execution reliability.

## User Review Required

> [!IMPORTANT]
> Some changes involve adding native Windows API calls to the agent. This improves reliability on systems where PowerShell is restricted.

> [!WARNING]
> I will be modifyng the IPC communication logic to ensure large data transfers (e.g., file downloads or massive task lists) do not hang the UI.

## Proposed Changes

---

### [Component] Server (Daemon & IPC)

#### [MODIFY] [ipc_manager.cpp](file:///d:/SECURITY-LAB/CommandRemote/server/src/ipc_manager.cpp)
- **Session Cleanup**: Modify `EXEC` and `KILL` handlers to remove the session from `active_sessions` immediately if `SendSecureMessage` or `ReceiveSecureMessage` fails. 
- **Robustness**: Increase the IPC receive buffer size and ensure `shutdown(sock, SD_SEND)` is handled correctly on both ends.

#### [MODIFY] [tunnel_manager.cpp](file:///d:/SECURITY-LAB/CommandRemote/server/src/tunnel_manager.cpp)
- **Status Verification**: Update `IsProcessRunning` or add a new check to verify if the last "Handshake" in the log was successful, rather than just checking if the process exists.

---

### [Component] Agent (Client)

#### [MODIFY] [executor.cpp](file:///d:/SECURITY-LAB/CommandRemote/client/executor.cpp)
- **Native Tasklist**: Implement a native C++ `EnumProcesses` fallback for the `tasks` command if PowerShell fails.
- **Native Drive List**: Implement a native C++ `GetLogicalDriveStringsA` fallback for the `drives` command.
- **Robust Argument Parsing**: Fix the `:upload` command argument parser to handle quoted paths or paths with spaces correctly.

#### [MODIFY] [main.cpp](file:///d:/SECURITY-LAB/CommandRemote/client/main.cpp)
- **Connection Resilience**: Ensure the SSL context is properly recreated on every reconnection attempt to prevent "stale" context errors.
- **Connection Keep-Alive**: Ensure consistent keep-alive settings to prevent silent disconnects over long idle periods.

---

### [Component] Common Utilities

#### [MODIFY] [protocol.cpp](file:///d:/SECURITY-LAB/CommandRemote/common/protocol.cpp)
- **Large Payload Support**: Ensure the 50MB limit is consistent and that memory allocation for large buffers is handled safely.

## Open Questions

- **File Limits**: Is 50MB sufficient for your typical usage, or should we implement a "streaming" file transfer for multi-GB files?
- **PowerShell Preference**: Should we always try native C++ first for `tasks` and `drives`, or keep PowerShell as the primary? (Native is usually faster and harder to log/block).

## Verification Plan

### Automated Tests
- Run `build.ps1` to ensure both Server and Agent compile.
- Test `sessions` command after killing an agent manually (e.g., Task Manager) to verify it's removed immediately upon next interaction.
- Test `tasks` and `drives` commands on a machine with PowerShell disabled.

### Manual Verification
- Verify `upload` works with paths like `C:\Users\Public\My File.txt`.
- Check `tunnel status` after manually killing the `localtonet.exe` process to ensure the server detects the stoppage correctly.
