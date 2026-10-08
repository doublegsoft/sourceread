/*
**                                                         __
**    _________  __  _______________  ________  ____ _____/ /
**   / ___/ __ \/ / / / ___/ ___/ _ \/ ___/ _ \/ __ `/ __  / 
**  (__  ) /_/ / /_/ / /  / /__/  __/ /  /  __/ /_/ / /_/ /  
** /____/\____/\__,_/_/   \___/\___/_/   \___/\__,_/\__,_/                                                          
*/
#ifndef __SOURCEREAD_H__
#define __SOURCEREAD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>

/*!
** 文件处理回调函数原型。
**
** @param parser   语法解析器实例指针。
** @param nodetype 当前匹配到的语法节点类型名称。
** @param filepath 匹配到的文件完整路径。
** @param userdata 用户自定义透传数据指针。
*/
typedef void (*sr_file_handler_fn)(TSParser* parser, 
                                   const char* nodetype, 
                                   const char* filepath, 
                                   void* userdata);

/*!
** 读取指定文件的全部内容到动态分配的内存中，并自动追加 '\0' 结尾。
**
** 注意：返回的字符串指针由 malloc 分配，调用方使用完毕后需负责调用 free() 释放。
** 若打开或读取文件失败，将向 stderr 输出错误并返回 NULL。
*/
char*
sr_read_file(const char* filename);

/*!
** 递归打印包含源码标识符/具体内容的优雅 AST 结构。
**
** @param node         当前遍历的 Tree-sitter 节点。
** @param source_code  对应的原始源代码字符串缓冲区。
** @param indent       当前打印的层级缩进量。
*/
void
sr_print_ast(TSNode node, const char* node_type, const char* source_code, int indent);

/*!
** 递归遍历指定目录，筛选出具有固定后缀名的文件并执行回调。
**
** @param dir_path 目标根目录路径。
** @param ext      需要匹配的文件后缀（例如 ".c"、".json"，若为 NULL 则匹配所有常规文件）。
** @param parser   语法解析器实例指针（透传给回调函数用于语法树分析）。
** @param nodetype 目标语法节点类型名称（透传给回调函数）。
** @param handler  匹配成功时的回调函数。
** @param userdata 透传给回调函数的上下文数据。
*/
void
sr_walk_dir(const char* dir_path,
            const char* ext,
            TSParser* parser,
            const char* nodetype,
            sr_file_handler_fn handler,
            void* userdata);

/*!
** 检查字符串是否以指定后缀名结尾（区分大小写）。
**
** @param str    待检查的文件名或路径。
** @param suffix 目标后缀（如 ".c"、".tree"）。
** @return       匹配返回 1，不匹配返回 0。
*/
int
sr_has_suffix(const char* str, const char* suffix);

/*!
** 从源代码中截取指定 TSNode 对应的完整文本内容。
**
** 注意：返回的字符串由 malloc 分配，调用方需要负责 free() 释放。
**
** @param node        目标语法树节点。
** @param source_code 原始源码字符串缓冲区。
** @return            截取出的文本字符串指针；若节点无效则返回 NULL。
*/
char*
sr_node_text(TSNode node, const char* source_code);

/*!
** 递归遍历 AST，根据指定的节点类型名称提取所有方法/函数源码及名称。
**
** @param node              当前遍历的起始节点（通常传入根节点 root）。
** @param source_code       原始源码字符串缓冲区。
** @param method_node_type  目标方法/函数的节点类型名称（如 "function_definition"、"method_declaration" 等）。
*/
void
sr_extract_methods(TSNode node, const char* source_code, const char* method_node_type);



#ifdef __cplusplus
}
#endif

#endif // __SOURCEREAD_H__