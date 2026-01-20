#!/bin/bash
#
# 生成嵌入用户程序的汇编文件
# 用法: ./gen_app_asm.sh <base> <step> <output> <bin1> [bin2 ...]
#

set -e

if [ $# -lt 4 ]; then
    echo "Usage: $0 <base> <step> <output> <bin1> [bin2 ...]"
    exit 1
fi

BASE=$1
STEP=$2
OUTPUT=$3
shift 3
BINS=("$@")
COUNT=${#BINS[@]}

cat > "$OUTPUT" <<EOF
# Auto-generated application metadata
    .section .data
    .align 3
    .global apps
apps:
    .quad $BASE     # base address
    .quad $STEP     # step between apps
    .quad $COUNT    # app count
EOF

# 位置数组
for i in $(seq 0 $((COUNT - 1))); do
    echo "    .quad app_${i}_start" >> "$OUTPUT"
done
echo "    .quad app_$((COUNT - 1))_end" >> "$OUTPUT"

# 嵌入二进制
for i in $(seq 0 $((COUNT - 1))); do
    BIN="${BINS[$i]}"
    [ -f "$BIN" ] || { echo "Error: $BIN not found" >&2; exit 1; }
    cat >> "$OUTPUT" <<EOF

    .align 3
app_${i}_start:
    .incbin "$BIN"
app_${i}_end:
EOF
done

echo "Generated $OUTPUT with $COUNT apps"
