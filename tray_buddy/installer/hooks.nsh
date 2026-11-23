; Lo que el instalador pregunta, y lo que deja atado con el sistema.
;
; Buddy no es una app que se abre: vive en la bandeja. Por eso el arranque
; automático se pregunta una sola vez, acá, y después se cambia desde el menú del
; icono de la bandeja ("Iniciar con Windows"), que escribe exactamente la misma
; entrada del registro: un solo lugar donde mirar, dos formas de llegar.

!macro NSIS_HOOK_POSTINSTALL
  ; En una instalación silenciosa (/S) no hay a quién preguntarle, y encender el
  ; arranque automático sin permiso es de las cosas que el dueño descubre tarde.
  ${IfNot} ${Silent}
    MessageBox MB_YESNO|MB_ICONQUESTION "¿Querés que Buddy arranque solo cuando prendas la computadora?$\n$\nPodés cambiarlo cuando quieras desde el menú del icono de la bandeja." IDNO buddySkipAutostart
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "Buddy" "$\"$INSTDIR\${MAINBINARYNAME}.exe$\""
    ; Si el Administrador de tareas tenía a Buddy apagado desde una instalación
    ; anterior, esa marca gana sobre la entrada recién escrita: sin borrarla,
    ; Windows no lo arranca igual.
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run" "Buddy"
    buddySkipAutostart:
  ${EndIf}
!macroend

!macro NSIS_HOOK_POSTUNINSTALL
  ; La plantilla de Tauri ya borra la entrada del `Run` (salvo en una
  ; actualización). La marca del Administrador de tareas quedaría huérfana, y una
  ; instalación nueva nacería "apagada" sin que nadie lo haya decidido.
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run" "Buddy"
!macroend
