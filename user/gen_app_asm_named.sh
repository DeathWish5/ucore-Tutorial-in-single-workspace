#!/bin/bash
# 生成应用程序汇编（带名称表，ch5格式）
# 用法: gen_app_asm_named.sh output.S app1.elf app2.elf ...

OUTPUT=$1
shift

APPS=("$@")
COUNT=${#APPS[@]}

cat > "$OUTPUT" << EOF
# Auto-generated application data with names
.section .data
.global app_names
.global apps
.global app_count

# 应用程序名称表（供 ch5 按名查找）
.align 3
app_names:
EOF

# 生成应用程序名称表
for app in "${APPS[@]}"; do
    name=$(basename "$app" .elf)
    echo "    .asciz \"$name\"" >> "$OUTPUT"
done

# 生成 app_count
echo "" >> "$OUTPUT"
echo ".align 3" >> "$OUTPUT"
echo "app_count:" >> "$OUTPUT"
echo "    .quad $COUNT" >> "$OUTPUT"

# 生成应用程序元数据（与 app_meta_t 兼容）
# 布局: base(0), step(0), count, 然后是 count+1 个地址
cat >> "$OUTPUT" << EOF

# 应用程序元数据 (app_meta_t 格式)
.align 3
apps:
    .quad 0                 # base = 0 (不复制)
    .quad 0                 # step = 0
    .quad $COUNT            # count
EOF

# 生成 count+1 个地址（每个应用的起始，最后一个的结束）
for i in "${!APPS[@]}"; do
    echo "    .quad app_${i}_start" >> "$OUTPUT"
done
echo "    .quad app_${COUNT}_end   # 最后一个的结束地址" >> "$OUTPUT"

# 嵌入应用程序数据
echo "" >> "$OUTPUT"

for i in "${!APPS[@]}"; do
    echo ".align 12" >> "$OUTPUT"
    echo "app_${i}_start:" >> "$OUTPUT"
    echo "    .incbin \"${APPS[$i]}\"" >> "$OUTPUT"
done

# 最后一个结束标签
echo "app_${COUNT}_end:" >> "$OUTPUT"

echo "Generated $OUTPUT with $COUNT apps"
