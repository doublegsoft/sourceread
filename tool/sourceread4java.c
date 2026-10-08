/*
**                                                         __
**    _________  __  _______________  ________  ____ _____/ /
**   / ___/ __ \/ / / / ___/ ___/ _ \/ ___/ _ \/ __ `/ __  /
**  (__  ) /_/ / /_/ / /  / /__/  __/ /  /  __/ /_/ / /_/ /
** /____/\____/\__,_/_/   \___/\___/_/   \___/\__,_/\__,_/
*/

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <argparse.h>
#include <tree_sitter/api.h>

// tree-sitter-java 语法符号声明
const TSLanguage* tree_sitter_java(void);

// 二进制协议定长字段定义
#define PROTOCOL_PATH_LEN   512
#define PROTOCOL_METHOD_LEN 128

// B-Tree 阶数 (Order M = 4, 2-3-4树特性: 节点最多 3 个键, 4 个子节点)
#define BTREE_ORDER         4
#define BTREE_MAX_KEYS      (BTREE_ORDER - 1)

/* =========================================================================
 * 1. 基础工具函数 (文件读取与目录遍历)
 * ========================================================================= */

// 读取文件全部内容至字符串
static char*
sr_read_file(const char* filepath)
{
  FILE* fp = fopen(filepath, "rb");
  if (!fp) {
    fprintf(stderr, "警告: 无法打开文件: %s\n", filepath);
    return NULL;
  }

  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  rewind(fp);

  if (size < 0) {
    fclose(fp);
    return NULL;
  }

  char* buffer = (char*)malloc(size + 1);
  if (!buffer) {
    perror("分配读取内存失败");
    fclose(fp);
    return NULL;
  }

  size_t read_bytes = fread(buffer, 1, size, fp);
  buffer[read_bytes] = '\0';
  fclose(fp);

  return buffer;
}

// 检查文件后缀名
static int
has_extension(const char* filename, const char* ext)
{
  size_t file_len = strlen(filename);
  size_t ext_len = strlen(ext);
  if (file_len < ext_len) {
    return 0;
  }
  return (strcmp(filename + file_len - ext_len, ext) == 0);
}

/* =========================================================================
 * 2. 独立 B-Tree 实现 (支持同名方法多 offset 链表索引)
 * ========================================================================= */

// 同名方法偏移量链表节点
typedef struct OffsetItem {
  uint64_t offset;
  struct OffsetItem* next;
} OffsetItem;

// B-Tree 节点定义
typedef struct BTreeNode {
  int num_keys;
  int is_leaf;
  char keys[BTREE_MAX_KEYS][PROTOCOL_METHOD_LEN];
  OffsetItem* offsets[BTREE_MAX_KEYS];
  struct BTreeNode* children[BTREE_ORDER];
} BTreeNode;

// B-Tree 容器
typedef struct {
  BTreeNode* root;
  size_t total_keys;
} BTree;

// 创建 B-Tree 节点
static BTreeNode*
btree_create_node(int is_leaf)
{
  BTreeNode* node = (BTreeNode*)calloc(1, sizeof(BTreeNode));
  if (!node) {
    perror("B-Tree 节点内存分配失败");
    exit(EXIT_FAILURE);
  }
  node->is_leaf = is_leaf;
  return node;
}

// 初始化 B-Tree
static BTree*
btree_init(void)
{
  BTree* tree = (BTree*)malloc(sizeof(BTree));
  if (!tree) {
    perror("B-Tree 内存分配失败");
    exit(EXIT_FAILURE);
  }
  tree->root = btree_create_node(1);
  tree->total_keys = 0;
  return tree;
}

// 分裂满子节点
static void
btree_split_child(BTreeNode* parent, int i, BTreeNode* full_child)
{
  int t = BTREE_ORDER / 2; // t = 2
  BTreeNode* z = btree_create_node(full_child->is_leaf);
  z->num_keys = t - 1;

  for (int j = 0; j < t - 1; j++) {
    strncpy(z->keys[j], full_child->keys[j + t], PROTOCOL_METHOD_LEN);
    z->offsets[j] = full_child->offsets[j + t];
    full_child->offsets[j + t] = NULL;
  }

  if (!full_child->is_leaf) {
    for (int j = 0; j < t; j++) {
      z->children[j] = full_child->children[j + t];
      full_child->children[j + t] = NULL;
    }
  }

  full_child->num_keys = t - 1;

  for (int j = parent->num_keys; j >= i + 1; j--) {
    parent->children[j + 1] = parent->children[j];
  }
  parent->children[i + 1] = z;

  for (int j = parent->num_keys - 1; j >= i; j--) {
    strncpy(parent->keys[j + 1], parent->keys[j], PROTOCOL_METHOD_LEN);
    parent->offsets[j + 1] = parent->offsets[j];
  }

  strncpy(parent->keys[i], full_child->keys[t - 1], PROTOCOL_METHOD_LEN);
  parent->offsets[i] = full_child->offsets[t - 1];
  full_child->offsets[t - 1] = NULL;

  parent->num_keys++;
}

// 向非满节点插入
static void
btree_insert_non_full(BTreeNode* node, const char* key, uint64_t offset)
{
  int i = node->num_keys - 1;

  if (node->is_leaf) {
    while (i >= 0 && strcmp(node->keys[i], key) > 0) {
      strncpy(node->keys[i + 1], node->keys[i], PROTOCOL_METHOD_LEN);
      node->offsets[i + 1] = node->offsets[i];
      i--;
    }

    // 若方法名已存在，追加偏移量到链表（支持同名重载）
    if (i >= 0 && strcmp(node->keys[i], key) == 0) {
      OffsetItem* item = (OffsetItem*)malloc(sizeof(OffsetItem));
      item->offset = offset;
      item->next = node->offsets[i];
      node->offsets[i] = item;
      return;
    }

    strncpy(node->keys[i + 1], key, PROTOCOL_METHOD_LEN - 1);
    node->keys[i + 1][PROTOCOL_METHOD_LEN - 1] = '\0';

    OffsetItem* item = (OffsetItem*)malloc(sizeof(OffsetItem));
    item->offset = offset;
    item->next = NULL;

    node->offsets[i + 1] = item;
    node->num_keys++;
  } else {
    while (i >= 0 && strcmp(node->keys[i], key) > 0) {
      i--;
    }

    if (i >= 0 && strcmp(node->keys[i], key) == 0) {
      OffsetItem* item = (OffsetItem*)malloc(sizeof(OffsetItem));
      item->offset = offset;
      item->next = node->offsets[i];
      node->offsets[i] = item;
      return;
    }

    i++;
    if (node->children[i]->num_keys == BTREE_MAX_KEYS) {
      btree_split_child(node, i, node->children[i]);
      if (strcmp(node->keys[i], key) < 0) {
        i++;
      } else if (strcmp(node->keys[i], key) == 0) {
        OffsetItem* item = (OffsetItem*)malloc(sizeof(OffsetItem));
        item->offset = offset;
        item->next = node->offsets[i];
        node->offsets[i] = item;
        return;
      }
    }
    btree_insert_non_full(node->children[i], key, offset);
  }
}

// 插入记录到 B-Tree
static void
btree_insert(BTree* tree, const char* key, uint64_t offset)
{
  BTreeNode* root = tree->root;
  if (root->num_keys == BTREE_MAX_KEYS) {
    BTreeNode* s = btree_create_node(0);
    tree->root = s;
    s->children[0] = root;
    btree_split_child(s, 0, root);

    int i = 0;
    if (strcmp(s->keys[0], key) < 0) {
      i++;
    } else if (strcmp(s->keys[0], key) == 0) {
      OffsetItem* item = (OffsetItem*)malloc(sizeof(OffsetItem));
      item->offset = offset;
      item->next = s->offsets[0];
      s->offsets[0] = item;
      return;
    }
    btree_insert_non_full(s->children[i], key, offset);
  } else {
    btree_insert_non_full(root, key, offset);
  }
  tree->total_keys++;
}

// 递归序列化节点并写入文件
static void
btree_serialize_node(BTreeNode* node, FILE* idx_fp)
{
  if (!node) {
    return;
  }

  fwrite(&node->is_leaf, sizeof(int), 1, idx_fp);
  fwrite(&node->num_keys, sizeof(int), 1, idx_fp);

  for (int i = 0; i < node->num_keys; i++) {
    fwrite(node->keys[i], 1, PROTOCOL_METHOD_LEN, idx_fp);

    uint32_t count = 0;
    OffsetItem* cur = node->offsets[i];
    while (cur) {
      count++;
      cur = cur->next;
    }
    fwrite(&count, sizeof(uint32_t), 1, idx_fp);

    cur = node->offsets[i];
    while (cur) {
      fwrite(&cur->offset, sizeof(uint64_t), 1, idx_fp);
      cur = cur->next;
    }
  }

  if (!node->is_leaf) {
    for (int i = 0; i <= node->num_keys; i++) {
      btree_serialize_node(node->children[i], idx_fp);
    }
  }
}

// 保存 B-Tree 索引文件
static void
btree_save(BTree* tree, const char* idx_filename)
{
  FILE* fp = fopen(idx_filename, "wb");
  if (!fp) {
    perror("无法保存索引文件");
    return;
  }
  const char magic[8] = "BT_MTHD";
  fwrite(magic, 1, 8, fp);
  btree_serialize_node(tree->root, fp);
  fclose(fp);
}

// 释放 B-Tree 内存
static void
btree_free_node(BTreeNode* node)
{
  if (!node) {
    return;
  }
  for (int i = 0; i < node->num_keys; i++) {
    OffsetItem* cur = node->offsets[i];
    while (cur) {
      OffsetItem* tmp = cur;
      cur = cur->next;
      free(tmp);
    }
  }
  if (!node->is_leaf) {
    for (int i = 0; i <= node->num_keys; i++) {
      btree_free_node(node->children[i]);
    }
  }
  free(node);
}

static void
btree_destroy(BTree* tree)
{
  if (tree) {
    btree_free_node(tree->root);
    free(tree);
  }
}

/* =========================================================================
 * 3. 业务提取上下文与语法树处理
 * ========================================================================= */

typedef struct {
  FILE* out_fp;
  BTree* tree;
  size_t method_count;
} ParseContext;

// 将单个方法按协议写入二进制文件并建立索引
static void
sr_write_method_binary(const char* filepath, TSNode node, const char* source_code, ParseContext* ctx)
{
  // 1. 记录写入前的起始偏移量
  uint64_t current_offset = (uint64_t)ftello(ctx->out_fp);

  // 2. 写入定长路径 (512 字节)
  char path_buf[PROTOCOL_PATH_LEN];
  memset(path_buf, 0, sizeof(path_buf));
  if (filepath != NULL) {
    strncpy(path_buf, filepath, sizeof(path_buf) - 1);
  }
  fwrite(path_buf, 1, sizeof(path_buf), ctx->out_fp);

  // 3. 提取并写入定长方法名 (128 字节)
  char method_buf[PROTOCOL_METHOD_LEN];
  memset(method_buf, 0, sizeof(method_buf));

  TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
  if (ts_node_is_null(name_node)) {
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
      TSNode child = ts_node_child(node, i);
      if (strcmp(ts_node_type(child), "identifier") == 0) {
        name_node = child;
        break;
      }
    }
  }

  if (!ts_node_is_null(name_node)) {
    uint32_t n_start = ts_node_start_byte(name_node);
    uint32_t n_end = ts_node_end_byte(name_node);
    uint32_t n_len = (n_end > n_start) ? (n_end - n_start) : 0;
    if (n_len >= sizeof(method_buf)) {
      n_len = sizeof(method_buf) - 1;
    }
    memcpy(method_buf, source_code + n_start, n_len);
  } else {
    strncpy(method_buf, "<anonymous>", sizeof(method_buf) - 1);
  }
  fwrite(method_buf, 1, sizeof(method_buf), ctx->out_fp);

  // 4. 写入方法体长度 (4 字节)
  uint32_t start_byte = ts_node_start_byte(node);
  uint32_t end_byte = ts_node_end_byte(node);
  uint32_t body_len = (end_byte > start_byte) ? (end_byte - start_byte) : 0;
  fwrite(&body_len, sizeof(uint32_t), 1, ctx->out_fp);

  // 5. 写入方法体实际源码
  if (body_len > 0) {
    fwrite(source_code + start_byte, 1, body_len, ctx->out_fp);
  }

  // 6. 存入 B-Tree 索引
  btree_insert(ctx->tree, method_buf, current_offset);
  ctx->method_count++;
}

// 递归遍历 AST
static void
sr_traverse_and_export(TSNode node, const char* source_code, const char* nodetype, const char* filepath, ParseContext* ctx)
{
  const char* current_type = ts_node_type(node);
  if (strcmp(current_type, nodetype) == 0) {
    sr_write_method_binary(filepath, node, source_code, ctx);
  }

  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    sr_traverse_and_export(ts_node_child(node, i), source_code, nodetype, filepath, ctx);
  }
}

// 解析单个文件
static void
sr_parse_file(TSParser* parser, const char* nodetype, const char* filepath, void* userdata)
{
  ParseContext* ctx = (ParseContext*)userdata;
  char* source_code = sr_read_file(filepath);
  if (!source_code) {
    return;
  }

  TSTree* tree = ts_parser_parse_string(
    parser,
    NULL,
    source_code,
    strlen(source_code)
  );

  TSNode root_node = ts_tree_root_node(tree);
  sr_traverse_and_export(root_node, source_code, nodetype, filepath, ctx);

  ts_tree_delete(tree);
  free(source_code);
}

// 递归扫描目录并调用处理函数
static void
sr_walk_dir(const char* dir_path, const char* ext, TSParser* parser, const char* nodetype,
            void (*cb)(TSParser*, const char*, const char*, void*), void* userdata)
{
  DIR* dir = opendir(dir_path);
  if (!dir) {
    fprintf(stderr, "警告: 无法打开目录 %s: %s\n", dir_path, strerror(errno));
    return;
  }

  struct dirent* entry;
  char full_path[4096];

  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

    struct stat statbuf;
    if (stat(full_path, &statbuf) == -1) {
      continue;
    }

    if (S_ISDIR(statbuf.st_mode)) {
      sr_walk_dir(full_path, ext, parser, nodetype, cb, userdata);
    } else if (S_ISREG(statbuf.st_mode)) {
      if (has_extension(entry->d_name, ext)) {
        cb(parser, nodetype, full_path, userdata);
      }
    }
  }

  closedir(dir);
}

/* =========================================================================
 * 4. 主程序入口
 * ========================================================================= */

static const char* const usages[] =
{
  "sourceread4java [options]",
  NULL,
};

int main(int argc, char* argv[])
{
  char* file = NULL;
  char* proj = NULL;
  char* type = NULL;
  char* output = NULL;
  char* idx_output = NULL;

  struct argparse_option options[] = {
    OPT_HELP(),
    OPT_STRING('f', "file", &file, "input single java file path", NULL, 0, 0),
    OPT_STRING('p', "project", &proj, "input java project path", NULL, 0, 0),
    OPT_STRING('t', "type", &type, "grammar node type (default: method_declaration)", NULL, 0, 0),
    OPT_STRING('o', "output", &output, "output binary file path (default: methods.bin)", NULL, 0, 0),
    OPT_STRING('i', "index", &idx_output, "output btree index path (default: methods.idx)", NULL, 0, 0),
    OPT_END(),
  };

  struct argparse argparse;
  argparse_init(&argparse, options, usages, 0);
  argparse_describe(&argparse, "\nParse java files to binary method records with self-implemented B-Tree index.", NULL);
  
  argc = argparse_parse(&argparse, argc, (const char**) argv);
  if (file == NULL && proj == NULL)
  {
    argparse_usage(&argparse);
    return 1;
  }

  if (type == NULL) {
    type = "method_declaration";
  }
  if (output == NULL) {
    output = "methods.bin";
  }
  if (idx_output == NULL) {
    idx_output = "methods.idx";
  }

  FILE* out_fp = fopen(output, "wb");
  if (!out_fp) {
    perror("无法创建输出二进制数据文件");
    return 1;
  }

  TSParser* parser = ts_parser_new();
  if (!ts_parser_set_language(parser, tree_sitter_java())) {
    fprintf(stderr, "加载 Java 语法失败。\n");
    ts_parser_delete(parser);
    fclose(out_fp);
    return 1;
  }

  ParseContext ctx;
  ctx.out_fp = out_fp;
  ctx.tree = btree_init();
  ctx.method_count = 0;

  if (file) {
    sr_parse_file(parser, type, file, (void*)&ctx);
  } else if (proj) {
    sr_walk_dir(proj, ".java", parser, type, sr_parse_file, (void*)&ctx);
  }

  // 序列化落盘 B-Tree 索引文件
  btree_save(ctx.tree, idx_output);

  printf("处理完成！\n");
  printf("  - 提取方法记录数: %zu\n", ctx.method_count);
  printf("  - 数据存储文件  : %s\n", output);
  printf("  - B-Tree 索引文件: %s\n", idx_output);

  btree_destroy(ctx.tree);
  ts_parser_delete(parser);
  fclose(out_fp);

  return 0;
}
