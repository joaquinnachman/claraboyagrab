# Claraboya Grab

Plugin VST3 para Cubase (Windows). Pegás un link, lo baja como WAV a la frecuencia de la sesión (24 bit) y lo arrastrás a un track.

- **Directo:** YouTube, YouTube Music, SoundCloud, Bandcamp, Vimeo, Instagram, TikTok, Facebook, Mixcloud y todo lo que soporta yt-dlp.
- **Spotify, Apple Music, Deezer, TIDAL, Amazon Music:** el audio de esas plataformas viene cifrado, así que no se baja de ahí. El plugin lee qué tema es (artista, título, duración) y baja ese mismo tema de YouTube Music. Si la duración no coincide con la original, te avisa en amarillo (puede ser otra versión o un remaster).
- **Texto suelto:** escribí "artista tema" y lo busca en YouTube Music.
- Podés pegar varios links a la vez (uno por línea).

No toca el audio del canal donde lo pongas: es solo la ventana del bajador.

---

## 1. Conseguir el .vst3

### Opción A: GitHub (no instalás nada)
1. Creá un repositorio **privado** en github.com y subí esta carpeta entera (con la carpeta `.github`).
2. Entrá a la pestaña **Actions**: arranca solo "Compilar para Windows" (unos 10 minutos).
3. Cuando termina, abrí la ejecución y bajá **ClaraboyaGrab-Windows** (abajo de todo, "Artifacts").

### Opción B: compilar en tu PC
1. Abrí PowerShell y pegá:
   ```
   winget install Kitware.CMake Git.Git
   winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
   ```
2. Reiniciá la PC y hacé doble clic en `compilar.bat`. La primera vez tarda (baja JUCE).

## 2. Instalarlo
1. Copiá la carpeta **`Claraboya Grab.vst3`** a `C:\Program Files\Common Files\VST3`.
2. En Cubase: Studio › VST Plug-in Manager › actualizar. Aparece en **Tools › Claraboya Grab**.
3. Cargalo como insert en cualquier canal. La primera vez tocá **INSTALAR**: baja yt-dlp, FFmpeg y Deno (unos 200 MB) a `%APPDATA%\Claraboya Grab\bin`. Es una sola vez.

También queda `Claraboya Grab.exe`, la misma herramienta como programa suelto, por si la querés usar fuera de Cubase.

## 3. Usarlo
1. Copiá un link (o escribí artista y tema en el campo).
2. **PEGAR Y BAJAR** (si el campo está vacío, toma lo que tengas copiado).
3. Cuando aparece **ARRASTRÁ**, arrastrá la fila al proyecto. Cubase crea el track solo.

Con clic derecho en una fila: mostrar en la carpeta, copiar ruta o mandar a la papelera. Doble clic la abre en el Explorador.

Los WAV quedan en `Documentos\Claraboya Grab` (se cambia en **AJUSTES**). Conviene que Cubase copie lo que importás a la carpeta del proyecto, así la sesión no depende de esa carpeta.

## 4. Si algo falla
- **YouTube dejó de bajar:** AJUSTES › Actualizar yt-dlp. YouTube cambia seguido y yt-dlp se actualiza para seguirle el ritmo.
- **"Pide iniciar sesión" o restricción de edad:** AJUSTES › Usar sesión de Firefox (usa tu sesión de YouTube en Firefox). Chrome no sirve para esto en Windows porque cifra sus cookies.
- **Algo se rompió con las herramientas:** AJUSTES › Reinstalar herramientas.

## 5. Calidad y uso
Lo que baja de YouTube es audio comprimido (Opus/AAC, ~128–160 kbps), aunque el archivo final sea WAV. Sirve para referencias de arreglo, sonido general y balance; para comparar el agudo fino de un master no tanto. Viene con su volumen original: para un A/B justo, igualá loudness.

Bajar de estas plataformas va contra sus términos de uso: pensado para escuchar referencias en el estudio, no para usar ese audio en una producción.

---

### Para desarrollo
- `Source/Resolver.cpp`: qué hacer con cada link (streaming → YouTube Music).
- `Source/GrabEngine.cpp`: cola de descargas, yt-dlp, FFmpeg, instalador.
- `Source/PluginEditor.cpp`: interfaz.
- Pruebas: `cmake -B build -DCLARABOYA_BUILD_TESTS=ON` y correr `ClaraboyaGrabTests`.
- Tipografías Bebas Neue y Montserrat bajo licencia SIL OFL (`Resources/`).
