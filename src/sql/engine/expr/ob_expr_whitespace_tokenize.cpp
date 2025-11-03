/**
 * Copyright (c) 2025 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 * This file contains implementation for whitespace_tokenize expression.
 */

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_whitespace_tokenize.h"
#include "lib/allocator/page_arena.h"
#include "lib/charset/ob_charset.h"
#include "lib/json_type/ob_json_base.h"
#include "lib/json_type/ob_json_tree.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/string/ob_string.h"
#include "lib/string/ob_string_buffer.h"
#include "lib/utility/ob_macro_utils.h"
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace sql
{
ObExprWhitespaceTokenize::ObExprWhitespaceTokenize(common::ObIAllocator &alloc)
    : ObStringExprOperator(alloc,
                           T_FUN_WHITESPACE_TOKENIZE,
                           N_WHITESPACE_TOKENIZE,
                           1,
                           VALID_FOR_GENERATED_COL)
{
}

ObExprWhitespaceTokenize::~ObExprWhitespaceTokenize() {}

int ObExprWhitespaceTokenize::eval_whitespace_tokenize(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;

  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  common::ObArenaAllocator &temp_allocator = tmp_alloc_g.get_allocator();

  ObDatum *text_datum = nullptr;
  ObIJsonBase *json_result = nullptr;

  if (OB_UNLIKELY(expr.arg_cnt_ != 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Args count invalid.", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, text_datum))) {
    LOG_WARN("Fail to eval text arg", K(ret));
  } else if (text_datum->is_null()) {
    expr_datum.set_null();
  } else {
    ObString text_str = text_datum->get_string();
    ObJsonArray *json_array = nullptr;
    
    if (OB_ISNULL(json_array = OB_NEWx(ObJsonArray, (&temp_allocator), &temp_allocator))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("Fail to allocate json array", K(ret));
    } else {
      // Tokenize by whitespace (space character ' ')
      const char *start = text_str.ptr();
      const char *end = start + text_str.length();
      const char *token_start = nullptr;
      const char *token_end = nullptr;
      bool in_token = false;

      // Skip leading spaces
      while (start < end && *start == ' ') {
        ++start;
      }

      token_start = start;

      // Parse tokens separated by space
      for (const char *p = start; p <= end; ++p) {
        bool is_space = (p < end) && (*p == ' ');
        
        if (!in_token && !is_space) {
          // Start of a new token
          token_start = p;
          in_token = true;
        } else if (in_token && (is_space || p == end)) {
          // End of current token
          token_end = p;
          if (token_end > token_start) {
            ObString token(static_cast<int32_t>(token_end - token_start), token_start);
            ObJsonString *json_str = nullptr;
            if (OB_ISNULL(json_str = OB_NEWx(ObJsonString, (&temp_allocator), token.ptr(), token.length()))) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("Fail to allocate json string", K(ret));
            } else if (OB_FAIL(json_array->append(json_str))) {
              LOG_WARN("Fail to append token to array", K(ret));
            }
          }
          in_token = false;
        }
      }

      if (OB_SUCC(ret)) {
        json_result = json_array;
        if (OB_FAIL(ObJsonExprHelper::pack_json_res(expr,
                                                     ctx,
                                                     temp_allocator,
                                                     json_result,
                                                     expr_datum))) {
          LOG_WARN("fail to pack json result", K(ret));
        }
      }
    }
  }

  return ret;
}

int ObExprWhitespaceTokenize::calc_result_type1(ObExprResType &type,
                                                ObExprResType &type1,
                                                common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);

  if (lib::is_oracle_mode()) {
    ret = OB_NOT_IMPLEMENT;
    LOG_USER_ERROR(OB_NOT_IMPLEMENT, "whitespace_tokenize in oracle mode");
  } else {
    // Set result type as JSON
    ObLength length = ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType].get_length();
    type.set_json();
    type.set_length(length);

    // Set parameter type
    if (ob_is_string_type(type1.get_type())) {
      if (type1.get_charset_type() != CHARSET_UTF8MB4) {
        type1.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
      }
    }
  }

  return ret;
}

int ObExprWhitespaceTokenize::cg_expr(ObExprCGCtx &op_cg_ctx,
                                      const ObRawExpr &raw_expr,
                                      ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  CK(rt_expr.arg_cnt_ == 1);
  if (OB_SUCC(ret)) {
    rt_expr.eval_func_ = eval_whitespace_tokenize;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase

