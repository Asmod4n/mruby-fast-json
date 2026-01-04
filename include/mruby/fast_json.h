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
#define E_JSON_T_ATOM_ERROR             (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "TAtomError"))
#define E_JSON_F_ATOM_ERROR             (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "FAtomError"))
#define E_JSON_N_ATOM_ERROR             (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "NAtomError"))

#define E_JSON_BIGINT_ERROR             (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "BigIntError"))
#define E_JSON_UNINITIALIZED_ERROR      (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "UninitializedError"))

#define E_JSON_UNESCAPED_CHARS_ERROR    (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "UnescapedCharsError"))

#define E_JSON_NUMBER_OUT_OF_RANGE_ERROR \
    (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "NumberOutOfRangeError"))

#define E_JSON_INDEX_OUT_OF_BOUNDS_ERROR \
    (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "IndexOutOfBoundsError"))

#define E_JSON_IO_ERROR                 (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "IOError"))
#define E_JSON_INVALID_JSON_POINTER_ERROR \
    (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "InvalidJSONPointerError"))

#define E_JSON_INVALID_URI_FRAGMENT_ERROR \
    (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "InvalidURIFragmentError"))

#define E_JSON_PARSER_IN_USE_ERROR      (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "ParserInUseError"))

#define E_JSON_OUT_OF_ORDER_ITERATION_ERROR \
    (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "OutOfOrderIterationError"))

#define E_JSON_INSUFFICIENT_PADDING_ERROR \
    (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "InsufficientPaddingError"))

#define E_JSON_INCOMPLETE_ARRAY_OR_OBJECT_ERROR \
    (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "IncompleteArrayOrObjectError"))

#define E_JSON_SCALAR_DOCUMENT_AS_VALUE_ERROR \
    (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "ScalarDocumentAsValueError"))

#define E_JSON_OUT_OF_BOUNDS_ERROR      (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "OutOfBoundsError"))

#define E_JSON_TRAILING_CONTENT_ERROR  (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "TrailingContentError"))

#define E_JSON_OUT_OF_CAPACITY_ERROR   (mrb_class_get_under(mrb, mrb_module_get(mrb, "JSON"), "OutOfCapacityError"))


MRB_API mrb_value
mrb_json_parse(mrb_state *mrb, mrb_value str, mrb_bool symbolize_names);

MRB_API mrb_value
mrb_json_dump(mrb_state *mrb, const mrb_value obj);

MRB_END_DECL
