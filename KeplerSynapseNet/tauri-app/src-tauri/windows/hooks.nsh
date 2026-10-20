; Place the node and its runtime next to SynapseNet.exe.
; The loader looks beside the executable first.
!macro NSIS_HOOK_POSTINSTALL
  IfFileExists "$INSTDIR\resources\synapsed.dll" 0 skip_resources
    CopyFiles /SILENT "$INSTDIR\resources\*.dll" "$INSTDIR"
  skip_resources:
  IfFileExists "$INSTDIR\resources\windows-runtime\synapsed.dll" 0 skip_nested
    CopyFiles /SILENT "$INSTDIR\resources\windows-runtime\*.dll" "$INSTDIR"
  skip_nested:
!macroend
