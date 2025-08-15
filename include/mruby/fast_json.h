#pragma once

#include <mruby.h>
#ifndef MRB_64BIT
#error "mruby-fast-json: needs 64-bit integer support"
#endif

MRB_BEGIN_DECL

#define E_JSON_PARSER_ERROR             (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "ParserError"))
#define E_JSON_TAPE_ERROR               (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "TapeError"))
#define E_JSON_STRING_ERROR             (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "StringError"))
#define E_JSON_UNCLOSED_STRING_ERROR    (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "UnclosedStringError"))
#define E_JSON_MEMALLOC_ERROR           (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "MemoryAllocationError"))
#define E_JSON_DEPTH_ERROR              (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "DepthError"))
#define E_JSON_UTF8_ERROR               (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "UTF8Error"))
#define E_JSON_NUMBER_ERROR             (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "NumberError"))
#define E_JSON_CAPACITY_ERROR           (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "CapacityError"))
#define E_JSON_INCORRECT_TYPE_ERROR     (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "IncorrectTypeError"))
#define E_JSON_UNSUPPORTED_ARCHITECTURE_ERROR (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "UnsupportedArchitectureError"))
#define E_JSON_EMPTY_INPUT_ERROR        (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "EmptyInputError"))
#define E_JSON_NO_SUCH_FIELD_ERROR      (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "NoSuchFieldError"))
#define E_JSON_UNEXPECTED_ERROR         (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "UnexpectedError"))


MRB_API mrb_value
mrb_json_dump_mrb_obj(mrb_state *mrb, const mrb_value obj);
MRB_END_DECL
