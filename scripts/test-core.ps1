param([string]$Clang = 'clang', [string]$Lld = 'ld.lld')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'native/test-results'
New-Item -ItemType Directory -Force $out | Out-Null
foreach ($source in @('app/twitch.c', 'tests/core_test.c', 'tests/wasm_libc.c')) {
    $object = Join-Path $out (([IO.Path]::GetFileNameWithoutExtension($source)) + '.obj')
    & $Clang --target=x86_64-pc-windows-msvc -std=c11 -O1 -ffreestanding -fno-builtin -fno-stack-protector -mno-stack-arg-probe -nostdlib -Wall -Wextra -Werror -I "$root/native/app" -c "$root/native/$source" -o $object
    if ($LASTEXITCODE -ne 0) { throw "C compilation failed: $source" }
}
$dll = Join-Path $out 'core-test.dll'
& $Lld -flavor link /dll /noentry /nodefaultlib /export:test_core /export:tw_token_url /export:tw_variant /export:tw_channel /export:tw_request "/out:$dll" "$out/twitch.obj" "$out/core_test.obj" "$out/wasm_libc.obj"
if ($LASTEXITCODE -ne 0) { throw 'C test link failed' }
$class = 'PTwitchTest' + [guid]::NewGuid().ToString('N')
$literal = $dll.Replace('\', '\\')
Add-Type -TypeDefinition "using System.Runtime.InteropServices; public static class $class { [DllImport(`"$literal`", CallingConvention = CallingConvention.Cdecl)] public static extern int test_core(); }"
$line = ([type]$class).GetMethod('test_core').Invoke($null, @())
if ($line -ne 0) { throw "C core assertion failed: native/tests/core_test.c:$line" }
Write-Output 'PASS: native C Twitch channel, request, token, URL and rendition tests'
