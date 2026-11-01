#!/bin/bash
# ConquerorScript regresyon testleri: her tests/*.cqs çıktısı tests/*.out ile karşılaştırılır.
# Her test üç kipte çalışır: yorumlayıcı (--nojit), varsayılan JIT ve istekli JIT (her fonksiyon/döngü hemen derlenir).  Kullanım: run_tests.sh [cqs yolu]
DIR="$(cd "$(dirname "$0")" && pwd)"
CQS="${1:-$DIR/../../build/bin/cqs}"
pass=0; fail=0
for t in "$DIR"/tests/*.cqs; do
    exp="${t%.cqs}.out"
    [ -f "$exp" ] || continue
    for mode in "--nojit" "" "eager"; do
        if [ "$mode" == "eager" ]; then
            out=$(CQS_JIT_EAGER=1 "$CQS" "$t" --recreate-vm 2>&1 | grep -v '\[CORE\]')
        else
            out=$("$CQS" "$t" $mode --recreate-vm 2>&1 | grep -v '\[CORE\]')
        fi
        if [ "$out" == "$(cat "$exp")" ]; then
            pass=$((pass+1))
        else
            fail=$((fail+1))
            echo "FAIL: $(basename "$t") ${mode:-(jit)}"
            diff <(echo "$out") "$exp" | head -20
        fi
    done
done
echo "passed=$pass failed=$fail"
[ $fail -eq 0 ]
