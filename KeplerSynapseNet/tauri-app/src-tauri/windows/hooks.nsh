; Place the node and its runtime next to SynapseNet.exe.
; The loader looks beside the executable first.
!macro NSIS_HOOK_POSTINSTALL
  IfFileExists "$INSTDIR\resources\synapsed.dll" 0 skip_resources
    CopyFiles /SILENT "$INSTDIR\resources\*.dll" "$INSTDIR"
  skip_resources:
  IfFileExists "$INSTDIR\resources\windows-runtime\synapsed.dll" 0 skip_nested
    CopyFiles /SILENT "$INSTDIR\resources\windows-runtime\*.dll" "$INSTDIR"
  skip_nested:
  IfFileExists "$INSTDIR\resources\synapsenet.conf.example" 0 skip_seed_stage
    CopyFiles /SILENT "$INSTDIR\resources\synapsenet.conf.example" "$INSTDIR"
  skip_seed_stage:
  IfFileExists "$PROFILE\.synapsenet\synapsenet.conf" skip_seed_user
    CreateDirectory "$PROFILE\.synapsenet"
    IfFileExists "$INSTDIR\synapsenet.conf.example" 0 skip_seed_user
      CopyFiles /SILENT "$INSTDIR\synapsenet.conf.example" "$PROFILE\.synapsenet\synapsenet.conf"
  skip_seed_user:
!macroend
