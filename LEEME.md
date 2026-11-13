# Claraboya Grab

Plugin VST3 para Cubase (Windows). Pegás un link, lo baja como WAV a la frecuencia de la sesión (24 bit) y lo arrastrás a un track.

- **Directo:** YouTube, YouTube Music, SoundCloud, Bandcamp, Vimeo, Instagram, TikTok, Facebook, Mixcloud y todo lo que soporta yt-dlp. Pegás el link y baja.
- **Spotify, Apple Music, Deezer, TIDAL, Amazon Music:** el audio de esas plataformas viene cifrado, así que no se baja de ahí. El plugin lee qué tema es (artista, título, duración) y te muestra **opciones de YouTube Music** para que elijas cuál bajar. Arriba quedan las más parecidas y la que dura lo mismo que el original lleva la marca **MISMA DURACIÓN**.
- **Texto:** escribí "artista tema" y mientras escribís aparecen las opciones (con tapa, disco y duración). Pestañas **CANCIONES** (audio de estudio) y **VIDEOS** (videoclips, vivos, etc.).
- **Preview:** tocá la tapa de una opción para escuchar 30 s (desde el 30 % del tema, nivelado en volumen para comparar parejo). Suena mientras baja; las primeras opciones (y la que tenga el mouse encima) se preparan solas mientras mirás la lista, así arranca casi al instante. Si el tema tiene publicidad, YouTube hace esperar unos segundos antes de dejarlo escuchar: la app lo muestra con una cuenta regresiva. Tocala de nuevo para parar. En Cubase suena por el canal donde está el plugin; en la app suelta, por la salida de audio de Windows.
- Podés pegar varios links a la vez (uno por línea): los directos bajan solos y los de Spotify/texto te los va mostrando de a uno ("TEMA 1 DE 3", con **SALTEAR**).

No toca el audio del canal donde lo pongas: es solo la ventana del bajador.

---

## 1. Instalarlo
1. Bajá el instalador: **[ClaraboyaGrab-Instalador.exe](https://github.com/joaquinnachman/claraboyagrab/releases/latest/download/ClaraboyaGrab-Instalador.exe)** (siempre la última versión).
2. Abrilo. Windows puede avisar que es de un editor desconocido: **Más información › Ejecutar de todas formas**. Pone el plugin en `C:\Program Files\Common Files\VST3` y el programa suelto en el menú Inicio.
3. En Cubase: Studio › VST Plug-in Manager › actualizar. Aparece en **Tools › Claraboya Grab**. Cargalo como insert en cualquier canal.
4. La primera vez tocá **INSTALAR**: baja yt-dlp, FFmpeg y Deno (unos 200 MB) a `%APPDATA%\Claraboya Grab\bin`. Es una sola vez.

El programa suelto (**Claraboya Grab** en el menú Inicio) es la misma herramienta sin Cubase. Ahí la frecuencia del WAV se elige en **AJUSTES** (en Cubase sigue a la sesión).

## 2. Actualizaciones
Claraboya Grab se fija solo (al abrir y cada 6 horas) si hay una versión nueva. Si hay, aparece un aviso con las novedades: **ACTUALIZAR** baja el instalador, verifica que llegó entero y lo abre (en la app suelta, la app se cierra sola para poder actualizarse; en Cubase, el instalador te pide cerrar Cubase: guardá antes). **MÁS TARDE** lo esconde hasta la próxima vez que lo abras. También está **AJUSTES › Buscar versión nueva**.

Cada cambio que se sube a `main` se publica solo como Release (`build-N`) con el instalador y el `.zip`.

### Compilarlo vos (opcional)
- **GitHub:** cada push compila en la pestaña **Actions** ("Compilar para Windows"); el resultado queda en "Artifacts".
- **En tu PC:** abrí PowerShell y pegá
  ```
  winget install Kitware.CMake Git.Git
  winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
  ```
  Reiniciá la PC y hacé doble clic en `compilar.bat`. La primera vez tarda (baja JUCE). Las compilaciones propias son "de prueba" y no se actualizan solas.

## 3. Usarlo
1. Copiá un link (o escribí artista y tema en el campo).
2. **PEGAR Y BAJAR** (si el campo está vacío, toma lo que tengas copiado). Con un link de YouTube baja directo; con Spotify o un texto aparecen las opciones: **tapa = escuchar 30 s**, **resto de la fila = bajar** (o flechas y Enter). **CERRAR** o Esc para salir.
3. Cuando aparece **ARRASTRÁ**, arrastrá la fila al proyecto. Cubase crea el track solo.

**LIMPIAR LISTA** vacía la lista de recientes; los WAV siguen en la carpeta. Con clic derecho en una fila: mostrar en la carpeta, copiar ruta, sacar de la lista o mandar el archivo a la papelera. Doble clic la abre en el Explorador.

Los WAV quedan en `Documentos\Claraboya Grab` (se cambia en **AJUSTES**). Conviene que Cubase copie lo que importás a la carpeta del proyecto, así la sesión no depende de esa carpeta.

## 4. Si algo falla
- **El preview no carga:** queda anotado qué pasó en `%APPDATA%\Claraboya Grab\preview-log.txt` (pegá esa dirección en el Explorador de Windows). Mandá ese archivo para revisarlo.
- **El preview no suena en Cubase:** el plugin tiene que estar en un canal que vaya a la salida (no muteado, con el fader arriba). Si dice "No llega sonido", Cubase no está procesando ese canal: probá en otro canal o usá la app suelta.
- **YouTube dejó de bajar:** AJUSTES › Actualizar yt-dlp. YouTube cambia seguido y yt-dlp se actualiza para seguirle el ritmo.
- **"Pide iniciar sesión" o restricción de edad:** AJUSTES › Usar sesión de Firefox (usa tu sesión de YouTube en Firefox). Chrome no sirve para esto en Windows porque cifra sus cookies.
- **Algo se rompió con las herramientas:** AJUSTES › Reinstalar herramientas.

## 5. Calidad y uso
Lo que baja de YouTube es audio comprimido (Opus/AAC, ~128–160 kbps), aunque el archivo final sea WAV. Sirve para referencias de arreglo, sonido general y balance; para comparar el agudo fino de un master no tanto. Viene con su volumen original: para un A/B justo, igualá loudness.

Bajar de estas plataformas va contra sus términos de uso: pensado para escuchar referencias en el estudio, no para usar ese audio en una producción.

---

### Para desarrollo
- `Source/Resolver.cpp`: qué tema es cada link (Spotify por su página "embed", Apple por iTunes, Deezer por su API).
- `Source/Search.cpp`: buscador de YouTube Music con opciones (y respaldo con yt-dlp), orden por parecido y duración.
- `Source/GrabEngine.cpp`: cola de descargas, historial, yt-dlp, FFmpeg, instalador.
- `Source/Updater.cpp`: aviso de versión nueva (consulta la última Release `build-N` de GitHub, baja y verifica el instalador).
- `installer/ClaraboyaGrab.iss`: instalador de Windows (Inno Setup), lo arma GitHub Actions.
- `Source/Preview.cpp`: preview de 30 s: yt-dlp resuelve el link (cliente sin JavaScript, con respaldo) con sus headers y el `available_at` de YouTube (espera por publicidad), FFmpeg decodifica directo al reproductor y suena mientras llega; links por adelantado, caché en memoria, reintento ante 403 y plan B con `--download-sections`. Anota todo en `preview-log.txt`. Nunca traba el hilo de audio.
- `Source/PluginEditor.cpp`: interfaz. `Source/StandaloneApp.cpp`: ventana de la app suelta.
- Pruebas: `cmake -B build -DCLARABOYA_BUILD_TESTS=ON` y correr `ClaraboyaGrabTests` (con `--live` prueba contra Spotify y YouTube Music de verdad; en GitHub corre solo en cada rama que no sea `main`).
- Logo e íconos: `Resources/brand/` (se regeneran con `tools/make_icons.py` a partir de la claraboya bordó).
- Tipografías Bebas Neue y Montserrat bajo licencia SIL OFL (`Resources/`).
