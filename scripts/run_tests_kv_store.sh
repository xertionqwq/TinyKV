#!/bin/bash

PROJECT_DIR="/home/xertion/Code/Tiny_KV"
BUILD_DIR="$PROJECT_DIR/build"
OUTPUT_DIR="$PROJECT_DIR/output"
OUTPUT_FILE="$OUTPUT_DIR/run_kv_store_log.md"

mkdir -p "$OUTPUT_DIR"
cd "$PROJECT_DIR" || exit 1

cat > "$OUTPUT_FILE" << 'HEADER'
# 测试结果

HEADER

echo "生成时间: $(date '+%Y-%m-%d %H:%M:%S')" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# 1. 编译
echo "## 编译" >> "$OUTPUT_FILE"
echo '```' >> "$OUTPUT_FILE"
cd "$BUILD_DIR" || exit 1
cmake .. -DTINY_KV_BUILD_TESTS=ON >> "$OUTPUT_FILE" 2>&1
make -j$(nproc) >> "$OUTPUT_FILE" 2>&1
BUILD_EXIT=$?
echo '```' >> "$OUTPUT_FILE"
echo "编译退出码: $BUILD_EXIT" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# 2. 运行测试
if [ $BUILD_EXIT -eq 0 ]; then
    echo "## 测试运行" >> "$OUTPUT_FILE"
    echo '```' >> "$OUTPUT_FILE"
    ./tests/test_kv_store >> "$OUTPUT_FILE" 2>&1
    echo '```' >> "$OUTPUT_FILE"
    echo "测试退出码: $?" >> "$OUTPUT_FILE"
fi

echo "Done. Output saved to $OUTPUT_FILE"
