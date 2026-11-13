#!/bin/sh
# 186 testi için sahte p4 (Perforce burada kurulu değil): depo = projenin Assets klasöründeki dosyalar; açma (edit/add) ve kilit
# durumu proje klasöründeki .fakep4state dosyasında. Gerçek p4'ün -ztag fstat çıktı biçimini taklit eder.
STATE="$(pwd)/.fakep4state"
touch "$STATE"
while [ $# -gt 0 ]; do
    case "$1" in
        -p|-u|-c) shift 2 ;;
        -ztag) shift ;;
        *) break ;;
    esac
done
cmd="$1"; shift
field() { grep -F "$1|" "$STATE" | tail -1 | cut -d'|' -f"$2"; }
setstate() { grep -vF "$1|" "$STATE" > "$STATE.tmp"; mv "$STATE.tmp" "$STATE"; [ -n "$2$3" ] && echo "$1|$2|$3" >> "$STATE"; }
case "$cmd" in
    info) echo "User name: tester"; echo "Client name: test_ws"; echo "Server address: fake:1666"; exit 0 ;;
    fstat)
        dir="${1%/...}"
        find "$dir/Assets" -type f ! -name 'fake_p4.sh' ! -name '*.meta' | while read -r f; do
            action=$(field "$f" 2); lock=$(field "$f" 3)
            [ "$action" = "add" ] || echo "... depotFile //depot$f"
            echo "... clientFile $f"
            [ -n "$action" ] && echo "... action $action"
            [ "$lock" = "ours" ] && echo "... ourLock"
            echo ""
        done
        exit 0 ;;
    edit|add) f="$1"; setstate "$f" "$cmd" "$(field "$f" 3)"; echo "$f - opened for $cmd"; exit 0 ;;
    lock)
        f="$1"; [ -n "$(field "$f" 2)" ] || { echo "$f - file(s) not opened on this client."; exit 1; }
        setstate "$f" "$(field "$f" 2)" ours; echo "$f - locking"; exit 0 ;;
    unlock) f="$1"; setstate "$f" "$(field "$f" 2)" ""; echo "$f - unlocking"; exit 0 ;;
    revert) f="$1"; setstate "$f" "" ""; echo "$f - was edit, reverted"; exit 0 ;;
esac
echo "Unknown command.  Try 'p4 help' for info."
exit 1
