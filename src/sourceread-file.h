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
#ifndef __SOURCEREAD_FILE_H__
#define __SOURCEREAD_FILE_H__

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
** 基于轻量级词法状态机对输入的 C 源代码进行动态缩进与排版格式化。
**
** 算法核心能力与边界处理：
** 1. 语法上下文保护：通过状态标志位识别单双引号字符串与字符字面量，避开转义字符，
**    防止字面量内部的大括号、分号等字符被误判为语法块或语句结尾。
** 2. 注释保护机制：精准区分行注释 (//) 与块注释，注释区域内按原文直接输出，
**    不应用代码缩进与断行逻辑。
** 3. 预处理指令独立：识别行首 '#' 引导的预编译指令（如 #include），重置缩进并在第 0 列顶格输出。
** 4. 动态分层缩进：遇到闭合括号 '}' 时提前缩减层级再排版；遇到开括号 '{' 与语句结束分号 ';'
**    时后置增加层级并自动插入换行。
** 5. 内存安全管理：格式化后的代码直接写入动态扩容的缓冲区，返回堆内存指针。
**
** @param src     待格式化的原始源代码字符串指针（要求以 '\0' 结尾）。
** @param indent  每一层缩进所代表的基础空格基数（例如：通常设置为 2 或 4）。
** @return        返回新分配的已格式化字符串指针，调用方必须负责 free() 释放内存；
**                若入参为空或内存分配失败则返回 NULL。
*/
char*
sr_format_code(const char* source_code, 
               int indent);


#ifdef __cplusplus
}
#endif

#endif // __SOURCEREAD_FILE_H__