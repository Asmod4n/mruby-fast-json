#include <mruby.h>
#include <mruby/class.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/string.h>
#include <mruby/numeric.h>
#include <mruby/fast_json.h>
#include <mruby/cpp_helpers.hpp>
#include <mruby/num_helpers.hpp>
#include <mruby/presym.h>
#include <mruby/object.h>
#include <mruby/branch_pred.h>
#include <simdjson.h>

using namespace simdjson;

static padded_string_view
simdjson_safe_view_from_mrb_string(mrb_state* mrb, mrb_value str, padded_string& jsonbuffer)
{
  const char* cstr = RSTRING_PTR(str);
  size_t len = RSTRING_LEN(str);

  if (mrb_frozen_p(mrb_obj_ptr(str))) {
    jsonbuffer = padded_string(cstr, len);
    return jsonbuffer;
  }

  size_t required = len + SIMDJSON_PADDING;
  if (RSTRING_CAPA(str) < required) {
    mrb_str_resize(mrb, str, required);
    RSTR_SET_LEN(RSTRING(str), len);
    cstr = RSTRING_PTR(str);
  }

  return padded_string_view(cstr, len, required);
}

static mrb_value convert_array(mrb_state *mrb, dom::element arr_el, mrb_bool symbolize_names);
static mrb_value convert_object(mrb_state *mrb, dom::element obj_el, mrb_bool symbolize_names);

static mrb_value
convert_element(mrb_state *mrb, dom::element el, mrb_bool symbolize_names)
{
  switch (el.type()) {
    case dom::element_type::ARRAY:
      return convert_array(mrb, el, symbolize_names);

    case dom::element_type::OBJECT:
      return convert_object(mrb, el, symbolize_names);

    case dom::element_type::INT64:
      return mrb_convert_number(mrb, static_cast<int64_t>(el.get<int64_t>()));

    case dom::element_type::UINT64:
      return mrb_convert_number(mrb, static_cast<uint64_t>(el.get<uint64_t>()));

    case dom::element_type::DOUBLE:
      return mrb_convert_number(mrb, static_cast<double>(el.get<double>()));

    case dom::element_type::STRING: {
      std::string_view sv(el);
      return mrb_str_new(mrb, sv.data(), sv.size());
    }

    case dom::element_type::BOOL:
      return mrb_bool_value(el.get_bool());

    case dom::element_type::NULL_VALUE:
      return mrb_nil_value();
  }
}

static mrb_value
convert_array(mrb_state *mrb, dom::element arr_el, mrb_bool symbolize_names)
{
  dom::array arr = arr_el.get_array();
  mrb_value ary = mrb_ary_new_capa(mrb, arr.size());
  int arena_index = mrb_gc_arena_save(mrb);
  for (dom::element item : arr) {
    mrb_ary_push(mrb, ary, convert_element(mrb, item, symbolize_names));
    mrb_gc_arena_restore(mrb, arena_index);
  }
  return ary;
}

using KeyConverterFn = mrb_value(*)(mrb_state*, std::string_view);

static mrb_value
convert_key_as_str(mrb_state* mrb, std::string_view sv) {
  return mrb_str_new(mrb, sv.data(), sv.size());
}

static mrb_value
convert_key_as_sym(mrb_state* mrb, std::string_view sv) {
  return mrb_symbol_value(mrb_intern(mrb, sv.data(), sv.size()));
}

static mrb_value
convert_object(mrb_state *mrb, dom::element obj_el, mrb_bool symbolize_names)
{
  dom::object obj = obj_el.get_object();

  mrb_value hash = mrb_hash_new_capa(mrb, obj.size());
  int arena_index = mrb_gc_arena_save(mrb);
  KeyConverterFn convert_key = symbolize_names ? convert_key_as_sym : convert_key_as_str;

  for (auto& kv : obj) {
    mrb_value key = convert_key(mrb, kv.key);
    mrb_value val = convert_element(mrb, kv.value, symbolize_names);
    mrb_hash_set(mrb, hash, key, val);
    mrb_gc_arena_restore(mrb, arena_index);
  }

  return hash;
}

static void raise_simdjson_error(mrb_state *mrb, error_code code)
{
  const char *msg = error_message(code);

  switch (code) {
    case UNCLOSED_STRING:          mrb_raise(mrb, E_JSON_UNCLOSED_STRING_ERROR, msg); break;
    case STRING_ERROR:             mrb_raise(mrb, E_JSON_STRING_ERROR, msg); break;
    case UNESCAPED_CHARS:          mrb_raise(mrb, E_JSON_UNESCAPED_CHARS_ERROR, msg); break;

    case TAPE_ERROR:               mrb_raise(mrb, E_JSON_TAPE_ERROR, msg); break;
    case DEPTH_ERROR:              mrb_raise(mrb, E_JSON_DEPTH_ERROR, msg); break;
    case INCOMPLETE_ARRAY_OR_OBJECT:
                                   mrb_raise(mrb, E_JSON_INCOMPLETE_ARRAY_OR_OBJECT_ERROR, msg); break;
    case TRAILING_CONTENT:         mrb_raise(mrb, E_JSON_TRAILING_CONTENT_ERROR, msg); break;

    case MEMALLOC:                 mrb_raise(mrb, E_JSON_MEMALLOC_ERROR, msg); break;
    case CAPACITY:                 mrb_raise(mrb, E_JSON_CAPACITY_ERROR, msg); break;
    case OUT_OF_CAPACITY:          mrb_raise(mrb, E_JSON_OUT_OF_CAPACITY_ERROR, msg); break;
    case INSUFFICIENT_PADDING:     mrb_raise(mrb, E_JSON_INSUFFICIENT_PADDING_ERROR, msg); break;

    case NUMBER_ERROR:             mrb_raise(mrb, E_JSON_NUMBER_ERROR, msg); break;
    case BIGINT_ERROR:             mrb_raise(mrb, E_JSON_BIGINT_ERROR, msg); break;
    case NUMBER_OUT_OF_RANGE:      mrb_raise(mrb, E_JSON_NUMBER_OUT_OF_RANGE_ERROR, msg); break;

    case T_ATOM_ERROR:             mrb_raise(mrb, E_JSON_T_ATOM_ERROR, msg); break;
    case F_ATOM_ERROR:             mrb_raise(mrb, E_JSON_F_ATOM_ERROR, msg); break;
    case N_ATOM_ERROR:             mrb_raise(mrb, E_JSON_N_ATOM_ERROR, msg); break;

    case UTF8_ERROR:               mrb_raise(mrb, E_JSON_UTF8_ERROR, msg); break;

    case EMPTY:                    mrb_raise(mrb, E_JSON_EMPTY_INPUT_ERROR, msg); break;
    case UNINITIALIZED:            mrb_raise(mrb, E_JSON_UNINITIALIZED_ERROR, msg); break;
    case PARSER_IN_USE:            mrb_raise(mrb, E_JSON_PARSER_IN_USE_ERROR, msg); break;
    case SCALAR_DOCUMENT_AS_VALUE: mrb_raise(mrb, E_JSON_SCALAR_DOCUMENT_AS_VALUE_ERROR, msg); break;

    case INCORRECT_TYPE:           mrb_raise(mrb, E_JSON_INCORRECT_TYPE_ERROR, msg); break;
    case NO_SUCH_FIELD:            mrb_raise(mrb, E_JSON_NO_SUCH_FIELD_ERROR, msg); break;
    case INDEX_OUT_OF_BOUNDS:      mrb_raise(mrb, E_JSON_INDEX_OUT_OF_BOUNDS_ERROR, msg); break;
    case OUT_OF_BOUNDS:            mrb_raise(mrb, E_JSON_OUT_OF_BOUNDS_ERROR, msg); break;
    case OUT_OF_ORDER_ITERATION:   mrb_raise(mrb, E_JSON_OUT_OF_ORDER_ITERATION_ERROR, msg); break;

    case IO_ERROR:                 mrb_raise(mrb, E_JSON_IO_ERROR, msg); break;
    case INVALID_JSON_POINTER:     mrb_raise(mrb, E_JSON_INVALID_JSON_POINTER_ERROR, msg); break;
    case INVALID_URI_FRAGMENT:     mrb_raise(mrb, E_JSON_INVALID_URI_FRAGMENT_ERROR, msg); break;

    case UNSUPPORTED_ARCHITECTURE: mrb_raise(mrb, E_JSON_UNSUPPORTED_ARCHITECTURE_ERROR, msg); break;
    case UNEXPECTED_ERROR:         mrb_raise(mrb, E_JSON_UNEXPECTED_ERROR, msg); break;

    case SUCCESS:
    default:
      mrb_raise(mrb, E_JSON_PARSER_ERROR, msg);
      break;
  }
}


MRB_API mrb_value
mrb_json_parse(mrb_state *mrb, mrb_value str, mrb_bool symbolize_names)
{
  dom::parser parser;
  padded_string jsonbuffer;
  auto view = simdjson_safe_view_from_mrb_string(mrb, str, jsonbuffer);
  auto result = parser.parse(view);

  if (unlikely(result.error() != SUCCESS)) {
    raise_simdjson_error(mrb, result.error());
  }

  return convert_element(mrb, result.value(), symbolize_names);
}

static mrb_value
mrb_json_parse_m(mrb_state *mrb, mrb_value self)
{
  mrb_value str;
  mrb_value kw_values[1] = { mrb_undef_value() }; // default value for symbolize_names
  mrb_sym kw_names[] = { MRB_SYM(symbolize_names) };
  mrb_kwargs kwargs = {
    1,                // num: number of keywords
    0,                // required: none required
    kw_names,
    kw_values,
    NULL
  };

  mrb_get_args(mrb, "S:", &str, &kwargs);

  // Fallback default
  mrb_bool symbolize_names = FALSE;
  if (!mrb_undef_p(kw_values[0])) {
    symbolize_names = mrb_bool(kw_values[0]); // cast to mrb_bool
  }

  return mrb_json_parse(mrb, str, symbolize_names);
}

static inline void json_encode_nil(builder::string_builder& builder)
{
  builder.append_null();
}

static inline void json_encode_false(builder::string_builder& builder)
{
  builder.append(false);
}

static inline void json_encode_false_type(mrb_value v, builder::string_builder& builder)
{
  if (mrb_nil_p(v)) {
      json_encode_nil(builder);
  } else {
      json_encode_false(builder);
  }
}

static inline void json_encode_true(builder::string_builder& builder)
{
  builder.append(true);
}

static void json_encode_symbol(mrb_state* mrb, mrb_value v, builder::string_builder& builder)
{
  v = mrb_sym_str(mrb, mrb_symbol(v));
  std::string_view sv(RSTRING_PTR(v), RSTRING_LEN(v));
  builder.escape_and_append_with_quotes(sv);
}
#ifndef MRB_NO_FLOAT
static inline void json_encode_float(mrb_value v, builder::string_builder& builder)
{
  builder.append(mrb_float(v));
}
#endif
static inline void json_encode_integer(mrb_value v, builder::string_builder& builder)
{
  builder.append(mrb_integer(v));
}

struct DumpHashCtx {
    builder::string_builder& builder;
    bool first;
};

static void json_encode(mrb_state* mrb, mrb_value v, builder::string_builder& builder);

static int dump_hash_cb(mrb_state* mrb, mrb_value key, mrb_value val, void* data)
{
    auto* ctx = static_cast<DumpHashCtx*>(data);

    if (ctx->first) ctx->first = false;
    else ctx->builder.append_comma();

    json_encode(mrb, key, ctx->builder);
    ctx->builder.append_colon();
    json_encode(mrb, val, ctx->builder);

    return 0; // continue iteration
}

static void json_encode_hash(mrb_state* mrb, mrb_value v, builder::string_builder& builder)
{
  builder.start_object();
  DumpHashCtx ctx{builder, true};
  mrb_hash_foreach(mrb, mrb_hash_ptr(v), dump_hash_cb, &ctx);
  builder.end_object();
}

static void json_encode_array(mrb_state* mrb, mrb_value v, builder::string_builder& builder)
{
  builder.start_array();
  mrb_int n = RARRAY_LEN(v);

  if (n > 0) {
      json_encode(mrb, mrb_ary_ref(mrb, v, 0), builder);

      for (mrb_int i = 1; i < n; ++i) {
          builder.append_comma();
          json_encode(mrb, mrb_ary_ref(mrb, v, i), builder);
      }
  }

  builder.end_array();
}

static void json_encode_string(mrb_value v, builder::string_builder& builder)
{
  std::string_view sv(RSTRING_PTR(v), RSTRING_LEN(v));
  builder.escape_and_append_with_quotes(sv);
}

static void json_encode(mrb_state* mrb, mrb_value v, builder::string_builder& builder)
{
    switch (mrb_type(v)) {
        case MRB_TT_FALSE: {
          json_encode_false_type(v, builder);
        } break;
        case MRB_TT_TRUE: {
          json_encode_true(builder);
        } break;
        case MRB_TT_SYMBOL: {
          json_encode_symbol(mrb, v, builder);
        } break;
#ifndef MRB_NO_FLOAT
        case MRB_TT_FLOAT: {
          json_encode_float(v, builder);
        } break;
#endif
        case MRB_TT_INTEGER: {
          json_encode_integer(v, builder);
        } break;
        case MRB_TT_HASH: {
          json_encode_hash(mrb, v, builder);
        } break;
        case MRB_TT_ARRAY: {
          json_encode_array(mrb, v, builder);
        } break;
        case MRB_TT_STRING: {
          json_encode_string(v, builder);
        } break;
        default: {
          json_encode_string(mrb_obj_as_string(mrb, v), builder);
        }
    }
}

MRB_API mrb_value
mrb_json_dump(mrb_state* mrb, mrb_value obj)
{
    builder::string_builder sb;
    json_encode(mrb, obj, sb);
    if (unlikely(!sb.validate_unicode())) {
      mrb_raise(mrb, E_JSON_UTF8_ERROR, "invalid utf-8");
    }
    std::string_view sv = sb.view();
    return mrb_str_new(mrb, sv.data(), sv.size());
}

static mrb_value
mrb_json_dump_m(mrb_state *mrb, mrb_value self)
{
  mrb_value obj;
  mrb_get_args(mrb, "o", &obj);

  return mrb_json_dump(mrb, obj);
}

#define DEFINE_MRB_TO_JSON(func_name, ENCODER_CALL)                   \
  static mrb_value func_name(mrb_state *mrb, mrb_value o)             \
  {                                                                   \
      builder::string_builder sb;                                     \
      ENCODER_CALL;                                                   \
      if (unlikely(!sb.validate_unicode())) {                         \
        mrb_raise(mrb, E_JSON_UTF8_ERROR, "invalid utf-8");           \
      }                                                               \
      std::string_view sv = sb.view();                                \
      return mrb_str_new(mrb, sv.data(),sv.size());                   \
  }


DEFINE_MRB_TO_JSON(mrb_string_to_json,
    json_encode_string(o, sb)
);
DEFINE_MRB_TO_JSON(mrb_array_to_json,
    json_encode_array(mrb, o, sb)
);
DEFINE_MRB_TO_JSON(mrb_hash_to_json,
    json_encode_hash(mrb, o, sb)
);
#ifndef MRB_NO_FLOAT
DEFINE_MRB_TO_JSON(mrb_float_to_json,
    json_encode_float(o, sb)
);
#endif
DEFINE_MRB_TO_JSON(mrb_integer_to_json,
    json_encode_integer(o, sb)
);
DEFINE_MRB_TO_JSON(mrb_true_to_json,
    json_encode_true(sb)
);
DEFINE_MRB_TO_JSON(mrb_false_to_json,
    json_encode_false(sb)
);
DEFINE_MRB_TO_JSON(mrb_nil_to_json,
    json_encode_nil(sb)
);
DEFINE_MRB_TO_JSON(mrb_symbol_to_json,
    json_encode_symbol(mrb, o, sb)
);

extern "C" void
mrb_mruby_fast_json_gem_init(mrb_state *mrb)
{
  struct RClass *json_mod    = mrb_define_module_id(mrb, MRB_SYM(JSON));
  struct RClass *json_error  = mrb_define_class_under_id(mrb, json_mod, MRB_SYM(ParserError), mrb->eStandardError_class);

#define DEFINE_JSON_ERROR(NAME) \
  mrb_define_class_under_id(mrb, json_mod, MRB_SYM(NAME##Error), json_error)

DEFINE_JSON_ERROR(Tape);
DEFINE_JSON_ERROR(String);
DEFINE_JSON_ERROR(UnclosedString);
DEFINE_JSON_ERROR(MemoryAllocation);
DEFINE_JSON_ERROR(Depth);
DEFINE_JSON_ERROR(UTF8);
DEFINE_JSON_ERROR(Number);
DEFINE_JSON_ERROR(Capacity);
DEFINE_JSON_ERROR(IncorrectType);
DEFINE_JSON_ERROR(EmptyInput);

DEFINE_JSON_ERROR(TAtom);
DEFINE_JSON_ERROR(FAtom);
DEFINE_JSON_ERROR(NAtom);

DEFINE_JSON_ERROR(BigInt);
DEFINE_JSON_ERROR(NumberOutOfRange);

DEFINE_JSON_ERROR(UnescapedChars);

DEFINE_JSON_ERROR(Uninitialized);
DEFINE_JSON_ERROR(ParserInUse);
DEFINE_JSON_ERROR(ScalarDocumentAsValue);

DEFINE_JSON_ERROR(IncompleteArrayOrObject);
DEFINE_JSON_ERROR(TrailingContent);

DEFINE_JSON_ERROR(OutOfCapacity);
DEFINE_JSON_ERROR(InsufficientPadding);

DEFINE_JSON_ERROR(IndexOutOfBounds);
DEFINE_JSON_ERROR(OutOfBounds);
DEFINE_JSON_ERROR(OutOfOrderIteration);
DEFINE_JSON_ERROR(NoSuchField);

DEFINE_JSON_ERROR(IO);
DEFINE_JSON_ERROR(InvalidJSONPointer);
DEFINE_JSON_ERROR(InvalidURIFragment);

DEFINE_JSON_ERROR(UnsupportedArchitecture);
DEFINE_JSON_ERROR(Unexpected);


  mrb_define_class_method_id(mrb, json_mod, MRB_SYM(parse), mrb_json_parse_m, MRB_ARGS_REQ(1)|MRB_ARGS_KEY(1, 0));
  mrb_define_class_method_id(mrb, json_mod, MRB_SYM(dump), mrb_json_dump_m, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, mrb->object_class, MRB_SYM(to_json), mrb_json_dump, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, mrb->string_class, MRB_SYM(to_json), mrb_string_to_json, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, mrb->array_class, MRB_SYM(to_json), mrb_array_to_json, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, mrb->hash_class, MRB_SYM(to_json), mrb_hash_to_json, MRB_ARGS_NONE());
#ifndef MRB_NO_FLOAT
  mrb_define_method_id(mrb, mrb->float_class, MRB_SYM(to_json), mrb_float_to_json, MRB_ARGS_NONE());
#endif
  mrb_define_method_id(mrb, mrb->integer_class, MRB_SYM(to_json), mrb_integer_to_json, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, mrb->true_class, MRB_SYM(to_json), mrb_true_to_json, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, mrb->false_class, MRB_SYM(to_json), mrb_false_to_json, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, mrb->nil_class, MRB_SYM(to_json), mrb_nil_to_json, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, mrb->symbol_class, MRB_SYM(to_json), mrb_symbol_to_json, MRB_ARGS_NONE());
}

extern "C" void
mrb_mruby_fast_json_gem_final(mrb_state *mrb) {}
