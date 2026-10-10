#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <tree_sitter/api.h>

#include "sourceread-file.h"
#include "sourceread-treesitter.h"

/* 声明 Tree-sitter Java 语法解析器接口 */
extern const TSLanguage* tree_sitter_java(void);

/*!
** 从当前方法节点向上追溯 AST，提取其直接所属的类/接口/枚举/Record名称。
**
** @param method_node  当前方法节点
** @param source_code  Java 源代码
** @return char*       类名字符串 (调用方需 free)
*/
static char*
sr_get_enclosing_class_name(TSNode method_node, const char* source_code)
{
  TSNode parent = ts_node_parent(method_node);

  while (!ts_node_is_null(parent))
  {
    const char* type = ts_node_type(parent);

    /* 匹配 Java 的所有类型定义体 */
    if (strcmp(type, "class_declaration") == 0 ||
        strcmp(type, "interface_declaration") == 0 ||
        strcmp(type, "enum_declaration") == 0 ||
        strcmp(type, "record_declaration") == 0)
    {
      TSNode class_name_node = ts_node_child_by_field_name(parent, "name", 4);
      if (!ts_node_is_null(class_name_node))
      {
        return sr_node_text(class_name_node, source_code);
      }
      return strdup("AnonymousClass");
    }

    parent = ts_node_parent(parent);
  }

  return strdup("TopLevel");
}

/*!
** 深度优先递归遍历 AST，捕获方法并格式化输出 ClassName#methodName 及其源码内容。
*/
static void
sr_visit_java_methods(TSNode node, 
                      const char* nodetype, 
                      const char* source_code, 
                      const char* filepath,
                      void* userdata)
{
  if (ts_node_is_null(node)) return;

  const char* type = ts_node_type(node);

  /* 匹配普通方法 (method_declaration) 以及构造函数 (constructor_declaration) */
  if (strcmp(type, nodetype) == 0 || strcmp(type, "constructor_declaration") == 0)
  {
    /* 1. 提取方法名称 */
    TSNode method_name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(method_name_node))
    {
      char* method_name = sr_node_text(method_name_node, source_code);
      /* 2. 向上追溯所属类名 */
      char* class_name = sr_get_enclosing_class_name(node, source_code);

      /* 3. 提取完整方法源码内容 (包含签名与方法体) */
      char* method_code = sr_node_text(node, source_code);

      /*
      ** 说明：如果只想要花括号内部的 { ... }，可以按下面这样写：
      ** TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
      ** char* method_code = sr_node_text(body_node, source_code);
      */

      if (class_name && method_name && method_code)
      {
        printf("\n=====================================================================\n");
        printf("METHOD: %s#%s\n", class_name, method_name);
        printf("FILE:   %s\n", filepath);
        printf("--------------------------- METHOD BODY -----------------------------\n");
        printf("%s\n", method_code);
        printf("=====================================================================\n");
      }

      free(class_name);
      free(method_name);
      free(method_code);
    }
  }

  /* 递归遍历所有子节点 */
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++)
  {
    TSNode child = ts_node_child(node, i);
    sr_visit_java_methods(child, nodetype, source_code, filepath, userdata);
  }
}

/* ========================================================================== */
/*                实现回调函数与主入口 (符合 sr_file_handler_fn)               */
/* ========================================================================== */

/*!
** 符合 sr_file_handler_fn 签名的 Java 文件处理回调函数。
*/
void
sr_java_method_handler(TSParser* parser, 
                       const char* nodetype, 
                       const char* filepath, 
                       void* userdata)
{
  char* source_code = sr_read_file(filepath);
  if (!source_code) return;

  TSTree* tree = ts_parser_parse_string(parser, NULL, source_code, strlen(source_code));
  if (tree)
  {
    TSNode root_node = ts_tree_root_node(tree);
    sr_visit_java_methods(root_node, nodetype, source_code, filepath, userdata);
    ts_tree_delete(tree);
  }

  free(source_code);
}

/*!
** 主调用入口：遍历工程并提取所有 Java 函数及方法体。
**
** @param proj_path Java 项目根目录路径
*/
void
sr_extract_all_java_methods(const char* proj_path)
{
  if (!proj_path) return;

  /* 1. 初始化 Java Tree-sitter 解析器 */
  TSParser* parser = ts_parser_new();
  if (!ts_parser_set_language(parser, tree_sitter_java())) {
    fprintf(stderr, "[Error] Failed to load Tree-sitter Java grammar.\n");
    ts_parser_delete(parser);
    return;
  }

  printf("=====================================================================\n");
  printf("  Scanning Java Methods in: %s\n", proj_path);
  printf("=====================================================================\n");

  /* 2. 直接调用原有的 sr_walk_dir 进行文件扫描，传入 sr_java_method_handler 回调 */
  sr_walk_dir(proj_path, 
              ".java", 
              parser, 
              "method_declaration", 
              sr_java_method_handler, 
              NULL);

  /* 3. 释放解析器 */
  ts_parser_delete(parser);
  printf("=====================================================================\n");
}

int main(void)
{
  sr_extract_all_java_methods("/Users/christian/export/local/works/koron.com/abpms/03.Development/abpms-java");
  return 0;
}