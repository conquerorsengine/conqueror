SUMMARY = "Conqueror Engine game for AGL: ${CONQUEROR_GAME_NAME}"
DESCRIPTION = "Installs a game folder built with the Conqueror Engine editor (Build Settings -> Build, Linux), \
a launcher (/usr/bin/<app id>) and a desktop entry so the AGL application launcher can start it."
LICENSE = "CLOSED"

# local.conf (ya da bir .bbappend) içinde:
#   CONQUEROR_GAME_DIR  = "/home/me/Builds/MyGame"   editörde derlenmiş oyun klasörü (.cqproj burada)
#   CONQUEROR_APP_ID    = "mygame"                   Wayland app_id, /usr/bin adı ve .desktop dosyası
#   CONQUEROR_GAME_NAME = "My Game"
CONQUEROR_GAME_DIR ??= ""
CONQUEROR_APP_ID ??= "conqueror-game"
CONQUEROR_GAME_NAME ??= "Conqueror Game"

UNPACKDIR ??= "${WORKDIR}"
SRC_URI = "file://conqueror-game.sh.in file://conqueror-game.desktop.in"

RDEPENDS:${PN} = "conqueror-player"

do_configure[noexec] = "1"
do_compile[noexec] = "1"
do_install[vardeps] += "CONQUEROR_GAME_DIR CONQUEROR_APP_ID CONQUEROR_GAME_NAME"

do_install() {
    if [ -z "${CONQUEROR_GAME_DIR}" ] || [ ! -d "${CONQUEROR_GAME_DIR}" ]; then
        bbfatal "Set CONQUEROR_GAME_DIR to a game folder built with the Conqueror editor (contains the .cqproj)"
    fi
    gamedir="${datadir}/conqueror/${CONQUEROR_APP_ID}"
    install -d ${D}${gamedir}
    # Masaüstü ikilileri (oyuncu çalıştırılabiliri ve lib/) alınmaz: oyuncu conqueror-player paketinden gelir
    for entry in "${CONQUEROR_GAME_DIR}"/*; do
        [ -e "$entry" ] || continue
        name=$(basename "$entry")
        case "$name" in lib|Library|Logs|Temp) continue ;; esac
        if [ -f "$entry" ] && head -c 4 "$entry" | grep -q "ELF"; then
            continue
        fi
        cp -R --no-preserve=ownership "$entry" ${D}${gamedir}/
    done

    install -d ${D}${bindir} ${D}${datadir}/applications
    sed -e "s|@APP_ID@|${CONQUEROR_APP_ID}|g" \
        -e "s|@GAME_DIR@|${gamedir}|g" \
        -e "s|@PLAYER@|${libexecdir}/conqueror/conqueror-player|g" \
        ${UNPACKDIR}/conqueror-game.sh.in > ${D}${bindir}/${CONQUEROR_APP_ID}
    chmod 0755 ${D}${bindir}/${CONQUEROR_APP_ID}
    sed -e "s|@APP_ID@|${CONQUEROR_APP_ID}|g" \
        -e "s|@NAME@|${CONQUEROR_GAME_NAME}|g" \
        -e "s|@BIN@|${bindir}/${CONQUEROR_APP_ID}|g" \
        ${UNPACKDIR}/conqueror-game.desktop.in > ${D}${datadir}/applications/${CONQUEROR_APP_ID}.desktop
}

FILES:${PN} = "${bindir}/${CONQUEROR_APP_ID} ${datadir}/conqueror/${CONQUEROR_APP_ID} ${datadir}/applications/${CONQUEROR_APP_ID}.desktop"
# Oyun eklentileri (Plugins/*.so) olabilir; oyun verisi mimariden bağımsız değildir
INSANE_SKIP:${PN} += "file-rdeps already-stripped"
