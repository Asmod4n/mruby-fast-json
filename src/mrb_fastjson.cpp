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
#ifndef MRB_DEBUG
#define NDEBUG
#define __OPTIMIZE__=1
#endif

#include <simdjson.h>

using namespace simdjson;

static mrb_value convert_element(mrb_state *mrb, dom::element el, mrb_bool symbolize_names);
static mrb_value convert_array(mrb_state *mrb, dom::element arr_el, mrb_bool symbolize_names);
static mrb_value convert_object(mrb_state *mrb, dom::element obj_el, mrb_bool symbolize_names);

static void raise_simdjson_error(mrb_state *mrb, error_code code) {
  const char *msg = error_message(code);

  switch (code) {
    case UNCLOSED_STRING:       mrb_raise(mrb, E_JSON_UNCLOSED_STRING_ERROR, msg); break;
    case STRING_ERROR:          mrb_raise(mrb, E_JSON_STRING_ERROR, msg); break;
    case TAPE_ERROR:            mrb_raise(mrb, E_JSON_TAPE_ERROR, msg); break;
    case DEPTH_ERROR:           mrb_raise(mrb, E_JSON_DEPTH_ERROR, msg); break;
    case MEMALLOC:              mrb_raise(mrb, E_JSON_MEMALLOC_ERROR, msg); break;
    case CAPACITY:              mrb_raise(mrb, E_JSON_CAPACITY_ERROR, msg); break;
    case NUMBER_ERROR:          mrb_raise(mrb, E_JSON_NUMBER_ERROR, msg); break;
    case UTF8_ERROR:            mrb_raise(mrb, E_JSON_UTF8_ERROR, msg); break;
    case UNEXPECTED_ERROR:      mrb_raise(mrb, E_JSON_UNEXPECTED_ERROR, msg); break;
    case EMPTY:                 mrb_raise(mrb, E_JSON_EMPTY_INPUT_ERROR, msg); break;
    case INCORRECT_TYPE:        mrb_raise(mrb, E_JSON_INCORRECT_TYPE_ERROR, msg); break;
    case NO_SUCH_FIELD:         mrb_raise(mrb, E_JSON_NO_SUCH_FIELD_ERROR, msg); break;
    case UNSUPPORTED_ARCHITECTURE: mrb_raise(mrb, E_JSON_UNSUPPORTED_ARCHITECTURE_ERROR, msg); break;
    default:                              mrb_raise(mrb, E_JSON_PARSER_ERROR, msg); break;
  }
}

static padded_string_view
simdjson_safe_view_from_mrb_string(mrb_state* mrb, mrb_value str, padded_string& jsonbuffer) {
  const char* cstr = RSTRING_PTR(str);
  size_t len = RSTRING_LEN(str);

  if (mrb_frozen_p(mrb_obj_ptr(str))) {
    jsonbuffer = padded_string(cstr, len); // copies & pads safely
    return jsonbuffer;
  }

  size_t required = len + SIMDJSON_PADDING;
  if (RSTRING_CAPA(str) < required) {
    mrb_str_resize(mrb, str, required); // resize backing buffer
    RSTR_SET_LEN(RSTRING(str), len);     // keep logical length unchanged
    cstr = RSTRING_PTR(str);             // refresh pointer after resize
  }

  return padded_string_view(cstr, len, required);
}

static mrb_value
mrb_json_parse(mrb_state *mrb, mrb_value self)
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

struct DumpHashCtx {
    builder::string_builder& builder;
    bool first;
};

static void json_encode(mrb_state* mrb, mrb_value v, builder::string_builder& builder);

// Hash foreach callback
static int dump_hash_cb(mrb_state* mrb, mrb_value key, mrb_value val, void* data) {
    auto* ctx = static_cast<DumpHashCtx*>(data);

    if (ctx->first) ctx->first = false;
    else ctx->builder.append_comma();

    json_encode(mrb, key, ctx->builder);
    ctx->builder.append_colon();
    json_encode(mrb, val, ctx->builder);

    return 0; // continue iteration
}

// Main encoder
static void json_encode(mrb_state* mrb, mrb_value v, builder::string_builder& builder) {
    switch (mrb_type(v)) {
        case MRB_TT_FALSE:
            if (mrb_nil_p(v)) {
                builder.append_null();
            } else {
                builder.append(false);
            }
            break;
        case MRB_TT_TRUE:
            builder.append(true);
            break;
        case MRB_TT_SYMBOL: {
            v = mrb_sym_str(mrb, mrb_symbol(v));
            std::string_view sv(RSTRING_PTR(v), RSTRING_LEN(v));
            builder.escape_and_append_with_quotes(sv);
        } break;
        case MRB_TT_FLOAT: {
            builder.append(mrb_float(v));

        } break;

        case MRB_TT_INTEGER: {
            builder.append(mrb_integer(v));
        } break;
        case MRB_TT_HASH: {
            builder.start_object();
            DumpHashCtx ctx{builder, true};
            mrb_hash_foreach(mrb, mrb_hash_ptr(v), dump_hash_cb, &ctx);
            builder.end_object();
        } break;
        case MRB_TT_ARRAY: {
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
        } break;
        case MRB_TT_STRING: {
            std::string_view sv(RSTRING_PTR(v), RSTRING_LEN(v));
            builder.escape_and_append_with_quotes(sv);
        } break;
        default: {
            v = mrb_obj_as_string(mrb, v);
            std::string_view sv(RSTRING_PTR(v), RSTRING_LEN(v));
            builder.escape_and_append_with_quotes(sv);
        }
    }
}

// Public dump entrypoint for any mrb_value
MRB_API mrb_value mrb_json_dump_mrb_obj(mrb_state* mrb, mrb_value obj) {
    builder::string_builder sb;
    json_encode(mrb, obj, sb);
    if (unlikely(!sb.validate_unicode())) {
      mrb_raise(mrb, E_JSON_UTF8_ERROR, "invalid utf-8");
    }
    std::string_view p = sb.view();
    return mrb_str_new(mrb, p.data(), p.size());
}

static mrb_value
mrb_json_dump_m(mrb_state *mrb, mrb_value self)
{
  mrb_value obj;
  mrb_get_args(mrb, "o", &obj);

  return mrb_json_dump_mrb_obj(mrb, obj);
}

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

  mrb_define_class_method_id(mrb, json_mod, MRB_SYM(parse), mrb_json_parse, MRB_ARGS_REQ(1)|MRB_ARGS_KEY(1, 0));
  mrb_define_class_method_id(mrb, json_mod, MRB_SYM(dump), mrb_json_dump_m, MRB_ARGS_REQ(1));
}

extern "C" void
mrb_mruby_fast_json_gem_final(mrb_state *mrb) {}
