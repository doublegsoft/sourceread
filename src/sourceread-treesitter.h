/*
**                                                         __
**    _________  __  _______________  ________  ____ _____/ /
**   / ___/ __ \/ / / / ___/ ___/ _ \/ ___/ _ \/ __ `/ __  / 
**  (__  ) /_/ / /_/ / /  / /__/  __/ /  /  __/ /_/ / /_/ /  
** /____/\____/\__,_/_/   \___/\___/_/   \___/\__,_/\__,_/       
**
** Copyright [yyyy] [name of copyright owner]
** 
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
** 
**     http://www.apache.org/licenses/LICENSE-2.0
** 
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.                                                   
*/
#ifndef __SOURCEREAD_TREESITTER_H__
#define __SOURCEREAD_TREESITTER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>

/*!
** 递归遍历 Tree-sitter 语法树，提取并格式化输出指定类型节点的源代码片段。
**
** 算法流程：
** 1. 递归终止判定：检查当前节点是否为空 (Null Node)。
** 2. 节点类型比对：若当前节点类型与目标类型 (node_type) 匹配：
**    - 提取该节点在原始源文件中的原始文本切片；
**    - 调用代码格式化引擎应用缩进排版；
**    - 打印格式化后的文本并立即释放所有临时分配的堆内存，防止内存泄漏。
** 3. 递归下降：若当前节点未命中，则深度优先遍历其所有子节点继续匹配。
**
** @param node         当前递归访问的 Tree-sitter 语法树节点。
** @param node_type    需要过滤的目标语法节点类型（例如："function_definition", "compound_statement" 等）。
** @param source_code  包含完整原始代码的字符串缓冲区。
** @param indent       排版时应用的初始空格缩进层级。
*/
void
sr_print_source(TSNode node, 
                const char* node_type, 
                const char* source_code, 
                int indent);

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

#endif // __SOURCEREAD_TREESITTER_H__