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
    MessageBox MB_YESNO|MB_ICONQUESTION "¿Querés que Buddy arranque solo cuando prendas la computadora?$\n$\nPodés cambiarlo cuando quieras desde el menú del icono de la bandeja." IDNO buddyAutostartNo

    ; Sí: escribir la misma entrada que el menú de la bandeja.
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "Buddy" "$\"$INSTDIR\${MAINBINARYNAME}.exe$\""
    ; Si el Administrador de tareas tenía a Buddy apagado de una instalación
    ; anterior, esa marca gana sobre la entrada recién escrita: sin borrarla,
    ; Windows no lo arranca igual.
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run" "Buddy"
    Goto buddyAutostartDone

    buddyAutostartNo:
    ; Un "no" también es una decisión sobre lo que ya había. El camino "no
    ; desinstalar" de la página de mantenimiento no corre al desinstalador viejo,
    ; así que una entrada de la instalación anterior seguiría arrancando a Buddy
    ; aunque el dueño acabe de decir que no. Abstenerse de escribir no alcanza:
    ; hay que sacarla.
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "Buddy"
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run" "Buddy"

    buddyAutostartDone:
  ${EndIf}
!macroend

!macro NSIS_HOOK_POSTUNINSTALL
  ; La plantilla de Tauri ya borra la entrada del `Run` en una desinstalación de
  ; verdad (se salta el borrado cuando es una actualización con `/UPDATE`, que es
  ; la que conserva la preferencia). La marca del Administrador de tareas tiene
  ; que seguir la misma regla: si se borrara siempre, una actualización
  ; devolvería prendido lo que el dueño había apagado.
  ${If} $UpdateMode <> 1
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run" "Buddy"
  ${EndIf}
!macroend
