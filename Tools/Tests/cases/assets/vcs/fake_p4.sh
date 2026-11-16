#!/bin/sh
# Testler için sahte p4 (Perforce burada kurulu değil): depo = projenin Assets klasöründeki dosyalar; açma (edit/add), kilit ve
# değişiklik listesi durumu proje klasöründeki .fakep4 klasöründe. Gerçek p4'ün -ztag çıktı biçimlerini taklit eder:
#   info, fstat (durum / -Ro açık dosyalar / -Ol boyut), edit, add, lock, unlock, revert, change -o / -i, reopen -c, submit -c,
#   shelve -f / -d -c, changes -s pending|submitted, filelog, describe -s, print -q, diff -du, diff2 -du, sync.
# Gönderilen sürümler .fakep4/depot altında "<yerel yol>#<sürüm>" dosyalarıdır. sync, .fakep4incoming altındaki dosyaları
# "sunucudan gelen" değişiklik olarak projeye kopyalar (Get Latest testi).
D="$(pwd)/.fakep4"
STATE="$D/state"           # yol|eylem|kilit|liste
mkdir -p "$D/pending" "$D/submitted" "$D/depot"
touch "$STATE"
ZTAG=0
LIST=""
while [ $# -gt 0 ]; do
    case "$1" in
        -p|-u|-c) shift 2 ;;
        -x) LIST="$2"; shift 2 ;;
        -ztag) ZTAG=1; shift ;;
        *) break ;;
    esac
done
cmd="$1"; [ $# -gt 0 ] && shift

field() { awk -F'|' -v f="$1" -v n="$2" '$1 == f { v = $n } END { print v }' "$STATE"; }
setstate() {
    awk -F'|' -v f="$1" '$1 != f' "$STATE" > "$STATE.tmp"; mv "$STATE.tmp" "$STATE"
    [ -n "$2$3" ] && echo "$1|$2|$3|${4:-default}" >> "$STATE"
    return 0
}
headrev() {
    n=0
    for c in "$D/depot$1#"*; do
        [ -e "$c" ] || continue
        r="${c##*#}"; [ "$r" -gt "$n" ] && n="$r"
    done
    echo "$n"
}
# -x listesi ya da argümanlar: satır başına bir dosya
files() { if [ -n "$LIST" ]; then cat "$LIST"; else for a in "$@"; do echo "$a"; done; fi; }
# //depot/yol#n ya da /yerel/yol#have -> yerel yol ve sürüm
spec_path() { p="${1%%#*}"; echo "${p#//depot}"; }
spec_rev() {
    case "$1" in
        *"#"*) r="${1##*#}" ;;
        *) r=head ;;
    esac
    case "$r" in have|head) headrev "$(spec_path "$1")" ;; *) echo "$r" ;; esac
}

case "$cmd" in
    info)
        if [ "$ZTAG" = 1 ]; then
            echo "... userName tester"; echo "... clientName test_ws"; echo "... clientRoot $(pwd)"
            echo "... clientStream //stream/main"; echo "... serverAddress fake:1666"
        else
            echo "User name: tester"; echo "Client name: test_ws"; echo "Server address: fake:1666"
        fi
        exit 0 ;;
    fstat)
        mode=""; target=""
        for a in "$@"; do case "$a" in -Ro) mode=opened ;; -Ol) mode=size ;; -*) ;; *) target="$a" ;; esac; done
        if [ "$mode" = size ]; then
            rev=$(headrev "$target"); echo "... depotFile //depot$target"; echo "... clientFile $target"
            [ "$rev" -gt 0 ] && echo "... fileSize $(wc -c < "$D/depot$target#$rev" | tr -d ' ')"
            exit 0
        fi
        dir="${target%/...}"
        find "$dir/Assets" -type f ! -name 'fake_p4.sh' ! -name '*.meta' | sort | while IFS= read -r f; do
            action=$(field "$f" 2); lock=$(field "$f" 3); change=$(field "$f" 4)
            [ "$mode" = opened ] && [ -z "$action" ] && continue
            [ "$action" = "add" ] || echo "... depotFile //depot$f"
            echo "... clientFile $f"
            if [ -n "$action" ]; then echo "... action $action"; echo "... change ${change:-default}"; fi
            [ "$lock" = "ours" ] && echo "... ourLock"
            echo ""
        done
        exit 0 ;;
    edit|add)
        files "$@" | while IFS= read -r f; do
            # Sunucuda henüz sürümü olmayan dosya: açıldığı andaki içerik #1 sayılır (farkın "sahip olunan sürümü")
            if [ "$cmd" = edit ] && [ "$(headrev "$f")" = 0 ]; then mkdir -p "$(dirname "$D/depot$f")"; cp "$f" "$D/depot$f#1"; fi
            setstate "$f" "$cmd" "$(field "$f" 3)" "default"; echo "$f - opened for $cmd"
        done
        exit 0 ;;
    lock)
        f="$1"; [ -n "$(field "$f" 2)" ] || { echo "$f - file(s) not opened on this client."; exit 1; }
        setstate "$f" "$(field "$f" 2)" ours "$(field "$f" 4)"; echo "$f - locking"; exit 0 ;;
    unlock) f="$1"; setstate "$f" "$(field "$f" 2)" "" "$(field "$f" 4)"; echo "$f - unlocking"; exit 0 ;;
    revert)
        files "$@" | while IFS= read -r f; do
            action=$(field "$f" 2); rev=$(headrev "$f")
            [ "$action" = edit ] && [ "$rev" -gt 0 ] && cp "$D/depot$f#$rev" "$f"
            setstate "$f" "" ""; echo "$f#$rev - was ${action:-edit}, reverted"
        done
        exit 0 ;;
    change)
        if [ "$1" = "-o" ]; then
            n="${2:-new}"
            if [ "$n" = new ]; then st=new; cl=default; else st=pending; cl="$n"; fi
            printf 'Change:\t%s\n\nClient:\ttest_ws\n\nUser:\ttester\n\nStatus:\t%s\n\nDescription:\n' "$n" "$st"
            if [ "$n" = new ]; then printf '\t<enter description here>\n'; else sed 's/^/\t/' "$D/pending/$n.desc"; fi
            printf '\nFiles:\n'
            awk -F'|' -v c="$cl" '$2 != "" && $4 == c { print "\t//depot" $1 "\t# " $2 }' "$STATE"
            exit 0
        fi
        if [ "$1" = "-i" ]; then
            spec=$(cat)
            n=$(printf '%s\n' "$spec" | awk -F'\t' '/^Change:/ { print $2 }')
            desc=$(printf '%s\n' "$spec" | awk '/^Description:/ { on = 1; next } on && /^[^\t]/ && NF { on = 0 } on && /^\t/ { sub(/^\t/, ""); print }')
            if [ "$n" = new ]; then
                last=$(cat "$D/counter" 2>/dev/null || echo 0); n=$((last + 1)); echo "$n" > "$D/counter"
                printf '%s\n' "$desc" > "$D/pending/$n.desc"; echo "Change $n created."
            else
                [ -f "$D/pending/$n.desc" ] || { echo "Change $n unknown." >&2; exit 1; }
                printf '%s\n' "$desc" > "$D/pending/$n.desc"; echo "Change $n updated."
            fi
            exit 0
        fi
        exit 1 ;;
    reopen)
        [ "$1" = "-c" ] || exit 1
        n="$2"; shift 2
        files "$@" | while IFS= read -r f; do
            setstate "$f" "$(field "$f" 2)" "$(field "$f" 3)" "$n"; echo "//depot$f - reopened; change $n"
        done
        exit 0 ;;
    submit)
        [ "$1" = "-c" ] || exit 1
        n="$2"
        [ -f "$D/pending/$n.shelved" ] && { echo "Change $n has shelved files -- cannot submit." >&2; exit 1; }
        list=$(awk -F'|' -v c="$n" '$2 != "" && $4 == c { print $1 "|" $2 }' "$STATE")
        [ -n "$list" ] || { echo "No files to submit." >&2; exit 1; }
        echo "Submitting change $n."
        : > "$D/submitted/$n.files"
        printf '%s\n' "$list" | while IFS='|' read -r f action; do
            rev=$(($(headrev "$f") + 1)); mkdir -p "$(dirname "$D/depot$f")"; cp "$f" "$D/depot$f#$rev"
            echo "$f|$action|$rev" >> "$D/submitted/$n.files"; echo "$action //depot$f#$rev"
            setstate "$f" "" ""
        done
        mv "$D/pending/$n.desc" "$D/submitted/$n.desc"; date +%s > "$D/submitted/$n.time"
        echo "Change $n submitted."
        exit 0 ;;
    shelve)
        del=0; n=""
        while [ $# -gt 0 ]; do case "$1" in -d) del=1; shift ;; -f) shift ;; -c) n="$2"; shift 2 ;; *) shift ;; esac; done
        [ -f "$D/pending/$n.desc" ] || { echo "Change $n unknown." >&2; exit 1; }
        if [ "$del" = 1 ]; then rm -f "$D/pending/$n.shelved"; echo "Shelved change $n deleted."; exit 0; fi
        touch "$D/pending/$n.shelved"
        echo "Shelving files for change $n."
        awk -F'|' -v c="$n" '$2 != "" && $4 == c { print $2 " //depot" $1 "#none" }' "$STATE"
        echo "Change $n files shelved."
        exit 0 ;;
    changes)
        status=""; max=1000; path=""
        while [ $# -gt 0 ]; do
            case "$1" in -s) status="$2"; shift 2 ;; -m) max="$2"; shift 2 ;; -c) shift 2 ;; -l) shift ;; *) path="$1"; shift ;; esac
        done
        if [ "$status" = pending ]; then
            for c in "$D/pending/"*.desc; do
                [ -e "$c" ] || continue
                n=$(basename "$c" .desc)
                printf '... change %s\n... time %s\n... user tester\n... client test_ws\n... status pending\n' "$n" "$(date +%s)"
                [ -f "$D/pending/$n.shelved" ] && echo "... shelved "
                printf '... desc %s\n\n' "$(cat "$c")"
            done
            exit 0
        fi
        prefix="${path%/...}"
        count=0
        for n in $(ls "$D/submitted" | sed -n 's/\.desc$//p' | sort -rn); do
            [ "$count" -ge "$max" ] && break
            if [ -n "$path" ]; then
                if [ "$prefix" != "$path" ]; then
                    awk -F'|' -v p="$prefix/" 'index($1, p) == 1 { found = 1 } END { exit !found }' "$D/submitted/$n.files" || continue
                else
                    awk -F'|' -v p="$path" '$1 == p { found = 1 } END { exit !found }' "$D/submitted/$n.files" || continue
                fi
            fi
            printf '... change %s\n... time %s\n... user tester\n... client test_ws\n... status submitted\n... desc %s\n\n' \
                "$n" "$(cat "$D/submitted/$n.time")" "$(cat "$D/submitted/$n.desc")"
            count=$((count + 1))
        done
        exit 0 ;;
    filelog)
        f=""; for a in "$@"; do case "$a" in -*) ;; [0-9]*) ;; *) f="$a" ;; esac; done
        echo "... depotFile //depot$f"
        i=0
        for n in $(ls "$D/submitted" | sed -n 's/\.desc$//p' | sort -rn); do
            line=$(awk -F'|' -v p="$f" '$1 == p' "$D/submitted/$n.files")
            [ -n "$line" ] || continue
            action=$(echo "$line" | cut -d'|' -f2); rev=$(echo "$line" | cut -d'|' -f3)
            printf '... rev%d %s\n... change%d %s\n... action%d %s\n... time%d %s\n... user%d tester\n... client%d test_ws\n... desc%d %s\n' \
                "$i" "$rev" "$i" "$n" "$i" "$action" "$i" "$(cat "$D/submitted/$n.time")" "$i" "$i" "$i" "$(cat "$D/submitted/$n.desc")"
            i=$((i + 1))
        done
        exit 0 ;;
    describe)
        n=""; for a in "$@"; do case "$a" in -*) ;; *) n="$a" ;; esac; done
        [ -f "$D/submitted/$n.desc" ] || { echo "$n - no such changelist." >&2; exit 1; }
        printf '... change %s\n... user tester\n... client test_ws\n... time %s\n... status submitted\n... desc %s\n' \
            "$n" "$(cat "$D/submitted/$n.time")" "$(cat "$D/submitted/$n.desc")"
        i=0
        while IFS='|' read -r f action rev; do
            printf '... depotFile%d //depot%s\n... action%d %s\n... rev%d %s\n' "$i" "$f" "$i" "$action" "$i" "$rev"
            i=$((i + 1))
        done < "$D/submitted/$n.files"
        exit 0 ;;
    print)
        [ "$1" = "-q" ] && shift
        f=$(spec_path "$1"); rev=$(spec_rev "$1")
        [ -f "$D/depot$f#$rev" ] || { echo "$1 - no such file(s)." >&2; exit 1; }
        cat "$D/depot$f#$rev"
        exit 0 ;;
    diff)
        [ "$1" = "-du" ] && shift
        f="$1"; rev=$(headrev "$f")
        [ "$rev" -gt 0 ] || { echo "$f - file(s) not opened on this client." >&2; exit 1; }
        if ! grep -qI . "$D/depot$f#$rev" 2>/dev/null || ! grep -qI . "$f" 2>/dev/null; then
            echo "==== //depot$f#$rev - $f ==== (binary)"; exit 0
        fi
        echo "==== //depot$f#$rev - $f ===="
        diff -u "$D/depot$f#$rev" "$f" | tail -n +3
        exit 0 ;;
    diff2)
        [ "$1" = "-du" ] && shift
        a=$(spec_path "$1"); ra=$(spec_rev "$1"); b=$(spec_path "$2"); rb=$(spec_rev "$2")
        echo "==== //depot$a#$ra (text) - //depot$b#$rb (text) ==== content"
        diff -u "$D/depot$a#$ra" "$D/depot$b#$rb" | tail -n +3
        exit 0 ;;
    sync)
        in="$(pwd)/.fakep4incoming"
        if [ ! -d "$in" ] || [ -z "$(find "$in" -type f | head -1)" ]; then echo "$1 - file(s) up-to-date." >&2; exit 0; fi
        find "$in" -type f | sort | while IFS= read -r src; do
            f="$(pwd)/${src#"$in"/}"; rev=$(($(headrev "$f") + 1))
            mkdir -p "$(dirname "$f")" "$(dirname "$D/depot$f")"
            if [ -e "$f" ]; then verb="updating"; else verb="added as"; fi
            cp "$src" "$f"; cp "$src" "$D/depot$f#$rev"; rm -f "$src"
            echo "//depot$f#$rev - $verb $f"
        done
        exit 0 ;;
esac
echo "Unknown command.  Try 'p4 help' for info."
exit 1
