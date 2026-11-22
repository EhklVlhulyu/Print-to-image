import io

p = r'E:\Github\ptimage\installer\ptimage.nsi'
raw = open(p, 'rb').read()
bom = raw.startswith(b'\xef\xbb\xbf')
text = raw.decode('utf-8-sig')
orig = text

def sub(old, new, count=1):
    global text
    assert old in text, 'missing: ' + old[:60]
    text = text.replace(old, new, count)

# 1) the x86 INF ships next to the x64 one
sub('  File "${PAYLOAD}\\ptimage.inf"\n',
    '  File "${PAYLOAD}\\ptimage.inf"\n  File "${PAYLOAD}\\ptimage_x86.inf"\n')

# 2) pick the INF matching the running architecture
sub('''  ; NSIS 本体是 32 位程序：关闭 WOW64 重定向，$SYSDIR 才是真正的 System32
  ${DisableX64FSRedirection}
''',
    '''  ; NSIS 本体是 32 位程序：关闭 WOW64 重定向，$SYSDIR 才是真正的 System32
  ${DisableX64FSRedirection}

  ; 驱动按架构使用各自的 INF：两个都是“纯” NTamd64 / NTx86，不带版本修饰，
  ; 因为带修饰的多架构 INF 在部分 Windows 10 上能暂存却无法注册。
  ${If} ${RunningX64}
    StrCpy $R6 "Windows x64"
    StrCpy $R5 "ptimage.inf"
  ${Else}
    StrCpy $R6 "Windows NT x86"
    StrCpy $R5 "ptimage_x86.inf"
  ${EndIf}
''')

# 3) printui fallback uses the chosen INF
sub('''printui.dll,PrintUIEntry /if /b "${PRINTER_NAME}" /f "$INSTDIR\\ptimage.inf" /r "${PORT_NAME}" /m "${PRINTER_NAME}" /q''',
    '''printui.dll,PrintUIEntry /if /b "${PRINTER_NAME}" /f "$INSTDIR\\$R5" /r "${PORT_NAME}" /m "${PRINTER_NAME}" /q''')

# 4) InstallDriver passes the chosen INF and its file name
sub('''  System::Call 'kernel32::SetEnvironmentVariableW(w "PTIMAGE_INF", w "$INSTDIR\\ptimage.inf")'
  System::Call 'kernel32::SetEnvironmentVariableW(w "PTIMAGE_LOG", w "$R4")'
''',
    '''  System::Call 'kernel32::SetEnvironmentVariableW(w "PTIMAGE_INF", w "$INSTDIR\\$R5")'
  System::Call 'kernel32::SetEnvironmentVariableW(w "PTIMAGE_LOG", w "$R4")'
''')

sub('''  System::Call 'kernel32::SetEnvironmentVariableW(w "PTIMAGE_INFNAME", w "ptimage.inf")'
''',
    '''  System::Call 'kernel32::SetEnvironmentVariableW(w "PTIMAGE_INFNAME", w "$R5")'
''')

assert text != orig
open(p, 'wb').write((b'\xef\xbb\xbf' if bom else b'') + text.encode('utf-8'))
print('nsi updated, BOM preserved:', bom)
